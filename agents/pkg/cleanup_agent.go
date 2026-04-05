package pkg

import (
	"os"
	"path/filepath"
	"regexp"
	"strings"
	"time"
)

// CleanupAgent monitors task completion patterns and performs cleanup operations
type CleanupAgent struct {
	*BaseAgent
	completionCache map[string]interface{}
	cleanupHistory  []map[string]interface{}
	lastCleanup     time.Time

	COMPLETION_PATTERNS []string
	TEMP_FILE_PATTERNS  []string
	MONITOR_DIRS        []string
}

// NewCleanupAgent creates a new CleanupAgent
func NewCleanupAgent(config *AgentConfig) *CleanupAgent {
	return &CleanupAgent{
		BaseAgent:       NewBaseAgent(config),
		completionCache: make(map[string]interface{}),
		cleanupHistory:  make([]map[string]interface{}, 0),
		COMPLETION_PATTERNS: []string{
			`Final Execution Complete`,
			`Task completed successfully`,
			`All tests passed`,
			`Cycle completed`,
			`Deployment successful`,
			`Build successful`,
			`Documentation update complete`,
			`CI/CD pipeline complete`,
		},
		TEMP_FILE_PATTERNS: []string{
			"*.tmp",
			"*.temp",
			"*.log",
			"*.bak",
			"*~",
			"__pycache__",
			"*.pyc",
			".pytest_cache",
			".mypy_cache",
		},
		MONITOR_DIRS: []string{
			"docs",
			"wiki-output",
			"build",
			"dist",
			"agents/memory",
		},
	}
}

// ExecuteTask executes a cleanup task
func (a *CleanupAgent) ExecuteTask(task map[string]interface{}) (*TaskResult, error) {
	taskType, _ := task["type"].(string)
	parameters, _ := task["parameters"].(map[string]interface{})

	if taskType == "" {
		taskType = "unknown"
	}

	a.State = AgentStateRunning

	var result interface{}
	var err error

	switch taskType {
	case "scan_completions":
		result, err = a.scanForCompletions(parameters)
	case "cleanup_temp_files":
		result, err = a.cleanupTempFiles(parameters)
	case "archive_logs":
		result, err = a.archiveCompletedLogs(parameters)
	case "full_cleanup":
		result, err = a.fullCleanup(parameters)
	case "check_pattern":
		result, err = a.checkCompletionPattern(parameters)
	default:
		a.State = AgentStateIdle
		return &TaskResult{
			Success: false,
			Errors:  []string{"Unknown task type: " + taskType},
		}, nil
	}

	if err != nil {
		a.State = AgentStateError
		return &TaskResult{
			Success: false,
			Errors:  []string{err.Error()},
		}, err
	}

	a.State = AgentStateIdle

	return &TaskResult{
		Success: true,
		Data:    result,
	}, nil
}

// scanForCompletions scans for completion patterns in logs and outputs
func (a *CleanupAgent) scanForCompletions(parameters map[string]interface{}) (map[string]interface{}, error) {
	repoRoot, _ := parameters["repo_root"].(string)
	if repoRoot == "" {
		repoRoot = "."
	}

	scanPaths, ok := parameters["scan_paths"].([]string)
	if !ok || len(scanPaths) == 0 {
		scanPaths = []string{"logs", "output", "build"}
	}

	completionsFound := make([]map[string]interface{}, 0)

	for _, scanPath := range scanPaths {
		path := filepath.Join(repoRoot, scanPath)
		if _, err := os.Stat(path); err == nil {
			for _, pattern := range a.COMPLETION_PATTERNS {
				completions := a.findPatternInFiles(path, pattern)
				completionsFound = append(completionsFound, completions...)
			}
		}
	}

	a.completionCache = map[string]interface{}{
		"last_scan":        time.Now().Format(time.RFC3339),
		"completions_found": completionsFound,
		"patterns_checked": len(a.COMPLETION_PATTERNS),
	}

	return map[string]interface{}{
		"scan_complete":      true,
		"completions_found":  len(completionsFound),
		"patterns":           completionsFound[:min(10, len(completionsFound))],
	}, nil
}

