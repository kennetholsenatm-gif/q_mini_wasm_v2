package main

import (
	"encoding/json"
	"fmt"
	"os"
	"os/exec"
	"strings"
	"time"
)

// GitHelper provides git operations for agents
type GitHelper struct {
	RepoPath string
}

// GitStatus represents git repository status
type GitStatus struct {
	Branch        string   `json:"branch"`
	Remote        string   `json:"remote"`
	IsClean       bool     `json:"is_clean"`
	AheadBy       int      `json:"ahead_by"`
	BehindBy      int      `json:"behind_by"`
	ModifiedFiles []string `json:"modified_files"`
	StagedFiles   []string `json:"staged_files"`
	UntrackedFiles []string `json:"untracked_files"`
}

// GitCommit represents a git commit
type GitCommit struct {
	Hash      string    `json:"hash"`
	ShortHash string    `json:"short_hash"`
	Author    string    `json:"author"`
	Email     string    `json:"email"`
	Date      time.Time `json:"date"`
	Message   string    `json:"message"`
	Parents   []string  `json:"parents"`
}

// GitDiff represents a git diff
type GitDiff struct {
	FromCommit string     `json:"from_commit"`
	ToCommit   string     `json:"to_commit"`
	Files      []FileDiff `json:"files"`
	Stats      DiffStats  `json:"stats"`
}

// FileDiff represents a file diff
type FileDiff struct {
	FileName    string `json:"file_name"`
	Additions   int    `json:"additions"`
	Deletions   int    `json:"deletions"`
	IsNewFile   bool   `json:"is_new_file"`
	IsDeleted   bool   `json:"is_deleted"`
	IsRenamed   bool   `json:"is_renamed"`
	Patch       string `json:"patch,omitempty"`
}

// DiffStats represents diff statistics
type DiffStats struct {
	TotalFiles    int `json:"total_files"`
	TotalAdditions int `json:"total_additions"`
	TotalDeletions int `json:"total_deletions"`
}

// NewGitHelper creates a new GitHelper
func NewGitHelper(repoPath string) *GitHelper {
	if repoPath == "" {
		repoPath = "."
	}
	return &GitHelper{RepoPath: repoPath}
}

// GetStatus returns the current git status
func (g *GitHelper) GetStatus() (*GitStatus, error) {
	status := &GitStatus{}

	// Get current branch
	branch, err := g.runGit("rev-parse", "--abbrev-ref", "HEAD")
	if err != nil {
		return nil, fmt.Errorf("failed to get branch: %w", err)
	}
	status.Branch = strings.TrimSpace(branch)

	// Get remote tracking branch
	remote, err := g.runGit("rev-parse", "--abbrev-ref", "--symbolic-full-name", "@{u}")
	if err == nil {
		status.Remote = strings.TrimSpace(remote)
	}

	// Get ahead/behind count
	if status.Remote != "" {
		aheadBehind, err := g.runGit("rev-list", "--count", "--left-right", fmt.Sprintf("%s...%s", status.Remote, status.Branch))
		if err == nil {
			parts := strings.Fields(strings.TrimSpace(aheadBehind))
			if len(parts) >= 2 {
				fmt.Sscanf(parts[0], "%d", &status.BehindBy)
				fmt.Sscanf(parts[1], "%d", &status.AheadBy)
			}
		}
	}

	// Get status output
	statusOutput, err := g.runGit("status", "--porcelain")
	if err != nil {
		return nil, fmt.Errorf("failed to get status: %w", err)
	}

	lines := strings.Split(strings.TrimSpace(statusOutput), "\n")
	for _, line := range lines {
		if len(line) < 4 {
			continue
		}
		fileName := strings.TrimSpace(line[3:])
		statusCode := line[:2]

		switch {
		case strings.HasPrefix(statusCode, "M"):
			status.ModifiedFiles = append(status.ModifiedFiles, fileName)
		case strings.HasPrefix(statusCode, "A") || strings.HasPrefix(statusCode, "C"):
			status.StagedFiles = append(status.StagedFiles, fileName)
		case strings.HasPrefix(statusCode, "??"):
			status.UntrackedFiles = append(status.UntrackedFiles, fileName)
		}
	}

	status.IsClean = len(status.ModifiedFiles) == 0 && len(status.StagedFiles) == 0 && len(status.UntrackedFiles) == 0

	return status, nil
}

