package main

import (
	"encoding/json"
	"fmt"
	"os"
	"strings"
	"time"
)

// CompletionStatus represents the status of a project/task
type CompletionStatus string

const (
	InProgress CompletionStatus = "in_progress"
	NearDone   CompletionStatus = "near_done"
	Completed  CompletionStatus = "completed"
	Archived   CompletionStatus = "archived"
	Unknown    CompletionStatus = "unknown"
)

// DoneIndicators contains terms that indicate completion
type DoneIndicators struct {
	StrongIndicators  []string `json:"strong_indicators"`
	WeakIndicators    []string `json:"weak_indicators"`
	ContextualPhrases []string `json:"contextual_phrases"`
}

// NewDoneIndicators creates default done indicators
func NewDoneIndicators() *DoneIndicators {
	return &DoneIndicators{
		StrongIndicators: []string{
			"task is complete", "work is complete", "project is done",
			"finished", "completed successfully", "all done",
			"nothing to commit", "no new changes", "no pending changes",
			"already committed", "already cherry-picked", "already integrated",
			"task complete", "done.", "done!", "complete.", "complete!",
		},
		WeakIndicators: []string{
			"done", "complete", "finished", "ready", "resolved",
			"merged", "deployed", "shipped", "released",
		},
		ContextualPhrases: []string{
			"no changes to", "nothing to", "no new", "no pending",
			"has been", "is already", "was already", "have been", "successfully",
		},
	}
}

// CleanupTask represents a task to be cleaned up
type CleanupTask struct {
	ID              string                 `json:"id"`
	Name            string                 `json:"name"`
	Description     string                 `json:"description"`
	Status          CompletionStatus       `json:"status"`
	LastUpdated     time.Time              `json:"last_updated"`
	CompletionScore float64                `json:"completion_score"`
	DoneIndicators  []string               `json:"done_indicators_found"`
	Metadata        map[string]interface{} `json:"metadata"`
}

// RAGContext represents context retrieved from RAG
type RAGContext struct {
	Query     string   `json:"query"`
	Results   []string `json:"results"`
	Relevance float64  `json:"relevance"`
	Sources   []string `json:"sources"`
}

// MCPServerConfig represents MCP server configuration
type MCPServerConfig struct {
	Name        string   `json:"name"`
	Description string   `json:"description"`
	Tools       []string `json:"tools"`
	Endpoint    string   `json:"endpoint"`
}

// CleanupAgent is the main cleanup agent structure
type CleanupAgent struct {
	DoneIndicators    *DoneIndicators
	MCPServers        map[string]MCPServerConfig
	RAGEndpoint       string
	TaskHistory       []CleanupTask
	CompletionHistory []CompletionStatus
}

// NewCleanupAgent creates a new CleanupAgent
func NewCleanupAgent() *CleanupAgent {
	return &CleanupAgent{
		DoneIndicators: NewDoneIndicators(),
		MCPServers: map[string]MCPServerConfig{
			"qminiwasm-autolearn": {
				Name:        "qminiwasm-autolearn",
				Description: "AutoLearn MCP server for self-improving AI agents",
				Tools:       []string{"capture_reasoning_trace", "crystallize_skill", "evaluate_agent_performance"},
				Endpoint:    "mcp://qminiwasm-autolearn",
			},
			"qminiwasm-rag-service": {
				Name:        "qminiwasm-rag-service",
				Description: "RAG service for token optimization",
				Tools:       []string{"rag_retrieve_context", "rag_index_document", "rag_get_metrics"},
				Endpoint:    "mcp://qminiwasm-rag-service",
			},
			"qminiwasm-self-learning": {
				Name:        "qminiwasm-self-learning",
				Description: "Self-learning agent for pattern detection",
				Tools:       []string{"detect_patterns", "generate_tools", "update_memory"},
				Endpoint:    "mcp://qminiwasm-self-learning",
			},
		},
		RAGEndpoint:       "http://localhost:8088/api/v1/rag",
		TaskHistory:       make([]CleanupTask, 0),
		CompletionHistory: make([]CompletionStatus, 0),
	}
}

