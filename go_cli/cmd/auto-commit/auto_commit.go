package main

import (
	"bytes"
	"encoding/json"
	"flag"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"strconv"
	"strings"
	"time"
)

var commitTypes = []string{"feat", "fix", "docs", "style", "refactor", "test", "chore", "quantum"}

// CommitResult contains result of auto commit operation
type CommitResult struct {
	Success       bool              `json:"success"`
	CommitMessage string            `json:"commit_message"`
	StagedFiles   []string          `json:"staged_files"`
	Stats         map[string]int    `json:"stats"`
	Error         string            `json:"error,omitempty"`
	DryRun        bool              `json:"dry_run"`
	PRCreated     bool              `json:"pr_created"`
	PRURL         string            `json:"pr_url,omitempty"`
}

// AutoCommit is the main auto commit handler
type AutoCommit struct {
	RepoPath string
}

// NewAutoCommit creates a new AutoCommit instance
func NewAutoCommit(repoPath string) *AutoCommit {
	if repoPath == "" {
		repoPath = "."
	}
	absPath, _ := filepath.Abs(repoPath)
	return &AutoCommit{
		RepoPath: absPath,
	}
}

// RunGit executes a git command
func (ac *AutoCommit) RunGit(args ...string) (bool, string) {
	cmd := exec.Command("git", args...)
	cmd.Dir = ac.RepoPath

	var out bytes.Buffer
	var stderr bytes.Buffer
	cmd.Stdout = &out
	cmd.Stderr = &stderr

	err := cmd.Run()
	if err != nil {
		return false, stderr.String()
	}

	return true, strings.TrimSpace(out.String())
}

// GetCurrentBranch returns current git branch name
func (ac *AutoCommit) GetCurrentBranch() string {
	success, output := ac.RunGit("rev-parse", "--abbrev-ref", "HEAD")
	if !success {
		return "unknown"
	}
	return output
}

// GetStagedFiles returns list of staged files
func (ac *AutoCommit) GetStagedFiles() []string {
	success, output := ac.RunGit("diff", "--cached", "--name-only")
	if !success || output == "" {
		return []string{}
	}

	files := strings.Split(output, "\n")
	var result []string
	for _, f := range files {
		f = strings.TrimSpace(f)
		if f != "" {
			result = append(result, f)
		}
	}
	return result
}

// GetStagedChanges returns statistics about staged changes
func (ac *AutoCommit) GetStagedChanges() map[string]int {
	stats := map[string]int{
		"files": 0, "insertions": 0, "deletions": 0,
	}

	success, output := ac.RunGit("diff", "--cached", "--stat")
	if !success || output == "" {
		return stats
	}

	lines := strings.Split(output, "\n")
	if len(lines) == 0 {
		return stats
	}

	summary := lines[len(lines)-1]
	parts := strings.Split(summary, ",")

	for _, part := range parts {
		part = strings.TrimSpace(part)
		fields := strings.Fields(part)
		if len(fields) < 2 {
			continue
		}

		val, err := strconv.Atoi(fields[0])
		if err != nil {
			continue
		}

		if strings.Contains(part, "file") {
			stats["files"] = val
		} else if strings.Contains(part, "insertion") {
			stats["insertions"] = val
		} else if strings.Contains(part, "deletion") {
			stats["deletions"] = val
		}
	}

	return stats
}

// AnalyzeFileChanges categorizes file changes by type
func (ac *AutoCommit) AnalyzeFileChanges(stagedFiles []string) map[string][]string {
	categories := map[string][]string{
		"source":  {}, "test": {}, "docs": {}, "config": {}, "build": {}, "other": {},
	}

	testPatterns := []string{"test", "tests", "_test.", "spec"}
	docPatterns := []string{"doc", "docs", "readme", "changelog"}
	configExt := []string{".json", ".yaml", ".yml", ".toml", ".ini", ".cfg"}
	buildPatterns := []string{"makefile", "cmake", "dockerfile", "docker-compose", ".github/workflows"}
	sourceExt := []string{".py", ".cpp", ".c", ".h", ".hpp", ".go", ".rs", ".js", ".ts"}

	for _, file := range stagedFiles {
		fileLower := strings.ToLower(file)
		ext := filepath.Ext(file)

		classified := false

		// Test files
		for _, p := range testPatterns {
			if strings.Contains(fileLower, p) {
				categories["test"] = append(categories["test"], file)
				classified = true
				break
			}
		}
		if classified {
			continue
		}

		// Docs
		for _, p := range docPatterns {
			if strings.Contains(fileLower, p) {
				categories["docs"] = append(categories["docs"], file)
				classified = true
				break
			}
		}
		if classified {
			continue
		}

		// Config
		for _, e := range configExt {
			if ext == e {
				categories["config"] = append(categories["config"], file)
				classified = true
				break
			}
		}
		if classified {
			continue
		}

		// Build
		for _, p := range buildPatterns {
			if strings.Contains(fileLower, p) {
				categories["build"] = append(categories["build"], file)
				classified = true
				break
			}
		}
		if classified {
			continue
		}

		// Source
		for _, e := range sourceExt {
			if ext == e {
				categories["source"] = append(categories["source"], file)
				classified = true
				break
			}
		}
		if classified {
			continue
		}

		// Other
		categories["other"] = append(categories["other"], file)
	}

	return categories
}

