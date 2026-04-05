package main

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"time"

	"github.com/spf13/cobra"
	"github.com/jedib0t/go-pretty/v6/table"
	"github.com/jedib0t/go-pretty/v6/text"

	"qminiwasm/agents/pkg"
)

var rootCmd = &cobra.Command{
	Use:   "qminiwasm",
	Short: "Auto-Improvement Agent System CLI",
	Long:  `Command line interface for managing the qminiwasm auto-improvement agent system.`,
}

var (
	configFlag  string
	verboseFlag bool
	asyncFlag   bool
	outputFlag  string
)

func init() {
	rootCmd.PersistentFlags().StringVarP(&configFlag, "config", "c", "", "Path to configuration file")
	rootCmd.PersistentFlags().BoolVarP(&verboseFlag, "verbose", "v", false, "Enable verbose output")

	rootCmd.AddCommand(statusCmd)
	rootCmd.AddCommand(runCycleCmd)
	rootCmd.AddCommand(continuousCmd)
	rootCmd.AddCommand(testLLMCmd)
	rootCmd.AddCommand(kanbanReviewCmd)
	rootCmd.AddCommand(generateConfigCmd)

	runCycleCmd.Flags().BoolVarP(&asyncFlag, "async", "a", false, "Run asynchronously")
	kanbanReviewCmd.Flags().StringP("action", "a", "scan", "Action to perform (scan, fix, report)")
	kanbanReviewCmd.Flags().StringP("card-id", "c", "", "Specific card ID to fix")
	generateConfigCmd.Flags().StringVarP(&outputFlag, "output", "o", "agents/config.json", "Output file path")
}

var statusCmd = &cobra.Command{
	Use:   "status",
	Short: "Show current system status",
	Run:   runStatus,
}

var runCycleCmd = &cobra.Command{
	Use:   "run-cycle",
	Short: "Run a single improvement cycle",
	Run:   runCycle,
}

var continuousCmd = &cobra.Command{
	Use:   "continuous",
	Short: "Run continuous improvement cycles",
	Run:   runContinuous,
}

var testLLMCmd = &cobra.Command{
	Use:   "test-llm",
	Short: "Test Gemini LLM integration",
	Run:   runTestLLM,
}

var kanbanReviewCmd = &cobra.Command{
	Use:   "kanban-review",
	Short: "Fix Kanban boards stuck in Review",
	Run:   runKanbanReview,
}

var generateConfigCmd = &cobra.Command{
	Use:   "generate-config",
	Short: "Generate default configuration file",
	Run:   runGenerateConfig,
}

func runStatus(cmd *cobra.Command, args []string) {
	cycle := pkg.NewImprovementCycle(configFlag)
	statusData := cycle.GetStatus()

	fmt.Println("\n" + text.Bold.Sprint(text.FgBlue.Render("=== Auto-Improvement System Status ===")))
	fmt.Println()

	t := table.NewWriter()
	t.SetOutputMirror(os.Stdout)
	t.SetTitle("System Status")
	t.AppendHeader(table.Row{"Property", "Value"})
	t.AppendRows([]table.Row{
		{"Running", fmt.Sprintf("%t", statusData["running"])},
		{"Total Cycles", fmt.Sprintf("%v", statusData["total_cycles"])},
		{"Successful Cycles", fmt.Sprintf("%v", statusData["successful_cycles"])},
		{"Agents Initialized", fmt.Sprintf("%v", statusData["agents_initialized"])},
	})

	if verboseFlag {
		if limits, ok := statusData["llm_rate_limits"].(map[string]interface{}); ok {
			data, _ := json.MarshalIndent(limits, "", "  ")
			t.AppendRow(table.Row{"LLM Rate Limits", string(data)})
		}
	}

	t.Render()
}

func runCycle(cmd *cobra.Command, args []string) {
	fmt.Println("\n" + text.Bold.Sprint(text.FgGreen.Render("=== Starting Improvement Cycle ===")))
	fmt.Println()

	cycle := pkg.NewImprovementCycle(configFlag)
	if err := cycle.Initialize(); err != nil {
		fmt.Printf("\n❌ Error: %s\n", err)
		os.Exit(1)
	}

	fmt.Println("⏳ Running cycle...")
	result, err := cycle.RunCycle()
	if err != nil {
		fmt.Printf("\n❌ Error: %s\n", err)
		os.Exit(1)
	}

	phaseText := text.FgGreen.Render("completed")
	if result.Phase != "completed" {
		phaseText = text.FgRed.Render(result.Phase)
	}

	fmt.Printf("\n✅ Cycle Completed: %s\n", result.CycleID)
	fmt.Println()

	t := table.NewWriter()
	t.SetOutputMirror(os.Stdout)
	t.SetTitle("Cycle Results")
	t.AppendHeader(table.Row{"Metric", "Value"})
	t.AppendRows([]table.Row{
		{"Phase", phaseText},
		{"Patterns Identified", fmt.Sprintf("%d", result.PatternsIdentified)},
		{"Improvements Planned", fmt.Sprintf("%d", result.ImprovementsPlanned)},
		{"Improvements Implemented", fmt.Sprintf("%d", result.ImprovementsImplemented)},
		{"Improvements Validated", fmt.Sprintf("%d", result.ImprovementsValidated)},
		{"Success Rate", fmt.Sprintf("%.1f%%", result.SuccessRate*100)},
	})

	if len(result.Errors) > 0 {
		t.AppendRow(table.Row{"Errors", fmt.Sprintf("%v", result.Errors)})
	}

	t.Render()

	cycle.Shutdown()
}