// findPatternInFiles finds pattern matches in files within a directory
func (a *CleanupAgent) findPatternInFiles(directory string, pattern string) []map[string]interface{} {
	matches := make([]map[string]interface{}, 0)

	re, err := regexp.Compile("(?i)" + pattern)
	if err != nil {
		return matches
	}

	err = filepath.Walk(directory, func(path string, info os.FileInfo, err error) error {
		if err != nil {
			return err
		}

		if !info.Mode().IsRegular() {
			return nil
		}

		ext := strings.ToLower(filepath.Ext(path))
		if ext != ".txt" && ext != ".log" && ext != ".md" && ext != ".json" {
			return nil
		}

		content, err := os.ReadFile(path)
		if err != nil {
			return nil
		}

		if re.Match(content) {
			relPath, _ := filepath.Rel(filepath.Dir(directory), path)
			matches = append(matches, map[string]interface{}{
				"file":      relPath,
				"pattern":   pattern,
				"timestamp": time.Now().Format(time.RFC3339),
			})
		}

		return nil
	})

	return matches
}

// cleanupTempFiles cleans up temporary files
func (a *CleanupAgent) cleanupTempFiles(parameters map[string]interface{}) (map[string]interface{}, error) {
	repoRoot, _ := parameters["repo_root"].(string)
	if repoRoot == "" {
		repoRoot = "."
	}

	dryRun, ok := parameters["dry_run"].(bool)
	if !ok {
		dryRun = false
	}

	filesCleaned := make([]string, 0)
	bytesFreed := int64(0)

	for _, pattern := range a.TEMP_FILE_PATTERNS {
		err := filepath.Walk(repoRoot, func(path string, info os.FileInfo, err error) error {
			if err != nil {
				return err
			}

			if strings.Contains(path, ".git") {
				if info.IsDir() {
					return filepath.SkipDir
				}
				return nil
			}

			matched, err := filepath.Match(pattern, filepath.Base(path))
			if err != nil || !matched {
				return nil
			}

			if info.Mode().IsRegular() {
				fileSize := info.Size()
				if !dryRun {
					os.Remove(path)
				}
				relPath, _ := filepath.Rel(repoRoot, path)
				filesCleaned = append(filesCleaned, relPath)
				bytesFreed += fileSize
			}

			return nil
		})

		if err != nil {
			continue
		}
	}

	a.lastCleanup = time.Now()

	return map[string]interface{}{
		"cleanup_complete": true,
		"files_cleaned":    len(filesCleaned),
		"bytes_freed":      bytesFreed,
	}, nil
}

// archiveCompletedLogs archives completed logs
func (a *CleanupAgent) archiveCompletedLogs(parameters map[string]interface{}) (map[string]interface{}, error) {
	return map[string]interface{}{
		"archive_complete": true,
		"logs_archived":    0,
	}, nil
}

// fullCleanup performs full cleanup cycle
func (a *CleanupAgent) fullCleanup(parameters map[string]interface{}) (map[string]interface{}, error) {
	results := make(map[string]interface{})

	scanResult, err := a.scanForCompletions(parameters)
	results["scan"] = scanResult
	if err != nil {
		return nil, err
	}

	cleanupResult, err := a.cleanupTempFiles(parameters)
	results["cleanup"] = cleanupResult
	if err != nil {
		return nil, err
	}

	return map[string]interface{}{
		"full_cleanup_complete": true,
		"results":              results,
	}, nil
}

// checkCompletionPattern checks text for completion pattern
func (a *CleanupAgent) checkCompletionPattern(parameters map[string]interface{}) (map[string]interface{}, error) {
	text, _ := parameters["text"].(string)
	pattern, _ := parameters["pattern"].(string)

	if pattern == "" {
		pattern = "Final Execution Complete"
	}

	re, err := regexp.Compile("(?i)" + pattern)
	if err != nil {
		return nil, err
	}

	match := re.FindString(text)

	return map[string]interface{}{
		"pattern_found": match != "",
		"pattern":       pattern,
		"matched_text":  match,
	}, nil
}

// AnalyzePerformance analyzes the cleanup agent performance
func (a *CleanupAgent) AnalyzePerformance() (map[string]interface{}, error) {
	performance := map[string]interface{}{
		"tasks_completed": a.TaskCount,
	}

	if !a.lastCleanup.IsZero() {
		performance["last_cleanup"] = a.lastCleanup.Format(time.RFC3339)
	} else {
		performance["last_cleanup"] = nil
	}

	return performance, nil
}

// SuggestImprovements suggests improvements for the cleanup agent
func (a *CleanupAgent) SuggestImprovements() ([]map[string]interface{}, error) {
	return make([]map[string]interface{}, 0), nil
}