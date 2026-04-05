package main

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"time"
)

// AnalysisAgent performs code and performance analysis
type AnalysisAgent struct {
	Name           string
	SupportedLangs []string
	AnalysisCache  map[string]*CodeAnalysis
}

// CodeAnalysis represents analysis results for a code file
type CodeAnalysis struct {
	FilePath          string            `json:"file_path"`
	Language          string            `json:"language"`
	LinesOfCode       int               `json:"lines_of_code"`
	Functions         []FunctionInfo    `json:"functions"`
	Complexity        ComplexityMetrics `json:"complexity"`
	Patterns          []Pattern         `json:"patterns"`
	Issues            []Issue           `json:"issues"`
	PerformanceHints  []string          `json:"performance_hints"`
	Timestamp         time.Time         `json:"timestamp"`
}

// FunctionInfo represents information about a function
type FunctionInfo struct {
	Name       string `json:"name"`
	StartLine  int    `json:"start_line"`
	EndLine    int    `json:"end_line"`
	Parameters int    `json:"parameters"`
	LOC        int    `json:"loc"`
}

// ComplexityMetrics represents code complexity metrics
type ComplexityMetrics struct {
	Cyclomatic       float64 `json:"cyclomatic"`
	Halstead         float64 `json:"halstead"`
	Maintainability  float64 `json:"maintainability"`
	TechnicalDebt    float64 `json:"technical_debt_hours"`
}

// Pattern represents a detected code pattern
type Pattern struct {
	Type        string `json:"type"`
	Name        string `json:"name"`
	Confidence  float64 `json:"confidence"`
	Description string `json:"description"`
	Line        int    `json:"line"`
}

// Issue represents a detected code issue
type Issue struct {
	Severity   string `json:"severity"`
	Category   string `json:"category"`
	Message    string `json:"message"`
	Line       int    `json:"line"`
	Suggestion string `json:"suggestion"`
}

// NewAnalysisAgent creates a new AnalysisAgent
func NewAnalysisAgent() *AnalysisAgent {
	return &AnalysisAgent{
		Name: "AnalysisAgent",
		SupportedLangs: []string{"C++", "Go", "R", "Python"},
		AnalysisCache:  make(map[string]*CodeAnalysis),
	}
}

// AnalyzeFile analyzes a single code file
func (a *AnalysisAgent) AnalyzeFile(filePath string) (*CodeAnalysis, error) {
	content, err := os.ReadFile(filePath)
	if err != nil {
		return nil, fmt.Errorf("failed to read file: %w", err)
	}
	return a.AnalyzeContent(filePath, string(content))
}

// AnalyzeContent analyzes code content
func (a *AnalysisAgent) AnalyzeContent(filePath string, content string) (*CodeAnalysis, error) {
	lang := a.detectLanguage(filePath)
	lines := strings.Split(content, "\n")
	
	analysis := &CodeAnalysis{
		FilePath:         filePath,
		Language:         lang,
		LinesOfCode:      len(lines),
		Functions:        []FunctionInfo{},
		Complexity:       ComplexityMetrics{},
		Patterns:         []Pattern{},
		Issues:           []Issue{},
		PerformanceHints: []string{},
		Timestamp:        time.Now(),
	}
	
	// Analyze based on language
	switch lang {
	case "C++":
		a.analyzeCPP(analysis, content, lines)
	case "Go":
		a.analyzeGo(analysis, content, lines)
	case "R":
		a.analyzeR(analysis, content, lines)
	}
	
	// Calculate complexity metrics
	a.calculateComplexity(analysis)
	
	// Cache result
	a.AnalysisCache[filePath] = analysis
	
	return analysis, nil
}

// detectLanguage detects programming language from file extension
func (a *AnalysisAgent) detectLanguage(filePath string) string {
	ext := strings.ToLower(filepath.Ext(filePath))
	langMap := map[string]string{
		".cpp": "C++", ".cc": "C++", ".cxx": "C++", ".h": "C++", ".hpp": "C++",
		".go": "Go",
		".r": "R", ".R": "R",
		".py": "Python",
	}
	if lang, ok := langMap[ext]; ok {
		return lang
	}
	return "unknown"
}

