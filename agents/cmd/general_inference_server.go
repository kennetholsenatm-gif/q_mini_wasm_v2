package main

import (
	"bufio"
	"encoding/json"
	"fmt"
	"io/ioutil"
	"log"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
	"time"

	"github.com/pelletier/go-toml/v2"
)

type InferenceRequest struct {
	Model       string  `json:"model"`
	Prompt      string  `json:"prompt"`
	MaxTokens   int     `json:"max_tokens"`
	Temperature float64 `json:"temperature"`
}

type InferenceResponse struct {
	ID      string `json:"id"`
	Object  string `json:"object"`
	Created int64  `json:"created"`
	Model   string `json:"model"`
	Choices []struct {
		Text  string `json:"text"`
		Index int    `json:"index"`
	} `json:"choices"`
	Usage struct {
		PromptTokens     int `json:"prompt_tokens"`
		CompletionTokens int `json:"completion_tokens"`
		TotalTokens      int `json:"total_tokens"`
	} `json:"usage"`
}

type AgentConfig struct {
	AgentType   string   `toml:"agent_type" json:"agent_type"`
	Description string   `toml:"description" json:"description"`
	Temperature float64  `toml:"temperature" json:"temperature"`
	MaxTokens   int      `toml:"max_tokens" json:"max_tokens"`
	RAGEnabled  bool     `toml:"rag_enabled" json:"rag_enabled"`
	RAGSources  []string `toml:"rag_sources" json:"rag_sources"`
	MCPs        []string `toml:"mcps" json:"mcps"`
}

func getAgentConfigPath(agentName string) string {
	wd, _ := os.Getwd()
	configDir := filepath.Join(wd, "config", "agents")
	os.MkdirAll(configDir, 0755)
	return filepath.Join(configDir, fmt.Sprintf("%s.toml", agentName))
}

func loadAgentConfig(agentName string) (AgentConfig, error) {
	path := getAgentConfigPath(agentName)
	var cfg AgentConfig
	
	data, err := ioutil.ReadFile(path)
	if err != nil {
		// Return default config if file doesn't exist
		cfg := AgentConfig{
			AgentType:   agentName,
			Description: fmt.Sprintf("Default %s agent", agentName),
			Temperature: 0.7,
			MaxTokens:   512,
			RAGEnabled:  false,
			RAGSources:  []string{},
			MCPs:        []string{},
		}
		
		switch agentName {
		case "chat":
			cfg.Temperature = 0.9
			cfg.Description = "Conversational AI assistant"
		case "code":
			cfg.Temperature = 0.2
			cfg.MaxTokens = 2048
			cfg.Description = "Expert programmer and software architect"
		case "summarize":
			cfg.Temperature = 0.3
			cfg.Description = "Concise document summarizer"
		case "explain":
			cfg.Temperature = 0.5
			cfg.Description = "Technical concepts explainer"
		case "translate":
			cfg.Temperature = 0.1
			cfg.Description = "Highly accurate translator"
		case "write":
			cfg.Temperature = 0.8
			cfg.MaxTokens = 1024
			cfg.Description = "Creative writer and editor"
		}
		
		return cfg, nil
	}
	
	err = toml.Unmarshal(data, &cfg)
	return cfg, err
}

func saveAgentConfig(agentName string, cfg AgentConfig) error {
	path := getAgentConfigPath(agentName)
	data, err := toml.Marshal(cfg)
	if err != nil {
		return err
	}
	return ioutil.WriteFile(path, data, 0644)
}

func handleAgentConfig(w http.ResponseWriter, r *http.Request) {
	agentName := r.URL.Query().Get("agent")
	if agentName == "" {
		http.Error(w, "Missing agent parameter", http.StatusBadRequest)
		return
	}

	if r.Method == "GET" {
		cfg, err := loadAgentConfig(agentName)
		if err != nil {
			http.Error(w, err.Error(), http.StatusInternalServerError)
			return
		}
		data, err := toml.Marshal(cfg)
		if err != nil {
			http.Error(w, err.Error(), http.StatusInternalServerError)
			return
		}
		w.Header().Set("Content-Type", "application/toml")
		w.Write(data)
	} else if r.Method == "POST" {
		body, err := ioutil.ReadAll(r.Body)
		if err != nil {
			http.Error(w, err.Error(), http.StatusBadRequest)
			return
		}
		
		var cfg AgentConfig
		if err := toml.Unmarshal(body, &cfg); err != nil {
			http.Error(w, "Invalid TOML: "+err.Error(), http.StatusBadRequest)
			return
		}
		
		if err := saveAgentConfig(agentName, cfg); err != nil {
			http.Error(w, err.Error(), http.StatusInternalServerError)
			return
		}
		
		w.WriteHeader(http.StatusOK)
		w.Write([]byte("Configuration saved successfully."))
	} else {
		http.Error(w, "Method not allowed", http.StatusMethodNotAllowed)
	}
}

