package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"os"

	"github.com/kennetholsenatm-gif/q_mini_wasm_v2/agents"
	"github.com/rs/zerolog/log"
)

func main() {
	lintCmd := flag.NewFlagSet("lint", flag.ExitOnError)
	tomlFile := lintCmd.String("toml", "", "TOML file to lint for cognitive ergonomics")

	if len(os.Args) < 2 {
		fmt.Println("Usage: agentd [lint|run] [options]")
		os.Exit(1)
	}

	switch os.Args[1] {
	case "lint":
		lintCmd.Parse(os.Args[2:])
		if *tomlFile == "" {
			fmt.Println("Error: --toml parameter is required")
			lintCmd.PrintDefaults()
			os.Exit(1)
		}

		content, err := os.ReadFile(*tomlFile)
		if err != nil {
			log.Fatal().Err(err).Str("file", *tomlFile).Msg("Failed to read TOML file")
		}

		linter := agents.NewCognitiveErgonomicsLinter()
		violations := linter.LintToml(string(content))

		fmt.Printf("Cognitive Ergonomics Analysis: %s\n", *tomlFile)
		fmt.Printf("Found %d violations:\n\n", len(violations))

		for i, v := range violations {
			fmt.Printf("%d. [%s] %s\n", i+1, v.Severity, v.Principle)
			fmt.Printf("   Path: %s\n", v.Path)
			fmt.Printf("   Issue: %s\n", v.Description)
			fmt.Printf("   Fix: %s\n\n", v.Recommendation)
		}

		if len(violations) > 0 {
			jsonOutput, _ := json.MarshalIndent(violations, "", "  ")
			os.WriteFile("cognitive_violations.json", jsonOutput, 0644)
			fmt.Println("Report written to cognitive_violations.json")
		}

	default:
		fmt.Printf("Unknown command: %s\n", os.Args[1])
		os.Exit(1)
	}
}