// DetermineCommitType identifies commit type from changes
func (ac *AutoCommit) DetermineCommitType(categories map[string][]string, branchName string) string {
	// Check branch name first
	switch {
	case strings.HasPrefix(branchName, "feature/"):
		return "feat"
	case strings.HasPrefix(branchName, "fix/"):
		return "fix"
	case strings.HasPrefix(branchName, "docs/"):
		return "docs"
	case strings.HasPrefix(branchName, "refactor/"):
		return "refactor"
	case strings.HasPrefix(branchName, "test/"):
		return "test"
	}

	// Check file categories
	switch {
	case len(categories["test"]) > 0 && len(categories["source"]) == 0:
		return "test"
	case len(categories["docs"]) > 0 && len(categories["source"]) == 0:
		return "docs"
	case len(categories["config"]) > 0 && len(categories["source"]) == 0:
		return "chore"
	case len(categories["source"]) > 0:
		for _, f := range categories["source"] {
			if strings.Contains(strings.ToLower(f), "quantum") {
				return "quantum"
			}
		}
		return "feat"
	}

	return "chore"
}

// GenerateScope creates commit scope from changed files
func (ac *AutoCommit) GenerateScope(categories map[string][]string) string {
	scopes := []string{}

	moduleNames := []string{
		"quantum_core", "dll_bridge", "go_runtime", "sycl_accelerator",
		"runtime_engine", "rag_service", "test_orchestrator", "wui_designer",
	}

	var allFiles []string
	for _, list := range categories {
		allFiles = append(allFiles, list...)
	}

	for _, file := range allFiles {
		parts := strings.Split(file, string(filepath.Separator))
		for _, part := range parts {
			for _, mod := range moduleNames {
				if part == mod {
					found := false
					for _, s := range scopes {
						if s == mod {
							found = true
							break
						}
					}
					if !found {
						scopes = append(scopes, mod)
					}
				}
			}
		}

		if strings.Contains(strings.ToLower(file), "hooks") {
			scopes = append(scopes, "hooks")
		}
		if strings.Contains(strings.ToLower(file), "config") {
			scopes = append(scopes, "config")
		}
	}

	// Limit to maximum 2 scopes
	if len(scopes) > 2 {
		scopes = scopes[:2]
	}

	return strings.Join(scopes, ", ")
}

// GenerateCommitMessage creates properly formatted commit message
func (ac *AutoCommit) GenerateCommitMessage(commitType string, scope string, stats map[string]int, categories map[string][]string) string {
	var fileDesc string
	if stats["files"] == 1 {
		var firstFile string
		if len(categories["source"]) > 0 {
			firstFile = categories["source"][0]
		} else {
			firstFile = categories["other"][0]
		}
		fileDesc = fmt.Sprintf("update %s", filepath.Base(firstFile))
	} else {
		fileDesc = fmt.Sprintf("%d files", stats["files"])
	}

	var message string
	switch commitType {
	case "feat":
		if stats["files"] == 1 && len(categories["source"]) > 0 {
			stem := strings.TrimSuffix(filepath.Base(categories["source"][0]), filepath.Ext(categories["source"][0]))
			message = fmt.Sprintf("add %s functionality", stem)
		} else {
			message = fmt.Sprintf("add %s with %d insertions", fileDesc, stats["insertions"])
		}
	case "fix":
		message = fmt.Sprintf("fix issues in %s", fileDesc)
	case "docs":
		message = fmt.Sprintf("update documentation in %s", fileDesc)
	case "refactor":
		message = fmt.Sprintf("refactor %s", fileDesc)
	case "test":
		message = fmt.Sprintf("add/update tests for %s", fileDesc)
	case "chore":
		message = fmt.Sprintf("update %s", fileDesc)
	case "quantum":
		message = fmt.Sprintf("update quantum operations in %s", fileDesc)
	default:
		message = fmt.Sprintf("update %s", fileDesc)
	}

	if scope != "" {
		return fmt.Sprintf("%s(%s): %s", commitType, scope, message)
	}
	return fmt.Sprintf("%s: %s", commitType, message)
}