// analyzeCPP performs C++ specific analysis
func (a *AnalysisAgent) analyzeCPP(analysis *CodeAnalysis, content string, lines []string) {
	// Detect functions
	for i, line := range lines {
		trimmed := strings.TrimSpace(line)
		if strings.Contains(trimmed, "(") && strings.Contains(trimmed, ")") && 
		   (strings.Contains(trimmed, "void") || strings.Contains(trimmed, "int") || 
		    strings.Contains(trimmed, "double") || strings.Contains(trimmed, "auto") ||
		    strings.Contains(trimmed, "class")) {
			// Simple function detection
			if !strings.HasPrefix(trimmed, "//") && !strings.HasPrefix(trimmed, "/*") {
				analysis.Functions = append(analysis.Functions, FunctionInfo{
					Name:      trimmed[:min(len(trimmed), 50)],
					StartLine: i + 1,
				})
			}
		}
		
		// Detect patterns
		if strings.Contains(trimmed, "for") || strings.Contains(trimmed, "while") {
			analysis.Patterns = append(analysis.Patterns, Pattern{
				Type: "loop", Name: "Loop detected", Confidence: 0.8,
				Description: "Loop construct found", Line: i + 1,
			})
		}
		
		// Detect potential issues
		if strings.Contains(trimmed, "new ") && !strings.Contains(trimmed, "delete") {
			analysis.Issues = append(analysis.Issues, Issue{
				Severity: "warning", Category: "memory",
				Message: "Potential memory leak - allocation without delete",
				Line: i + 1, Suggestion: "Consider using smart pointers",
			})
		}
	}
	
	// Performance hints
	if strings.Contains(content, "std::vector") && strings.Contains(content, "push_back") {
		analysis.PerformanceHints = append(analysis.PerformanceHints, 
			"Consider using reserve() for vectors with known size")
	}
}

// analyzeGo performs Go specific analysis
func (a *AnalysisAgent) analyzeGo(analysis *CodeAnalysis, content string, lines []string) {
	for i, line := range lines {
		trimmed := strings.TrimSpace(line)
		if strings.HasPrefix(trimmed, "func ") {
			analysis.Functions = append(analysis.Functions, FunctionInfo{
				Name:      trimmed[5:min(len(trimmed), 55)],
				StartLine: i + 1,
			})
		}
		
		if strings.Contains(trimmed, "for") {
			analysis.Patterns = append(analysis.Patterns, Pattern{
				Type: "loop", Name: "Loop detected", Confidence: 0.9,
				Description: "Go for loop found", Line: i + 1,
			})
		}
		
		if strings.Contains(trimmed, "err != nil") {
			analysis.Patterns = append(analysis.Patterns, Pattern{
				Type: "error_handling", Name: "Error handling", Confidence: 0.95,
				Description: "Standard Go error handling", Line: i + 1,
			})
		}
	}
}

// analyzeR performs R specific analysis
func (a *AnalysisAgent) analyzeR(analysis *CodeAnalysis, content string, lines []string) {
	for i, line := range lines {
		trimmed := strings.TrimSpace(line)
		if strings.HasPrefix(trimmed, "<-") || strings.Contains(trimmed, "=") {
			if strings.Contains(trimmed, "function(") {
				analysis.Functions = append(analysis.Functions, FunctionInfo{
					Name:      trimmed[:min(len(trimmed), 50)],
					StartLine: i + 1,
				})
			}
		}
		
		if strings.Contains(trimmed, "ggplot") || strings.Contains(trimmed, "plot(") {
			analysis.Patterns = append(analysis.Patterns, Pattern{
				Type: "visualization", Name: "Plotting", Confidence: 0.85,
				Description: "Data visualization detected", Line: i + 1,
			})
		}
	}
}

