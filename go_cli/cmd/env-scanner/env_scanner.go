package main

import (
	"bufio"
	"encoding/json"
	"flag"
	"fmt"
	"io/fs"
	"os"
	"path/filepath"
	"regexp"
	"strings"
	"time"
)

// EnvFile represents detected environment file information
type EnvFile struct {
	File           string   `json:"file"`
	RelativePath   string   `json:"relative_path"`
	Exists         bool     `json:"exists"`
	VariableCount  int      `json:"variable_count"`
	Variables      []string `json:"variables"`
	SizeBytes      int64    `json:"size_bytes"`
}

// EnvReport contains full environment scan results
type EnvReport struct {
	ScanDirectory         string              `json:"scan_directory"`
	ScanTimestamp         string              `json:"scan_timestamp"`
	EnvironmentFiles      []EnvFile           `json:"environment_files"`
	CodeFilesUsingEnvVars map[string][]string `json:"code_files_using_env_vars"`
	AllEnvironmentVars    []string            `json:"all_environment_variables"`
	Summary               map[string]int      `json:"summary"`
}

// EnvScanner scans directories for environment files and variables
type EnvScanner struct {
	RootDirectory string
}

// NewEnvScanner creates a new EnvScanner instance
func NewEnvScanner(directory string) *EnvScanner {
	absPath, _ := filepath.Abs(directory)
	return &EnvScanner{
		RootDirectory: absPath,
	}
}

// FindAllEnvFiles recursively finds all .env* files
func (es *EnvScanner) FindAllEnvFiles() []string {
	var files []string

	filepath.WalkDir(es.RootDirectory, func(path string, d fs.DirEntry, err error) error {
		if err != nil {
			return nil
		}

		if !d.IsDir() {
			name := strings.ToLower(d.Name())
			if strings.HasPrefix(name, ".env") || name == ".env" {
				files = append(files, path)
			}
		}
		return nil
	})

	return files
}

// ParseEnvFile reads and parses an environment file
func (es *EnvScanner) ParseEnvFile(path string) (map[string]string, error) {
	vars := make(map[string]string)

	file, err := os.Open(path)
	if err != nil {
		return vars, err
	}
	defer file.Close()

	scanner := bufio.NewScanner(file)
	for scanner.Scan() {
		line := strings.TrimSpace(scanner.Text())
		if line == "" || strings.HasPrefix(line, "#") {
			continue
		}

		parts := strings.SplitN(line, "=", 2)
		if len(parts) == 2 {
			key := strings.TrimSpace(parts[0])
			value := strings.TrimSpace(parts[1])
			vars[key] = value
		}
	}

	return vars, scanner.Err()
}

// ScanDirectory scans directory for environment files
func (es *EnvScanner) ScanDirectory() ([]EnvFile, error) {
	envPaths := es.FindAllEnvFiles()
	var results []EnvFile

	for _, path := range envPaths {
		vars, err := es.ParseEnvFile(path)
		if err != nil {
			continue
		}

		relPath, err := filepath.Rel(es.RootDirectory, path)
		if err != nil {
			relPath = path
		}

		stat, err := os.Stat(path)
		if err != nil {
			continue
		}

		varNames := make([]string, 0, len(vars))
		for k := range vars {
			varNames = append(varNames, k)
		}

		results = append(results, EnvFile{
			File:           path,
			RelativePath:   relPath,
			Exists:         true,
			VariableCount:  len(vars),
			Variables:      varNames,
			SizeBytes:      stat.Size(),
		})

		fmt.Printf("\n📄 Found: %s\n", relPath)
		fmt.Printf("   Variables: %d\n", len(vars))
		fmt.Printf("   Size: %d bytes\n", stat.Size())

		if len(varNames) > 0 {
			fmt.Println("   Sample variables:")
			for i, key := range varNames {
				if i >= 5 {
					fmt.Printf("     ... and %d more\n", len(varNames)-5)
					break
				}
				fmt.Printf("     - %s\n", key)
			}
		}
	}

	return results, nil
}