func handleInference(w http.ResponseWriter, r *http.Request) {
	if r.Method != "POST" {
		http.Error(w, "Method not allowed", http.StatusMethodNotAllowed)
		return
	}

	body, err := ioutil.ReadAll(r.Body)
	if err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}

	var req InferenceRequest
	if err := json.Unmarshal(body, &req); err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}

	// Model parameter maps to the agent capability name
	agentName := req.Model
	if agentName == "" || agentName == "q-mini-wasm-v2-general" {
		agentName = "chat"
	}
	
	cfg, _ := loadAgentConfig(agentName)
	
	// Execute the native C++ inference engine
	wd, _ := os.Getwd()
	cmdPaths := []string{
		filepath.Join(wd, "q_mini_wasm_v2", "build", "Release", "q_mini_wasm_v2_infer.exe"),
		filepath.Join(wd, "q_mini_wasm_v2", "build", "q_mini_wasm_v2_infer.exe"),
		filepath.Join(wd, "..", "q_mini_wasm_v2", "build", "Release", "q_mini_wasm_v2_infer.exe"),
		filepath.Join(wd, "..", "q_mini_wasm_v2", "build", "q_mini_wasm_v2_infer.exe"),
	}

	if runtime.GOOS != "windows" {
		for i, p := range cmdPaths {
			cmdPaths[i] = strings.TrimSuffix(p, ".exe")
		}
	}

	var cmdPath string
	for _, p := range cmdPaths {
		if _, err := os.Stat(p); err == nil {
			cmdPath = p
			break
		}
	}

	var generatedText string
	promptTokens := len(strings.Fields(req.Prompt))
	completionTokens := 0

	var prefixBuilder strings.Builder
	prefixBuilder.WriteString(fmt.Sprintf("[Agent: %s | Temp: %.2f | RAG: %v]\n\n", cfg.AgentType, cfg.Temperature, cfg.RAGEnabled))
	if cfg.RAGEnabled && len(cfg.RAGSources) > 0 {
		prefixBuilder.WriteString(fmt.Sprintf("Using knowledge from: %s\n", strings.Join(cfg.RAGSources, ", ")))
	}
	if len(cfg.MCPs) > 0 {
		prefixBuilder.WriteString(fmt.Sprintf("Active MCP tools: %s\n", strings.Join(cfg.MCPs, ", ")))
	}
	prefixBuilder.WriteString("\n")

	if cmdPath == "" {
		generatedText = prefixBuilder.String() + "Error: Failed to locate q_mini_wasm_v2_infer executable. Please build the project."
	} else {
		cmdArgs := []string{
			"--prompt", req.Prompt,
			"--max-tokens", fmt.Sprintf("%d", cfg.MaxTokens),
			"--temperature", fmt.Sprintf("%f", cfg.Temperature),
			"--agent", cfg.AgentType,
		}
		
		cmd := exec.Command(cmdPath, cmdArgs...)
		outputBytes, err := cmd.Output()
		
		if err != nil {
			generatedText = prefixBuilder.String() + "Error during native inference execution: " + err.Error()
		} else {
			var inferResult struct {
				Text             string `json:"text"`
				PromptTokens     int    `json:"prompt_tokens"`
				CompletionTokens int    `json:"completion_tokens"`
				TotalLatency     float64 `json:"total_latency_ms"`
				Error            string `json:"error"`
			}
			
			// We might have multiple lines, so find the JSON object line
			lines := strings.Split(string(outputBytes), "\n")
			for _, line := range lines {
				if strings.HasPrefix(strings.TrimSpace(line), "{") {
					if err := json.Unmarshal([]byte(line), &inferResult); err == nil {
						if inferResult.Error != "" {
							generatedText = prefixBuilder.String() + "Native Inference Error: " + inferResult.Error
						} else {
							generatedText = prefixBuilder.String() + inferResult.Text
							promptTokens = inferResult.PromptTokens
							completionTokens = inferResult.CompletionTokens
						}
						break
					}
				}
			}
			
			if generatedText == "" {
				// Fallback if parsing failed
				generatedText = prefixBuilder.String() + string(outputBytes)
			}
		}
	}

	resp := InferenceResponse{
		ID:      fmt.Sprintf("cmpl-%d", time.Now().UnixNano()),
		Object:  "text_completion",
		Created: time.Now().Unix(),
		Model:   req.Model,
		Choices: []struct {
			Text  string `json:"text"`
			Index int    `json:"index"`
		}{
			{Text: generatedText, Index: 0},
		},
		Usage: struct {
			PromptTokens     int `json:"prompt_tokens"`
			CompletionTokens int `json:"completion_tokens"`
			TotalTokens      int `json:"total_tokens"`
		}{
			PromptTokens:     promptTokens,
			CompletionTokens: completionTokens,
			TotalTokens:      promptTokens + completionTokens,
		},
	}

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(resp)
}