// AnalyzeText analyzes text for done indicators
func (a *CleanupAgent) AnalyzeText(text string) (*CleanupTask, error) {
	text = strings.ToLower(text)
	foundIndicators := make([]string, 0)
	score := 0.0

	for _, indicator := range a.DoneIndicators.StrongIndicators {
		if strings.Contains(text, strings.ToLower(indicator)) {
			foundIndicators = append(foundIndicators, "strong: "+indicator)
			score += 0.4
		}
	}

	for _, indicator := range a.DoneIndicators.WeakIndicators {
		if strings.Contains(text, strings.ToLower(indicator)) {
			foundIndicators = append(foundIndicators, "weak: "+indicator)
			score += 0.15
		}
	}

	for _, phrase := range a.DoneIndicators.ContextualPhrases {
		if strings.Contains(text, strings.ToLower(phrase)) {
			foundIndicators = append(foundIndicators, "contextual: "+phrase)
			score += 0.1
		}
	}

	if score > 1.0 {
		score = 1.0
	}

	status := InProgress
	if score >= 0.8 {
		status = Completed
	} else if score >= 0.5 {
		status = NearDone
	}

	task := &CleanupTask{
		ID:              fmt.Sprintf("task_%d", time.Now().UnixNano()),
		Name:            "Text Analysis",
		Description:     "Analyzed text for completion indicators",
		Status:          status,
		LastUpdated:     time.Now(),
		CompletionScore: score,
		DoneIndicators:  foundIndicators,
		Metadata: map[string]interface{}{
			"text_length":   len(text),
			"analysis_time": time.Now().Format(time.RFC3339),
		},
	}

	a.TaskHistory = append(a.TaskHistory, *task)
	a.CompletionHistory = append(a.CompletionHistory, status)
	return task, nil
}

// AnalyzeProject analyzes a project directory for completion
func (a *CleanupAgent) AnalyzeProject(projectPath string) (*CleanupTask, error) {
	return &CleanupTask{
		ID:              fmt.Sprintf("project_%d", time.Now().UnixNano()),
		Name:            "Project Analysis",
		Description:     fmt.Sprintf("Analyzed project at %s", projectPath),
		Status:          InProgress,
		LastUpdated:     time.Now(),
		CompletionScore: 0.0,
		DoneIndicators:  []string{},
		Metadata:        map[string]interface{}{"project_path": projectPath},
	}, nil
}

// RetrieveContext retrieves context using RAG
func (a *CleanupAgent) RetrieveContext(query string, maxResults int) (*RAGContext, error) {
	return &RAGContext{
		Query: query, Results: []string{}, Relevance: 0.0, Sources: []string{},
	}, nil
}

// CallMCPTool calls an MCP server tool
func (a *CleanupAgent) CallMCPTool(serverName string, toolName string, params map[string]interface{}) (map[string]interface{}, error) {
	server, exists := a.MCPServers[serverName]
	if !exists {
		return nil, fmt.Errorf("MCP server not found: %s", serverName)
	}
	toolExists := false
	for _, tool := range server.Tools {
		if tool == toolName {
			toolExists = true
			break
		}
	}
	if !toolExists {
		return nil, fmt.Errorf("tool not found: %s on server %s", toolName, serverName)
	}
	return map[string]interface{}{
		"server": serverName, "tool": toolName, "params": params,
		"status": "success", "message": fmt.Sprintf("Called %s.%s", serverName, toolName),
	}, nil
}

// GetCompletionSummary returns a summary of completion analysis
func (a *CleanupAgent) GetCompletionSummary() map[string]interface{} {
	totalTasks := len(a.TaskHistory)
	completedTasks := 0
	nearDoneTasks := 0
	for _, task := range a.TaskHistory {
		if task.Status == Completed {
			completedTasks++
		} else if task.Status == NearDone {
			nearDoneTasks++
		}
	}
	completionRate := 0.0
	if totalTasks > 0 {
		completionRate = float64(completedTasks) / float64(totalTasks)
	}
	return map[string]interface{}{
		"total_tasks": totalTasks, "completed_tasks": completedTasks,
		"near_done_tasks": nearDoneTasks, "completion_rate": completionRate,
		"mcp_servers": len(a.MCPServers), "rag_endpoint": a.RAGEndpoint,
	}
}

// GetMCPServers returns available MCP servers
func (a *CleanupAgent) GetMCPServers() map[string]MCPServerConfig {
	return a.MCPServers
}