// calculateComplexity calculates complexity metrics
func (a *AnalysisAgent) calculateComplexity(analysis *CodeAnalysis) {
	// Simplified cyclomatic complexity estimation
	complexity := 1.0 // Base complexity
	for _, pattern := range analysis.Patterns {
		if pattern.Type == "loop" {
			complexity += 1.0
		}
	}
	analysis.Complexity.Cyclomatic = complexity
	
	// Estimate maintainability (simplified)
	analysis.Complexity.Maintainability = 100.0 - (complexity * 5.0)
	if analysis.Complexity.Maintainability < 0 {
		analysis.Complexity.Maintainability = 0
	}
	
	// Technical debt estimation (hours)
	analysis.Complexity.TechnicalDebt = float64(len(analysis.Issues)) * 0.5
}

// AnalyzeDirectory analyzes all code files in a directory
func (a *AnalysisAgent) AnalyzeDirectory(dirPath string) ([]*CodeAnalysis, error) {
	var analyses []*CodeAnalysis
	
	err := filepath.Walk(dirPath, func(path string, info os.FileInfo, err error) error {
		if err != nil {
			return err
		}
		if !info.IsDir() && a.isCodeFile(path) {
			analysis, err := a.AnalyzeFile(path)
			if err != nil {
				fmt.Fprintf(os.Stderr, "Warning: Failed to analyze %s: %v\n", path, err)
				return nil
			}
			analyses = append(analyses, analysis)
		}
		return nil
	})
	
	return analyses, err
}

// isCodeFile checks if a file is a code file
func (a *AnalysisAgent) isCodeFile(path string) bool {
	ext := strings.ToLower(filepath.Ext(path))
	codeExts := map[string]bool{
		".cpp": true, ".cc": true, ".cxx": true, ".h": true, ".hpp": true,
		".go": true, ".r": true, ".R": true, ".py": true,
	}
	return codeExts[ext]
}

// GetAnalysisSummary returns a summary of all analyses
func (a *AnalysisAgent) GetAnalysisSummary() map[string]interface{} {
	totalLOC := 0
	totalFunctions := 0
	totalIssues := 0
	
	for _, analysis := range a.AnalysisCache {
		totalLOC += analysis.LinesOfCode
		totalFunctions += len(analysis.Functions)
		totalIssues += len(analysis.Issues)
	}
	
	return map[string]interface{}{
		"total_files":     len(a.AnalysisCache),
		"total_lines":     totalLOC,
		"total_functions": totalFunctions,
		"total_issues":    totalIssues,
		"languages":       a.SupportedLangs,
	}
}

func main() {
	agent := NewAnalysisAgent()
	
	if len(os.Args) > 1 {
		command := os.Args[1]
		
		switch command {
		case "analyze-file":
			if len(os.Args) < 3 {
				fmt.Println("Usage: analysis-agent analyze-file <file-path>")
				os.Exit(1)
			}
			analysis, err := agent.AnalyzeFile(os.Args[2])
			if err != nil {
				fmt.Fprintf(os.Stderr, "Error: %v\n", err)
				os.Exit(1)
			}
			json.NewEncoder(os.Stdout).Encode(analysis)
			
		case "analyze-dir":
			if len(os.Args) < 3 {
				fmt.Println("Usage: analysis-agent analyze-dir <directory-path>")
				os.Exit(1)
			}
			analyses, err := agent.AnalyzeDirectory(os.Args[2])
			if err != nil {
				fmt.Fprintf(os.Stderr, "Error: %v\n", err)
				os.Exit(1)
			}
			json.NewEncoder(os.Stdout).Encode(analyses)
			
		case "summary":
			summary := agent.GetAnalysisSummary()
			json.NewEncoder(os.Stdout).Encode(summary)
			
		default:
			fmt.Println("Analysis Agent - Code and Performance Analysis")
			fmt.Println("Supported languages:", strings.Join(agent.SupportedLangs, ", "))
			fmt.Println("\nCommands:")
			fmt.Println("  analyze-file <path>  - Analyze a single file")
			fmt.Println("  analyze-dir <path>   - Analyze all code files in directory")
			fmt.Println("  summary              - Get analysis summary")
		}
	} else {
		fmt.Println("Analysis Agent - Code and Performance Analysis")
		fmt.Println("Supported languages:", strings.Join(agent.SupportedLangs, ", "))
		fmt.Println("\nUsage: analysis-agent <command> [args]")
	}
}

func min(a, b int) int {
	if a < b {
		return a
	}
	return b
}