func handleModels(w http.ResponseWriter, r *http.Request) {
	models := map[string]interface{}{
		"object": "list",
		"data": []map[string]interface{}{
			{
				"id":       "q-mini-wasm-v2-general",
				"object":   "model",
				"created":  time.Now().Unix(),
				"owned_by": "q_mini_wasm_v2",
			},
		},
	}
	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(models)
}

func handleHealth(w http.ResponseWriter, r *http.Request) {
	status := map[string]interface{}{
		"status":    "healthy",
		"model":     "q-mini-wasm-v2-general",
		"timestamp": time.Now().Format(time.RFC3339),
	}
	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(status)
}

func handleTraining(w http.ResponseWriter, r *http.Request) {
	if r.Method != "POST" {
		http.Error(w, "Method not allowed", http.StatusMethodNotAllowed)
		return
	}

	var req struct {
		Epochs             int     `json:"epochs"`
		BatchSize          int     `json:"batch_size"`
		LearningRate       float64 `json:"learning_rate"`
		ContextWindow      int     `json:"context_window"`
		EntanglementTokens int     `json:"entanglement_tokens"`
		DatasetPath        string  `json:"dataset_path"`
		BaseModelPath      string  `json:"base_model_path"`
		OutputPath         string  `json:"output_path"`
		MoEExperts         int     `json:"moe_experts"`
		MoETopK            int     `json:"moe_top_k"`
		SteaneCorrection   bool    `json:"steane_correction"`
		FlashCIM           bool    `json:"flash_cim"`
	}

	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}

	w.Header().Set("Content-Type", "application/json")
	w.Header().Set("Transfer-Encoding", "chunked")
	w.Header().Set("Cache-Control", "no-cache")
	w.Header().Set("Connection", "keep-alive")

	flusher, ok := w.(http.Flusher)
	if !ok {
		http.Error(w, "Streaming unsupported!", http.StatusInternalServerError)
		return
	}

	wd, _ := os.Getwd()
	cmdPaths := []string{
		filepath.Join(wd, "q_mini_wasm_v2", "build", "Release", "q_mini_wasm_v2_trainer.exe"),
		filepath.Join(wd, "q_mini_wasm_v2", "build", "q_mini_wasm_v2_trainer.exe"),
		filepath.Join(wd, "..", "q_mini_wasm_v2", "build", "Release", "q_mini_wasm_v2_trainer.exe"),
		filepath.Join(wd, "..", "q_mini_wasm_v2", "build", "q_mini_wasm_v2_trainer.exe"),
	}

	if runtime.GOOS != "windows" {
		for i, p := range cmdPaths {
			cmdPaths[i] = strings.TrimSuffix(p, ".exe")
		}
	}

	var cmdPath string
	for _, p := range cmdPaths {
		if _, err := os.Stat(p); err == nil {
			cmdPath = p
			break
		}
	}

	if cmdPath == "" {
		errResp := map[string]string{"status": "error", "message": "Failed to locate q_mini_wasm_v2_trainer executable. Please ensure you have built it using CMake."}
		json.NewEncoder(w).Encode(errResp)
		flusher.Flush()
		return
	}
	
	if req.BaseModelPath != "" {
		json.NewEncoder(w).Encode(map[string]interface{}{"status": "init", "message": "Loading base model from " + req.BaseModelPath})
		flusher.Flush()
		time.Sleep(500 * time.Millisecond) // Simulate loading delay
	}

	steaneFlag := "false"
	if req.SteaneCorrection {
		steaneFlag = "true"
	}
	flashCIMFlag := "false"
	if req.FlashCIM {
		flashCIMFlag = "true"
	}

	cmdArgs := []string{
		"--epochs", fmt.Sprintf("%d", req.Epochs),
		"--batch-size", fmt.Sprintf("%d", req.BatchSize),
		"--learning-rate", fmt.Sprintf("%f", req.LearningRate),
		"--context-window", fmt.Sprintf("%d", req.ContextWindow),
		"--entanglement-tokens", fmt.Sprintf("%d", req.EntanglementTokens),
		"--moe-experts", fmt.Sprintf("%d", req.MoEExperts),
		"--moe-top-k", fmt.Sprintf("%d", req.MoETopK),
		"--steane-correction", steaneFlag,
		"--flash-cim", flashCIMFlag,
	}
	
	if req.DatasetPath != "" {
		cmdArgs = append(cmdArgs, "--dataset", req.DatasetPath)
	}
	if req.BaseModelPath != "" {
		cmdArgs = append(cmdArgs, "--base-model", req.BaseModelPath)
	}
	if req.OutputPath != "" {
		cmdArgs = append(cmdArgs, "--output", req.OutputPath)
	}

	cmd := exec.Command(cmdPath, cmdArgs...)

	stdout, err := cmd.StdoutPipe()
	if err != nil {
		json.NewEncoder(w).Encode(map[string]string{"status": "error", "message": err.Error()})
		flusher.Flush()
		return
	}

	if err := cmd.Start(); err != nil {
		json.NewEncoder(w).Encode(map[string]string{"status": "error", "message": "Failed to start native trainer: " + err.Error()})
		flusher.Flush()
		return
	}

	// Setup timeout handler
	done := make(chan error, 1)
	go func() {
		scanner := bufio.NewScanner(stdout)
		for scanner.Scan() {
			select {
			case <-r.Context().Done():
				// Client disconnected, kill process
				cmd.Process.Kill()
				done <- r.Context().Err()
				return
			default:
				w.Write(scanner.Bytes())
				w.Write([]byte("\n"))
				flusher.Flush()
			}
		}
		done <- scanner.Err()
	}()

	// Wait for completion with timeout or cancellation
	select {
	case err := <-done:
		if err != nil {
			log.Printf("Training stdout error: %v", err)
		}
	case <-r.Context().Done():
		cmd.Process.Kill()
		log.Println("Training cancelled by client disconnect")
	case <-time.After(30 * time.Minute):
		cmd.Process.Kill()
		json.NewEncoder(w).Encode(map[string]string{"status": "timeout", "message": "Training timed out after 30 minutes"})
		flusher.Flush()
		log.Println("Training timed out")
	}

	// Cleanup process
	if err := cmd.Wait(); err != nil {
		if exitErr, ok := err.(*exec.ExitError); ok {
			log.Printf("Training exited with code: %d", exitErr.ExitCode())
		}
	}
}