func runContinuous(cmd *cobra.Command, args []string) {
	fmt.Println("\n" + text.Bold.Sprint(text.FgYellow.Render("=== Starting Continuous Improvement ===")))
	fmt.Println()

	cycle := pkg.NewImprovementCycle(configFlag)
	if err := cycle.Initialize(); err != nil {
		fmt.Printf("\n❌ Error: %s\n", err)
		os.Exit(1)
	}

	cycleCount := 0
	for {
		cycleCount++
		fmt.Printf("\n📋 Running cycle %d...\n", cycleCount)

		result, err := cycle.RunCycle()
		if err != nil {
			fmt.Printf("❌ Cycle %d error: %s\n", cycleCount, err)
			fmt.Println("⏱ Waiting 1 minute on error...")
			time.Sleep(60 * time.Second)
			continue
		}

		if result.Phase == "completed" {
			fmt.Printf("✅ Cycle %d completed successfully\n", cycleCount)
		} else {
			fmt.Printf("❌ Cycle %d failed: %s\n", cycleCount, result.Phase)
		}

		fmt.Println("⏱ Waiting 1 hour before next cycle...")
		time.Sleep(3600 * time.Second)
	}
}

func runTestLLM(cmd *cobra.Command, args []string) {
	prompt, _ := cmd.Flags().GetString("prompt")
	if prompt == "" {
		fmt.Println("❌ Error: prompt is required")
		os.Exit(1)
	}

	fmt.Println("\n" + text.Bold.Sprint(text.FgBlue.Render("=== Testing Gemini LLM ===")))
	fmt.Println()

	configPath := configFlag
	if configPath == "" {
		configPath = "agents/config.json"
	}

	var configData map[string]interface{}
	if _, err := os.Stat(configPath); err == nil {
		data, err := os.ReadFile(configPath)
		if err == nil {
			json.Unmarshal(data, &configData)
		}
	}

	geminiConfig := pkg.NewGeminiConfig(configData)
	llm := pkg.NewGeminiService(geminiConfig)

	fmt.Println("⏳ Initializing LLM...")
	if err := llm.Initialize(); err != nil {
		fmt.Printf("❌ Error: %s\n", err)
		os.Exit(1)
	}

	fmt.Printf("📤 Sending prompt: %.50s...\n", prompt)
	response, err := llm.Complete(prompt)
	if err != nil {
		fmt.Printf("❌ Error: %s\n", err)
		os.Exit(1)
	}

	fmt.Println("\n📥 LLM Response:")
	fmt.Println(text.FgGreen.Render(response))

	rateStatus := llm.GetRateLimitStatus()
	fmt.Printf("\n📊 Rate Limit Status:\n")
	fmt.Printf("  RPM: %d/%d\n", rateStatus["rpm_used"], rateStatus["rpm_limit"])
	fmt.Printf("  TPM: %d/%d\n", rateStatus["tpm_used"], rateStatus["tpm_limit"])
	fmt.Printf("  Cache Size: %d\n", rateStatus["cache_size"])

	llm.Shutdown()
}