// ValidateCommitMessage checks message against required format
func (ac *AutoCommit) ValidateCommitMessage(message string) (bool, string) {
	pattern := `^(feat|fix|docs|style|refactor|test|chore|quantum)(\(\w+\))?: .{1,72}$`
	matched, _ := regexp.MatchString(pattern, message)

	if matched {
		return true, "Commit message format is valid"
	}
	return false, fmt.Sprintf("Commit message does not match required format: %s", pattern)
}

// Commit executes full auto commit workflow
func (ac *AutoCommit) Commit(autoPush bool, createPR bool, dryRun bool) CommitResult {
	result := CommitResult{
		Success:   false,
		DryRun:    dryRun,
		PRCreated: false,
	}

	// Verify git repository
	success, _ := ac.RunGit("status")
	if !success {
		result.Error = "Not in a git repository"
		return result
	}

	// Get current branch
	branchName := ac.GetCurrentBranch()
	if branchName == "unknown" {
		result.Error = "Could not determine current branch"
		return result
	}

	// Get staged files
	stagedFiles := ac.GetStagedFiles()
	if len(stagedFiles) == 0 {
		result.Error = "No staged files to commit"
		return result
	}
	result.StagedFiles = stagedFiles

	// Get change statistics
	stats := ac.GetStagedChanges()
	result.Stats = stats

	// Analyze file changes
	categories := ac.AnalyzeFileChanges(stagedFiles)

	// Determine commit type and scope
	commitType := ac.DetermineCommitType(categories, branchName)
	scope := ac.GenerateScope(categories)

	// Generate commit message
	commitMessage := ac.GenerateCommitMessage(commitType, scope, stats, categories)
	result.CommitMessage = commitMessage

	// Validate commit message
	isValid, _ := ac.ValidateCommitMessage(commitMessage)
	if !isValid {
		result.Error = fmt.Sprintf("Generated commit message is invalid: %s", commitMessage)
		return result
	}

	if dryRun {
		result.Success = true
		return result
	}

	// Create commit
	success, output := ac.RunGit("commit", "-m", commitMessage)
	if !success {
		result.Error = fmt.Sprintf("Failed to create commit: %s", output)
		return result
	}

	// Push if requested
	if autoPush {
		success, output := ac.RunGit("push", "-u", "origin", branchName)
		if !success {
			result.Error = fmt.Sprintf("Failed to push: %s", output)
			return result
		}
	}

	// Create PR if requested
	if createPR && autoPush {
		prBody := fmt.Sprintf(`## Auto-Generated PR

This PR was automatically created by the Kanban Auto-Commit workflow.

**Commit Message:** %s

### Changes
- Auto-committed staged changes
- Generated commit message following project conventions

---
*Generated on %s*
`, commitMessage, time.Now().Format(time.RFC3339))

		cmd := exec.Command("gh", "pr", "create",
			"--title", commitMessage,
			"--body", prBody,
			"--base", "develop",
			"--head", branchName)
		cmd.Dir = ac.RepoPath

		out, err := cmd.Output()
		if err == nil {
			result.PRCreated = true
			result.PRURL = strings.TrimSpace(string(out))
		}
	}

	result.Success = true
	return result
}

func main() {
	repoPath := flag.String("repo-path", ".", "Path to git repository")
	autoPush := flag.Bool("auto-push", false, "Push after committing")
	createPR := flag.Bool("create-pr", false, "Create PR after pushing")
	dryRun := flag.Bool("dry-run", false, "Only generate message, don't commit")
	jsonOutput := flag.Bool("json", false, "Output result as JSON")

	flag.Parse()

	ac := NewAutoCommit(*repoPath)
	result := ac.Commit(*autoPush, *createPR, *dryRun)

	if *jsonOutput {
		data, _ := json.MarshalIndent(result, "", "  ")
		fmt.Println(string(data))
	} else {
		if result.Success {
			if result.DryRun {
				fmt.Printf("Would commit with message: %s\n", result.CommitMessage)
				fmt.Printf("Staged files: %d\n", len(result.StagedFiles))
				fmt.Printf("Stats: +%d/-%d\n", result.Stats["insertions"], result.Stats["deletions"])
			} else {
				fmt.Printf("Successfully committed: %s\n", result.CommitMessage)
				if *autoPush {
					fmt.Println("Changes pushed to remote")
				}
				if result.PRCreated {
					fmt.Printf("PR created: %s\n", result.PRURL)
				}
			}
		} else {
			fmt.Fprintf(os.Stderr, "Error: %s\n", result.Error)
			os.Exit(1)
		}
	}
}