func handleIndex(w http.ResponseWriter, r *http.Request) {
	wd, _ := os.Getwd()
	
	// Handle root path
	if r.URL.Path == "/" {
		htmlPath := filepath.Join(wd, "wui_index.html")
		http.ServeFile(w, r, htmlPath)
		return
	}
	
	// Try serving static files first
	staticPath := filepath.Join(wd, r.URL.Path)
	if _, err := os.Stat(staticPath); err == nil {
		http.ServeFile(w, r, staticPath)
		return
	}
	
	// Fallback to SPA routing - serve index for all other paths
	htmlPath := filepath.Join(wd, "wui_index.html")
	http.ServeFile(w, r, htmlPath)
}

func main() {
	port := ":3486"

	http.HandleFunc("/", handleIndex)
	http.HandleFunc("/v1/completions", handleInference)
	http.HandleFunc("/v1/chat/completions", handleInference)
	http.HandleFunc("/v1/models", handleModels)
	http.HandleFunc("/health", handleHealth)
	http.HandleFunc("/v1/training", handleTraining)
	http.HandleFunc("/v1/agent/config", handleAgentConfig)

	fmt.Println("=== q-mini-wasm-v2-general Inference Server ===")
	fmt.Printf("Server running on http://localhost%s\n", port)
	fmt.Println("\nEndpoints:")
	fmt.Println("  GET  /                 - WUI (Web User Interface)")
	fmt.Println("  POST /v1/completions   - Send a prompt for completion")
	fmt.Println("  POST /v1/chat/completions - Chat interface")
	fmt.Println("  GET  /v1/models        - List available models")
	fmt.Println("  GET  /health           - Health check")
	fmt.Println("  POST /v1/training      - Start native SYCL training")
	fmt.Println("  GET/POST /v1/agent/config - Manage per-tab TOML configurations")
	fmt.Println("\nExample usage:")
	fmt.Println(`  curl -X POST http://localhost:3486/v1/completions \`)
	fmt.Println(`    -H "Content-Type: application/json" \`)
	fmt.Println(`    -d '{"model": "chat", "prompt": "Hello!"}'`)

	log.Fatal(http.ListenAndServe(port, nil))
}
