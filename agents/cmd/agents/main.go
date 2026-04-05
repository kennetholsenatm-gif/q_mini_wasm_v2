package main

import (
	"fmt"
	"github.com/kennetholsenatm-gif/q_mini_wasm_v2/agents/pkg"
)

func main() {
	fmt.Println("q_mini_wasm_v2 Agent System")
	fmt.Println("Go Base Agent implementation successfully loaded")

	// Test base agent initialization
	config := &pkg.AgentConfig{
		Name:        "TestAgent",
		Description: "Test agent implementation",
		RateLimitRPM: 15,
	}

	agent := pkg.NewBaseAgent(config)
	
	if err := agent.Initialize(); err != nil {
		fmt.Printf("Failed to initialize agent: %v\n", err)
		return
	}

	fmt.Printf("Agent %s initialized successfully\n", agent.Name())
	fmt.Printf("Uptime: %.2f seconds\n", agent.Uptime())

	// Test ergonomics linter
	linter := pkg.NewCognitiveErgonomicsLinter()
	fmt.Println("Cognitive Ergonomics Linter loaded")

	_ = agent.Shutdown()
	fmt.Println("Agent system ready")
}