package main

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"time"

	"github.com/fatih/color"
	"github.com/rodaine/table"
	"github.com/spf13/cobra"
	agents "qminiwasm/agents/pkg"
)

var (
	configFlag  string
	verboseFlag bool
	asyncFlag   bool
)

func main() {
	rootCmd := &cobra.Command{
		Use:   "qminiwasm",
		Short: "Auto-Improvement Agent System CLI",
		Long:  `Command line interface for managing the q_mini_wasm_v2 auto-improvement cycle system.`,
	}

	// Status command
	statusCmd := &cobra.Command{
		Use:   "status",
		Short: "Show current system status",
		Run:   runStatus,
	}
	statusCmd.Flags().StringVarP(&configFlag, "config", "c", "", "Path to configuration file")
	statusCmd.Flags().BoolVarP(&verboseFlag, "verbose", "v", false, "Enable verbose output")

	// Run cycle command
	runCycleCmd := &cobra.Command{
		Use:   "run-cycle",
		Short: "Run a single improvement cycle",
		Run:   runCycle,
	}
	runCycleCmd.Flags().StringVarP(&configFlag, "config", "c", "", "Path to configuration file")
	runCycleCmd.Flags().BoolVarP(&asyncFlag, "async", "a", false, "Run asynchronously")

	// Continuous command
	continuousCmd := &cobra.Command{
		Use:   "continuous",
		Short: "Run continuous improvement cycles",
		Run:   runContinuous,
	}
	continuousCmd.Flags().StringVarP(&configFlag, "config", "c", "", "Path to configuration file")

	// Generate config command
	generateConfigCmd := &cobra.Command{
		Use:   "generate-config",
		Short: "Generate default configuration file",
		Run:   runGenerateConfig,
	}
	generateConfigCmd.Flags().StringP("output", "o", "agents/config.json", "Output file path")

	rootCmd.AddCommand(statusCmd)
	rootCmd.AddCommand(runCycleCmd)
	rootCmd.AddCommand(continuousCmd)
	rootCmd.AddCommand(generateConfigCmd)

	if err := rootCmd.Execute(); err != nil {
		color.Red("Error: %v", err)
		os.Exit(1)
	}
}

func runStatus(cmd *cobra.Command, args []string) {
	color.Blue("Auto-Improvement System Status")
	fmt.Println()

	cycle, err := agents.NewImprovementCycle(configFlag, nil)
	if err != nil {
		color.Red("Failed to create cycle: %v", err)
		os.Exit(1)
	}

	if err := cycle.Initialize(); err != nil {
		color.Red("Failed to initialize: %v", err)
		os.Exit(1)
	}

	status := cycle.GetStatus()

	tbl := table.New("Property", "Value")
	tbl.WithHeaderFormatter(color.New(color.FgCyan, color.Bold).SprintfFunc())
	tbl.WithFirstColumnFormatter(color.New(color.FgCyan).SprintfFunc())

	tbl.AddRow("Running", status["running"])
	tbl.AddRow("Total Cycles", status["total_cycles"])
	tbl.AddRow("Successful Cycles", status["successful_cycles"])
	tbl.AddRow("Agents Initialized", status["agents_initialized"])

	if verboseFlag {
		if llmStatus, ok := status["llm_rate_limits"]; ok {
			data, _ := json.MarshalIndent(llmStatus, "", "  ")
			tbl.AddRow("LLM Rate Limits", string(data))
		}
	}

	tbl.Print()
}