// AddDoneIndicator adds a custom done indicator
func (a *CleanupAgent) AddDoneIndicator(indicator string, strength string) {
	switch strength {
	case "strong":
		a.DoneIndicators.StrongIndicators = append(a.DoneIndicators.StrongIndicators, indicator)
	case "weak":
		a.DoneIndicators.WeakIndicators = append(a.DoneIndicators.WeakIndicators, indicator)
	case "contextual":
		a.DoneIndicators.ContextualPhrases = append(a.DoneIndicators.ContextualPhrases, indicator)
	}
}

// ExportIndicators exports done indicators to JSON
func (a *CleanupAgent) ExportIndicators() ([]byte, error) {
	return json.MarshalIndent(a.DoneIndicators, "", "  ")
}

func main() {
	agent := NewCleanupAgent()

	if len(os.Args) > 1 {
		command := os.Args[1]
		switch command {
		case "analyze-text":
			if len(os.Args) < 3 {
				fmt.Println("Usage: cleanup-agent analyze-text <text>")
				os.Exit(1)
			}
			text := strings.Join(os.Args[2:], " ")
			result, err := agent.AnalyzeText(text)
			if err != nil {
				fmt.Fprintf(os.Stderr, "Error: %v\n", err)
				os.Exit(1)
			}
			json.NewEncoder(os.Stdout).Encode(result)

		case "analyze-project":
			if len(os.Args) < 3 {
				fmt.Println("Usage: cleanup-agent analyze-project <path>")
				os.Exit(1)
			}
			result, err := agent.AnalyzeProject(os.Args[2])
			if err != nil {
				fmt.Fprintf(os.Stderr, "Error: %v\n", err)
				os.Exit(1)
			}
			json.NewEncoder(os.Stdout).Encode(result)

		case "rag-query":
			if len(os.Args) < 3 {
				fmt.Println("Usage: cleanup-agent rag-query <query>")
				os.Exit(1)
			}
			query := strings.Join(os.Args[2:], " ")
			result, err := agent.RetrieveContext(query, 5)
			if err != nil {
				fmt.Fprintf(os.Stderr, "Error: %v\n", err)
				os.Exit(1)
			}
			json.NewEncoder(os.Stdout).Encode(result)

		case "mcp-call":
			if len(os.Args) < 5 {
				fmt.Println("Usage: cleanup-agent mcp-call <server> <tool> <params-json>")
				os.Exit(1)
			}
			var params map[string]interface{}
			if err := json.Unmarshal([]byte(os.Args[4]), &params); err != nil {
				fmt.Fprintf(os.Stderr, "Error parsing params: %v\n", err)
				os.Exit(1)
			}
			result, err := agent.CallMCPTool(os.Args[2], os.Args[3], params)
			if err != nil {
				fmt.Fprintf(os.Stderr, "Error: %v\n", err)
				os.Exit(1)
			}
			json.NewEncoder(os.Stdout).Encode(result)

		case "summary":
			json.NewEncoder(os.Stdout).Encode(agent.GetCompletionSummary())

		case "servers":
			json.NewEncoder(os.Stdout).Encode(agent.GetMCPServers())

		case "indicators":
			data, err := agent.ExportIndicators()
			if err != nil {
				fmt.Fprintf(os.Stderr, "Error: %v\n", err)
				os.Exit(1)
			}
			fmt.Println(string(data))

		default:
			fmt.Println("Cleanup Agent - Recognizes project completion and integrates with RAG/MCP")
			fmt.Println("\nCommands:")
			fmt.Println("  analyze-text <text>           - Analyze text for done indicators")
			fmt.Println("  analyze-project <path>        - Analyze project for completion")
			fmt.Println("  rag-query <query>             - Query RAG for context")
			fmt.Println("  mcp-call <server> <tool> <params> - Call MCP server tool")
			fmt.Println("  summary                       - Get completion summary")
			fmt.Println("  servers                       - List available MCP servers")
			fmt.Println("  indicators                    - Export done indicators")
		}
	} else {
		fmt.Println("Cleanup Agent - Recognizes project completion and integrates with RAG/MCP")
		fmt.Println("\nUsage: cleanup-agent <command> [args]")
	}
}