// ScanForEnvVariables finds env var usage in code files
func (es *EnvScanner) ScanForEnvVariables() map[string][]string {
	result := make(map[string][]string)
	envPattern := regexp.MustCompile(`(?:os\.Getenv|os\.LookupEnv|env\.Get|process\.env\.|ENV\[)[\'"]?([A-Z0-9_]+)[\'"]?[)\]]?`)

	codeExt := []string{".go", ".py", ".js", ".ts", ".cpp", ".c", ".h", ".rs"}

	filepath.WalkDir(es.RootDirectory, func(path string, d fs.DirEntry, err error) error {
		if err != nil || d.IsDir() {
			return nil
		}

		ext := filepath.Ext(path)
		isCode := false
		for _, e := range codeExt {
			if ext == e {
				isCode = true
				break
			}
		}
		if !isCode {
			return nil
		}

		content, err := os.ReadFile(path)
		if err != nil {
			return nil
		}

		matches := envPattern.FindAllStringSubmatch(string(content), -1)
		if len(matches) > 0 {
			var found []string
			seen := make(map[string]bool)
			for _, m := range matches {
				if !seen[m[1]] {
					found = append(found, m[1])
					seen[m[1]] = true
				}
			}

			relPath, err := filepath.Rel(es.RootDirectory, path)
			if err != nil {
				relPath = path
			}
			result[relPath] = found
		}

		return nil
	})

	return result
}

// GenerateReport creates full environment report
func (es *EnvScanner) GenerateReport(outputFile string) (*EnvReport, error) {
	fmt.Println("\n" + strings.Repeat("=", 60))
	fmt.Println("ENVIRONMENT SECRETS PATH FINDER")
	fmt.Println(strings.Repeat("=", 60))
	fmt.Printf("\nTarget directory: %s\n", es.RootDirectory)

	envFiles, err := es.ScanDirectory()
	if err != nil {
		return nil, err
	}

	envVarsByFile := es.ScanForEnvVariables()

	allVars := make(map[string]bool)
	for _, vars := range envVarsByFile {
		for _, v := range vars {
			allVars[v] = true
		}
	}

	var uniqueVars []string
	for k := range allVars {
		uniqueVars = append(uniqueVars, k)
	}

	fmt.Printf("\n%s\n", strings.Repeat("=", 60))
	fmt.Println("SUMMARY")
	fmt.Println(strings.Repeat("=", 60))
	fmt.Printf("Environment files found: %d\n", len(envFiles))
	fmt.Printf("Code files using environment variables: %d\n", len(envVarsByFile))
	fmt.Printf("Unique environment variables: %d\n", len(uniqueVars))

	if len(uniqueVars) > 0 {
		fmt.Println("\nCommon environment variables found:")
		for _, v := range uniqueVars {
			fmt.Printf("  - %s\n", v)
		}
	}

	report := &EnvReport{
		ScanDirectory:         es.RootDirectory,
		ScanTimestamp:         time.Now().Format(time.RFC3339),
		EnvironmentFiles:      envFiles,
		CodeFilesUsingEnvVars: envVarsByFile,
		AllEnvironmentVars:    uniqueVars,
		Summary: map[string]int{
			"env_files_count":   len(envFiles),
			"code_files_count":  len(envVarsByFile),
			"unique_vars_count": len(uniqueVars),
		},
	}

	if outputFile != "" {
		data, err := json.MarshalIndent(report, "", "  ")
		if err != nil {
			return report, err
		}

		err = os.WriteFile(outputFile, data, 0644)
		if err == nil {
			fmt.Printf("\n📄 JSON report saved to: %s\n", outputFile)
		}
	}

	fmt.Printf("\n%s\n", strings.Repeat("=", 60))
	fmt.Println("SCAN COMPLETE")
	fmt.Println(strings.Repeat("=", 60))

	return report, nil
}

func main() {
	directory := flag.String("directory", ".", "Directory to scan (default: current directory)")
	outputFile := flag.String("output", "", "Output file for JSON report")
	githubFlag := flag.Bool("github", false, "Scan C:\\GitHub directory (if it exists)")

	flag.Parse()

	scanDir := *directory

	if *githubFlag {
		githubDir := "C:\\GitHub"
		if _, err := os.Stat(githubDir); !os.IsNotExist(err) {
			scanDir = githubDir
		} else {
			fmt.Println("C:\\GitHub directory not found. Using current directory.")
		}
	}

	scanner := NewEnvScanner(scanDir)
	_, err := scanner.GenerateReport(*outputFile)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Error: %v\n", err)
		os.Exit(1)
	}
}