// GetLog returns recent commits
func (g *GitHelper) GetLog(limit int) ([]GitCommit, error) {
	if limit <= 0 {
		limit = 10
	}

	output, err := g.runGit("log", 
		fmt.Sprintf("-%d", limit),
		"--pretty=format:%H|%h|%an|%ae|%ai|%s|%P",
		"--no-decorate")
	if err != nil {
		return nil, fmt.Errorf("failed to get log: %w", err)
	}

	var commits []GitCommit
	lines := strings.Split(strings.TrimSpace(output), "\n")
	for _, line := range lines {
		parts := strings.Split(line, "|")
		if len(parts) < 6 {
			continue
		}

		date, _ := time.Parse("2006-01-02 15:04:05 -0700", parts[4])

		commit := GitCommit{
			Hash:      parts[0],
			ShortHash: parts[1],
			Author:    parts[2],
			Email:     parts[3],
			Date:      date,
			Message:   parts[5],
		}

		if len(parts) > 6 && parts[6] != "" {
			commit.Parents = strings.Fields(parts[6])
		}

		commits = append(commits, commit)
	}

	return commits, nil
}

// GetDiff returns the diff between two commits
func (g *GitHelper) GetDiff(fromRef, toRef string) (*GitDiff, error) {
	if fromRef == "" {
		fromRef = "HEAD~1"
	}
	if toRef == "" {
		toRef = "HEAD"
	}

	diff := &GitDiff{
		FromCommit: fromRef,
		ToCommit:   toRef,
		Files:      []FileDiff{},
	}

	// Get diff stats
	statsOutput, err := g.runGit("diff", "--stat", fromRef, toRef)
	if err != nil {
		return nil, fmt.Errorf("failed to get diff stats: %w", err)
	}

	lines := strings.Split(strings.TrimSpace(statsOutput), "\n")
	for _, line := range lines {
		if strings.Contains(line, "file changed") || strings.Contains(line, "files changed") {
			// Parse summary line
			parts := strings.Split(line, ",")
			for _, part := range parts {
				part = strings.TrimSpace(part)
				if strings.Contains(part, "insertion") {
					fmt.Sscanf(part, "%d insertion", &diff.Stats.TotalAdditions)
				} else if strings.Contains(part, "deletion") {
					fmt.Sscanf(part, "%d deletion", &diff.Stats.TotalDeletions)
				}
			}
		} else if strings.Contains(line, "|") {
			parts := strings.Split(line, "|")
			if len(parts) >= 2 {
				fileDiff := FileDiff{FileName: strings.TrimSpace(parts[0])}
				if len(parts) >= 3 {
					fmt.Sscanf(parts[1], "%d", &fileDiff.Additions)
					fmt.Sscanf(parts[2], "%d", &fileDiff.Deletions)
				}
				diff.Files = append(diff.Files, fileDiff)
				diff.Stats.TotalFiles++
			}
		}
	}

	return diff, nil
}

// Commit creates a commit with the given message
func (g *GitHelper) Commit(message string, files []string) (string, error) {
	// Stage files if specified
	if len(files) > 0 {
		for _, file := range files {
			_, err := g.runGit("add", file)
			if err != nil {
				return "", fmt.Errorf("failed to stage %s: %w", file, err)
			}
		}
	}

	// Create commit
	_, err := g.runGit("commit", "-m", message)
	if err != nil {
		return "", fmt.Errorf("failed to commit: %w", err)
	}

	// Get commit hash
	hash, err := g.runGit("rev-parse", "HEAD")
	if err != nil {
		return "", fmt.Errorf("failed to get commit hash: %w", err)
	}

	return strings.TrimSpace(hash), nil
}

// Push pushes changes to remote
func (g *GitHelper) Push(branch string) error {
	if branch == "" {
		branch = "main"
	}

	_, err := g.runGit("push", "origin", branch)
	if err != nil {
		return fmt.Errorf("failed to push: %w", err)
	}

	return nil
}

// Pull pulls changes from remote
func (g *GitHelper) Pull(branch string) error {
	if branch == "" {
		branch = "main"
	}

	_, err := g.runGit("pull", "origin", branch)
	if err != nil {
		return fmt.Errorf("failed to pull: %w", err)
	}

	return nil
}

// CreateBranch creates a new branch
func (g *GitHelper) CreateBranch(name string) error {
	_, err := g.runGit("checkout", "-b", name)
	if err != nil {
		return fmt.Errorf("failed to create branch: %w", err)
	}

	return nil
}

// SwitchBranch switches to a branch
func (g *GitHelper) SwitchBranch(name string) error {
	_, err := g.runGit("checkout", name)
	if err != nil {
		return fmt.Errorf("failed to switch branch: %w", err)
	}

	return nil
}