func runCycle(cmd *cobra.Command, args []string) {
	color.Green("Starting Improvement Cycle")
	fmt.Println()

	cycle, err := agents.NewImprovementCycle(configFlag, nil)
	if err != nil {
		color.Red("Failed to create cycle: %v", err)
		os.Exit(1)
	}

	if err := cycle.Initialize(); err != nil {
		color.Red("Failed to initialize: %v", err)
		os.Exit(1)
	}

	color.Yellow("Running cycle...")
	result, err := cycle.RunCycle()
	if err != nil {
		color.Red("Cycle failed: %v", err)
	}

	fmt.Println()
	if result.Phase == agents.CyclePhaseCompleted {
		color.Green("Cycle Completed: %s", result.CycleID)
	} else {
		color.Red("Cycle Failed: %s", result.CycleID)
	}
	fmt.Println()

	tbl := table.New("Metric", "Value")
	tbl.WithHeaderFormatter(color.New(color.FgCyan, color.Bold).SprintfFunc())
	tbl.WithFirstColumnFormatter(color.New(color.FgCyan).SprintfFunc())

	tbl.AddRow("Phase", result.Phase)
	tbl.AddRow("Patterns Identified", result.PatternsIdentified)
	tbl.AddRow("Improvements Planned", result.ImprovementsPlanned)
	tbl.AddRow("Improvements Implemented", result.ImprovementsImplemented)
	tbl.AddRow("Improvements Validated", result.ImprovementsValidated)
	tbl.AddRow("Success Rate", fmt.Sprintf("%.1f%%", result.SuccessRate*100))

	if len(result.Errors) > 0 {
		for _, e := range result.Errors {
			tbl.AddRow("Error", e)
		}
	}

	tbl.Print()

	if err := cycle.Shutdown(); err != nil {
		color.Yellow("Warning during shutdown: %v", err)
	}
}

func runContinuous(cmd *cobra.Command, args []string) {
	color.Yellow("Starting Continuous Improvement")
	fmt.Println()

	cycle, err := agents.NewImprovementCycle(configFlag, nil)
	if err != nil {
		color.Red("Failed to create cycle: %v", err)
		os.Exit(1)
	}

	if err := cycle.Initialize(); err != nil {
		color.Red("Failed to initialize: %v", err)
		os.Exit(1)
	}

	cycleCount := 0
	for {
		cycleCount++
		color.Cyan("\nRunning cycle %d...", cycleCount)

		result, err := cycle.RunCycle()
		if err != nil {
			color.Red("Cycle %d error: %v", cycleCount, err)
			color.Yellow("Waiting 1 minute before next cycle...")
			time.Sleep(1 * time.Minute)
			continue
		}

		if result.Phase == agents.CyclePhaseCompleted {
			color.Green("Cycle %d completed successfully", cycleCount)
		} else {
			color.Red("Cycle %d failed: %s", cycleCount, result.Phase)
		}

		color.Yellow("Waiting 1 hour before next cycle...")
		time.Sleep(1 * time.Hour)
	}
}

func runGenerateConfig(cmd *cobra.Command, args []string) {
	outputPath, _ := cmd.Flags().GetString("output")

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
				"name":         "ResearchAgent",
				"description":  "Analyzes patterns and gathers information",
				"max_iterations": 5,
			},
			"analysis_agent": map[string]interface{}{
				"name":         "AnalysisAgent",
				"description":  "Evaluates performance",
				"max_iterations": 3,
			},
			"code_agent": map[string]interface{}{
				"name":         "CodeAgent",
				"description":  "Generates code improvements",
				"max_iterations": 10,
			},
			"test_agent": map[string]interface{}{
				"name":         "TestAgent",
				"description":  "Validates improvements",
				"max_iterations": 7,
			},
		},
		"improvement_cycle": map[string]interface{}{
			"max_concurrent_improvements": 3,
			"improvement_cooldown_days":   7,
			"rollback_threshold":          0.95,
		},
	}

	data, err := json.MarshalIndent(defaultConfig, "", "  ")
	if err != nil {
		color.Red("Failed to marshal config: %v", err)
		os.Exit(1)
	}

	if err := os.MkdirAll(filepath.Dir(outputPath), 0755); err != nil {
		color.Red("Failed to create directory: %v", err)
		os.Exit(1)
	}

	if err := os.WriteFile(outputPath, data, 0644); err != nil {
		color.Red("Failed to write file: %v", err)
		os.Exit(1)
	}

	color.Green("Configuration generated: %s", outputPath)
}