func runKanbanReview(cmd *cobra.Command, args []string) {
	action, _ := cmd.Flags().GetString("action")
	cardID, _ := cmd.Flags().GetString("card-id")

	fmt.Printf("\n=== Kanban Review Fix Agent - %s ===\n\n", text.Bold.Sprint(text.FgBlue.Render(action)))

	agentConfig := &pkg.AgentConfig{
		Name:          "KanbanReviewFixAgent",
		Description:   "Monitors and fixes Kanban boards stuck in Review",
		SystemPrompt:  "You are an agent that monitors Kanban boards and fixes stuck review cards.",
		Tools:         []string{"scan_review", "fix_card", "generate_report"},
	}

	kanbanConfigPath := configFlag
	if kanbanConfigPath == "" {
		kanbanConfigPath = "config/kanban-config.json"
	}

	agent := pkg.NewKanbanReviewFixAgent(agentConfig, kanbanConfigPath)
	if err := agent.Initialize(); err != nil {
		fmt.Printf("❌ Error: %s\n", err)
		os.Exit(1)
	}

	task := map[string]interface{}{
		"action": action,
	}
	if cardID != "" {
		task["card_id"] = cardID
	}

	fmt.Printf("⏳ Executing %s...\n", action)
	result, err := agent.ExecuteTask(task)
	if err != nil {
		fmt.Printf("❌ Error: %s\n", err)
		os.Exit(1)
	}

	if result.Success {
		fmt.Println("\n✅ Action Completed Successfully")
		fmt.Println()

		t := table.NewWriter()
		t.SetOutputMirror(os.Stdout)
		t.SetTitle("Results")
		t.AppendHeader(table.Row{"Metric", "Value"})

		switch action {
		case "scan":
			t.AppendRows([]table.Row{
				{"Total Review Cards", fmt.Sprintf("%v", result.Data["total_review_cards"])},
				{"Stuck Cards", fmt.Sprintf("%v", result.Data["stuck_cards"])},
				{"Cards with Errors", fmt.Sprintf("%v", result.Data["error_cards"])},
				{"Cards with Issues", fmt.Sprintf("%v", result.Data["issue_cards"])},
			})
		case "fix":
			t.AppendRows([]table.Row{
				{"Total Stuck", fmt.Sprintf("%v", result.Data["total_stuck"])},
				{"Fixed", fmt.Sprintf("%v", result.Data["fixed_count"])},
			})
		case "report":
			if summary, ok := result.Data["summary"].(map[string]interface{}); ok {
				t.AppendRows([]table.Row{
					{"Total Cards", fmt.Sprintf("%v", summary["total_review_cards"])},
					{"Stuck Cards", fmt.Sprintf("%v", summary["stuck_cards"])},
					{"Avg Stuck Hours", fmt.Sprintf("%.1f", summary["avg_stuck_hours"])},
				})
			}
		}

		t.Render()

		if verboseFlag {
			if recs, ok := result.Data["recommendations"].([]interface{}); ok && len(recs) > 0 {
				fmt.Println("\n💡 Recommendations:")
				for _, rec := range recs {
					fmt.Printf("  • %v\n", rec)
				}
			}
		}
	} else {
		fmt.Println("\n❌ Action Failed")
		for _, err := range result.Errors {
			fmt.Printf("  • %s\n", err)
		}
	}

	agent.Shutdown()
}

func runGenerateConfig(cmd *cobra.Command, args []string) {
	defaultConfig := map[string]interface{}{
		"project":     "q_mini_wasm_v2",
		"version":     "1.0.0",
		"description": "Auto-improvement cycle configuration",
		"gemini": map[string]interface{}{
			"api_key_env": "GEMINI_API_KEY",
			"model":       "gemini-3-flash-preview",
			"temperature": 0.7,
			"max_tokens":  8192,
			"rate_limits": map[string]interface{}{
				"rpm": 15,
				"tpm": 1000000,
				"rpd": 1500,
			},
			"batch_size": 10,
			"cache_ttl":  3600,
		},
		"agents": map[string]interface{}{
			"research_agent": map[string]interface{}{
				"name":                          "ResearchAgent",
				"description":                   "Analyzes patterns and gathers information",
				"system_prompt":                 "You are a research agent focused on identifying patterns and improvement opportunities.",
				"tools":                         []string{"search_web", "analyze_patterns", "collect_metrics"},
				"max_iterations":                5,
				"improvement_cycle_frequency":   "daily",
			},
			"analysis_agent": map[string]interface{}{
				"name":                          "AnalysisAgent",
				"description":                   "Evaluates performance and identifies bottlenecks",
				"system_prompt":                 "You are an analysis agent that evaluates performance and identifies areas for improvement.",
				"tools":                         []string{"evaluate_performance", "identify_bottlenecks", "suggest_improvements"},
				"max_iterations":                3,
				"improvement_cycle_frequency":   "weekly",
			},
			"code_agent": map[string]interface{}{
				"name":                          "CodeAgent",
				"description":                   "Generates and refines code improvements",
				"system_prompt":                 "You are a code generation agent that implements improvements.",
				"tools":                         []string{"generate_code", "refactor_code", "validate_code"},
				"max_iterations":                10,
				"improvement_cycle_frequency":   "on_demand",
			},
			"test_agent": map[string]interface{}{
				"name":                          "TestAgent",
				"description":                   "Validates improvements through testing",
				"system_prompt":                 "You are a testing agent that validates improvements.",
				"tools":                         []string{"run_tests", "generate_tests", "measure_performance"},
				"max_iterations":                7,
				"improvement_cycle_frequency":   "per_improvement",
			},
		},
	}

	outputPath := outputFlag
	if err := os.MkdirAll(filepath.Dir(outputPath), 0755); err != nil {
		fmt.Printf("❌ Error: %s\n", err)
		os.Exit(1)
	}

	data, err := json.MarshalIndent(defaultConfig, "", "  ")
	if err != nil {
		fmt.Printf("❌ Error: %s\n", err)
		os.Exit(1)
	}

	if err := os.WriteFile(outputPath, data, 0644); err != nil {
		fmt.Printf("❌ Error: %s\n", err)
		os.Exit(1)
	}

	fmt.Printf("\n✅ Configuration generated: %s\n", outputPath)
}

func main() {
	if err := rootCmd.Execute(); err != nil {
		fmt.Println(err)
		os.Exit(1)
	}
}