// MergeBranch merges a branch into current branch
func (g *GitHelper) MergeBranch(branch string) error {
	_, err := g.runGit("merge", branch)
	if err != nil {
		return fmt.Errorf("failed to merge: %w", err)
	}

	return nil
}

// runGit executes a git command and returns the output
func (g *GitHelper) runGit(args ...string) (string, error) {
	cmd := exec.Command("git", args...)
	cmd.Dir = g.RepoPath
	output, err := cmd.CombinedOutput()
	if err != nil {
		return "", fmt.Errorf("git %s: %s: %w", strings.Join(args, " "), string(output), err)
	}
	return string(output), nil
}

// GetBranches returns list of branches
func (g *GitHelper) GetBranches() ([]string, error) {
	output, err := g.runGit("branch", "-a")
	if err != nil {
		return nil, fmt.Errorf("failed to get branches: %w", err)
	}

	var branches []string
	lines := strings.Split(strings.TrimSpace(output), "\n")
	for _, line := range lines {
		branch := strings.TrimSpace(strings.TrimPrefix(line, "* "))
		branch = strings.TrimPrefix(branch, "remotes/")
		if branch != "" {
			branches = append(branches, branch)
		}
	}

	return branches, nil
}

func main() {
	helper := NewGitHelper(".")

	if len(os.Args) > 1 {
		command := os.Args[1]

		switch command {
		case "status":
			status, err := helper.GetStatus()
			if err != nil {
				fmt.Fprintf(os.Stderr, "Error: %v\n", err)
				os.Exit(1)
			}
			json.NewEncoder(os.Stdout).Encode(status)

		case "log":
			limit := 10
			if len(os.Args) > 2 {
				fmt.Sscanf(os.Args[2], "%d", &limit)
			}
			commits, err := helper.GetLog(limit)
			if err != nil {
				fmt.Fprintf(os.Stderr, "Error: %v\n", err)
				os.Exit(1)
			}
			json.NewEncoder(os.Stdout).Encode(commits)

		case "diff":
			fromRef := ""
			toRef := ""
			if len(os.Args) > 2 {
				fromRef = os.Args[2]
			}
			if len(os.Args) > 3 {
				toRef = os.Args[3]
			}
			diff, err := helper.GetDiff(fromRef, toRef)
			if err != nil {
				fmt.Fprintf(os.Stderr, "Error: %v\n", err)
				os.Exit(1)
			}
			json.NewEncoder(os.Stdout).Encode(diff)

		case "commit":
			if len(os.Args) < 3 {
				fmt.Println("Usage: git-helper commit <message> [files...]")
				os.Exit(1)
			}
			message := os.Args[2]
			var files []string
			if len(os.Args) > 3 {
				files = os.Args[3:]
			}
			hash, err := helper.Commit(message, files)
			if err != nil {
				fmt.Fprintf(os.Stderr, "Error: %v\n", err)
				os.Exit(1)
			}
			fmt.Printf("Committed: %s\n", hash)

		case "push":
			branch := "main"
			if len(os.Args) > 2 {
				branch = os.Args[2]
			}
			if err := helper.Push(branch); err != nil {
				fmt.Fprintf(os.Stderr, "Error: %v\n", err)
				os.Exit(1)
			}
			fmt.Printf("Pushed to %s\n", branch)

		case "pull":
			branch := "main"
			if len(os.Args) > 2 {
				branch = os.Args[2]
			}
			if err := helper.Pull(branch); err != nil {
				fmt.Fprintf(os.Stderr, "Error: %v\n", err)
				os.Exit(1)
			}
			fmt.Printf("Pulled from %s\n", branch)

		case "branches":
			branches, err := helper.GetBranches()
			if err != nil {
				fmt.Fprintf(os.Stderr, "Error: %v\n", err)
				os.Exit(1)
			}
			json.NewEncoder(os.Stdout).Encode(branches)

		default:
			fmt.Println("Git Helper - Git Operations for Agents")
			fmt.Println("\nCommands:")
			fmt.Println("  status              - Show repository status")
			fmt.Println("  log [limit]         - Show recent commits")
			fmt.Println("  diff [from] [to]    - Show diff between commits")
			fmt.Println("  commit <msg> [files] - Create commit")
			fmt.Println("  push [branch]       - Push to remote")
			fmt.Println("  pull [branch]       - Pull from remote")
			fmt.Println("  branches            - List all branches")
		}
	} else {
		fmt.Println("Git Helper - Git Operations for Agents")
		fmt.Println("\nUsage: git-helper <command> [args]")
	}
}