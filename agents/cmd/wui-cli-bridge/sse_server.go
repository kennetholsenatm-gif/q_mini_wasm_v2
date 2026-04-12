package main

import (
	"encoding/json"
	"fmt"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"sync"
	"time"
)

// SSEServer manages Server-Sent Events for training progress streaming
type SSEServer struct {
	mu          sync.RWMutex
	clients     map[chan string]bool
	trainingCmd *exec.Cmd
	isRunning   bool
	port        string
	lineBuffer  strings.Builder // Buffer for incomplete lines
}

// NewSSEServer creates a new SSE server
func NewSSEServer(port string) *SSEServer {
	if port == "" {
		port = "9090"
	}
	return &SSEServer{
		clients: make(map[chan string]bool),
		port:    port,
	}
}

// Start begins the HTTP server
func (s *SSEServer) Start() {
	http.HandleFunc("/training-stream", s.handleSSE)
	http.HandleFunc("/health", s.handleHealth)
	http.HandleFunc("/mcp", s.handleMCP) // JSON-RPC endpoint for WUI
	go func() {
		fmt.Printf("[SSE] Server starting on port %s\n", s.port)
		if err := http.ListenAndServe(":"+s.port, nil); err != nil {
			fmt.Printf("[SSE] Server error: %v\n", err)
		}
	}()
}

// handleMCP handles JSON-RPC requests from WUI
func (s *SSEServer) handleMCP(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "application/json")
	w.Header().Set("Access-Control-Allow-Origin", "*")
	w.Header().Set("Access-Control-Allow-Methods", "POST, OPTIONS")
	w.Header().Set("Access-Control-Allow-Headers", "Content-Type")

	if r.Method == "OPTIONS" {
		w.WriteHeader(http.StatusOK)
		return
	}

	if r.Method != "POST" {
		json.NewEncoder(w).Encode(map[string]interface{}{
			"jsonrpc": "2.0",
			"error":   map[string]interface{}{"code": -32600, "message": "Invalid Request"},
		})
		return
	}

	var req struct {
		JSONRPC string          `json:"jsonrpc"`
		ID      interface{}     `json:"id"`
		Method  string          `json:"method"`
		Params  json.RawMessage `json:"params"`
	}

	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		json.NewEncoder(w).Encode(map[string]interface{}{
			"jsonrpc": "2.0",
			"error":   map[string]interface{}{"code": -32700, "message": "Parse error"},
		})
		return
	}

	result, err := s.handleMCPMethod(req.Method, req.Params)

	response := map[string]interface{}{"jsonrpc": "2.0", "id": req.ID}
	if err != nil {
		response["error"] = map[string]interface{}{"code": -32603, "message": err.Error()}
	} else {
		response["result"] = result
	}

	json.NewEncoder(w).Encode(response)
}

// handleMCPMethod routes MCP method calls
func (s *SSEServer) handleMCPMethod(method string, params json.RawMessage) (interface{}, error) {
	switch method {
	case "wui_connect":
		return map[string]interface{}{
			"connected":            true,
			"strict_mode":          false,
			"backend_passthrough":  true,
			"pipeline_initialized": true,
		}, nil

	case "wui_start_training_sse":
		var p struct {
			DatasetPath string `json:"dataset_path"`
		}
		json.Unmarshal(params, &p)

		// Start actual training process
		args := []string{"--sse-mode"} // Enable SSE output format for WUI
		if p.DatasetPath != "" {
			args = append(args, "--dataset", p.DatasetPath)
		}
		// Add config file path
		args = append(args, "--config", "flash_cim_243expert/training_config.toml")

		// Get project root (current directory)
		projectRoot, _ := os.Getwd()

		if err := s.StartTraining(projectRoot, args); err != nil {
			return nil, fmt.Errorf("failed to start training: %v", err)
		}

		s.Broadcast(`{"status": "init", "message": "Training started via MCP"}`)
		return map[string]interface{}{
			"status":     "training_started",
			"stream_url": "http://localhost:" + s.port + "/training-stream",
		}, nil

	case "wui_stop_training", "wui_stop_training_sse":
		if err := s.StopTraining(); err != nil {
			return nil, fmt.Errorf("failed to stop training: %v", err)
		}
		s.Broadcast(`{"status": "stopped", "message": "Training stopped"}`)
		return map[string]interface{}{"stopped": true}, nil

	case "wui_list_models":
		return map[string]interface{}{"models": []interface{}{}}, nil

	case "wui_load_api_keys":
		// Load all API keys from Windows Credential Manager
		services := []string{"wolfram", "wikidata", "arxiv", "github", "lean"}
		keys := make(map[string]string)
		for _, svc := range services {
			if key, err := credManager.Load(svc); err == nil && key != "" {
				keys[svc] = key
			}
		}
		return map[string]interface{}{"keys": keys}, nil

	case "wui_store_api_keys":
		// Store API keys to Windows Credential Manager
		var p struct {
			Keys map[string]string `json:"keys"`
		}
		json.Unmarshal(params, &p)
		for svc, key := range p.Keys {
			if key != "" {
				credManager.Store(svc, key)
			}
		}
		return map[string]interface{}{"stored": true}, nil

	case "wui_get_training_config":
		// Load training config from file
		configPath := filepath.Join("flash_cim_243expert", "training_config.toml")
		data, err := os.ReadFile(configPath)
		if err != nil {
			return map[string]interface{}{"config": ""}, nil
		}
		return map[string]interface{}{"config": string(data)}, nil

	case "wui_save_training_config":
		// Save training config to file
		var p struct {
			Config string `json:"config"`
		}
		json.Unmarshal(params, &p)
		configDir := "flash_cim_243expert"
		os.MkdirAll(configDir, 0755)
		configPath := filepath.Join(configDir, "training_config.toml")
		if err := os.WriteFile(configPath, []byte(p.Config), 0644); err != nil {
			return nil, fmt.Errorf("failed to save config: %v", err)
		}
		return map[string]interface{}{"saved": true}, nil

	case "wui_get_data_sources":
		// Return data sources (placeholder - would load from storage)
		return map[string]interface{}{"sources": []interface{}{}}, nil

	case "wui_save_data_sources":
		// Save data sources (placeholder)
		return map[string]interface{}{"saved": true}, nil

	default:
		return nil, fmt.Errorf("unknown method: %s", method)
	}
}

// handleSSE handles SSE connections
func (s *SSEServer) handleSSE(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "text/event-stream")
	w.Header().Set("Cache-Control", "no-cache")
	w.Header().Set("Connection", "keep-alive")
	w.Header().Set("Access-Control-Allow-Origin", "*")

	client := make(chan string, 100)
	s.mu.Lock()
	s.clients[client] = true
	s.mu.Unlock()

	defer func() {
		s.mu.Lock()
		delete(s.clients, client)
		s.mu.Unlock()
		close(client)
	}()

	flusher, ok := w.(http.Flusher)
	if !ok {
		http.Error(w, "Streaming unsupported", http.StatusInternalServerError)
		return
	}

	// Send initial connection message
	fmt.Fprintf(w, "data: {\"status\": \"connected\", \"message\": \"SSE stream active\"}\n\n")
	flusher.Flush()

	for {
		select {
		case msg, ok := <-client:
			if !ok {
				return
			}
			fmt.Fprintf(w, "data: %s\n\n", msg)
			flusher.Flush()
		case <-r.Context().Done():
			return
		}
	}
}

// handleHealth provides health check endpoint
func (s *SSEServer) handleHealth(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(map[string]interface{}{
		"status":    "ok",
		"running":   s.isRunning,
		"clients":   len(s.clients),
		"timestamp": time.Now().Format(time.RFC3339),
	})
}

// Broadcast sends a message to all connected clients
// Strips SSE "data: " prefix if present (trainer outputs SSE format)
func (s *SSEServer) Broadcast(msg string) {
	original := msg
	// Strip SSE "data: " prefix if present
	msg = strings.TrimSpace(msg)
	if strings.HasPrefix(msg, "data: ") {
		msg = msg[6:] // Strip "data: " prefix
	}
	if msg == "" {
		return
	}

	// Debug: log first 100 chars of what we're broadcasting
	preview := msg
	if len(preview) > 100 {
		preview = preview[:100] + "..."
	}
	fmt.Printf("[SSE Broadcast] Original: %q -> Broadcast: %q\n", original[:min(50, len(original))], preview)

	s.mu.RLock()
	defer s.mu.RUnlock()
	for client := range s.clients {
		select {
		case client <- msg:
		default:
			// Channel full, skip
		}
	}
}

func min(a, b int) int {
	if a < b {
		return a
	}
	return b
}

// StartTraining launches the trainer with SSE output
func (s *SSEServer) StartTraining(projectRoot string, args []string) error {
	if s.isRunning {
		return fmt.Errorf("training already running")
	}

	trainerPath := filepath.Join(projectRoot, "q_mini_wasm_v2", "build_final", "Release", "q_mini_wasm_v2_trainer.exe")
	if _, err := os.Stat(trainerPath); os.IsNotExist(err) {
		// Try alternative path
		trainerPath = filepath.Join(projectRoot, "q_mini_wasm_v2_trainer.exe")
	}

	s.trainingCmd = exec.Command(trainerPath, args...)
	s.trainingCmd.Dir = projectRoot

	// Capture stdout for SSE
	stdout, err := s.trainingCmd.StdoutPipe()
	if err != nil {
		return err
	}

	stderr, err := s.trainingCmd.StderrPipe()
	if err != nil {
		return err
	}

	if err := s.trainingCmd.Start(); err != nil {
		return err
	}

	s.isRunning = true
	s.Broadcast(`{"status": "init", "message": "Training process started"}`)

	// Stream stdout to SSE clients with proper line buffering
	go func() {
		buf := make([]byte, 4096)
		for {
			n, err := stdout.Read(buf)
			if n > 0 {
				data := string(buf[:n])
				lines := strings.Split(data, "\n")

				// Process all lines except possibly the last incomplete one
				for i := 0; i < len(lines)-1; i++ {
					s.lineBuffer.WriteString(lines[i])
					line := s.lineBuffer.String()
					s.lineBuffer.Reset()
					if strings.TrimSpace(line) != "" {
						s.Broadcast(line)
					}
				}

				// Last line: if data ends with newline, it's complete; otherwise buffer it
				if strings.HasSuffix(data, "\n") {
					if strings.TrimSpace(lines[len(lines)-1]) != "" {
						s.Broadcast(lines[len(lines)-1])
					}
				} else {
					s.lineBuffer.WriteString(lines[len(lines)-1])
				}
			}
			if err != nil {
				// Flush any remaining buffered content
				if s.lineBuffer.Len() > 0 {
					line := s.lineBuffer.String()
					s.lineBuffer.Reset()
					if strings.TrimSpace(line) != "" {
						s.Broadcast(line)
					}
				}
				break
			}
		}
	}()

	// Stream stderr to SSE clients
	go func() {
		buf := make([]byte, 1024)
		for {
			n, err := stderr.Read(buf)
			if n > 0 {
				msg := strings.TrimSpace(string(buf[:n]))
				if msg != "" {
					s.Broadcast(fmt.Sprintf(`{"status": "error", "message": %q}`, msg))
				}
			}
			if err != nil {
				break
			}
		}
	}()

	// Wait for completion
	go func() {
		err := s.trainingCmd.Wait()
		s.isRunning = false
		if err != nil {
			s.Broadcast(fmt.Sprintf(`{"status": "error", "message": "Training exited with error: %v"}`, err))
		} else {
			s.Broadcast(`{"status": "complete", "message": "Training completed successfully"}`)
		}
	}()

	return nil
}

// StopTraining terminates the training process
func (s *SSEServer) StopTraining() error {
	if !s.isRunning || s.trainingCmd == nil || s.trainingCmd.Process == nil {
		return fmt.Errorf("no training process running")
	}
	return s.trainingCmd.Process.Kill()
}

// IsRunning returns whether training is active
func (s *SSEServer) IsRunning() bool {
	return s.isRunning
}

// Global SSE server instance
var sseServer *SSEServer

// InitSSEServer initializes the SSE server and broadcasts ready event
func InitSSEServer(port string) {
	sseServer = NewSSEServer(port)
	sseServer.Start()

	// Broadcast backend ready after brief delay to let clients connect
	go func() {
		time.Sleep(500 * time.Millisecond)
		msg := fmt.Sprintf(`{"type": "backend_ready", "timestamp": "%s", "message": "Backend initialized and ready"}`,
			time.Now().Format(time.RFC3339))
		sseServer.Broadcast(msg)
		fmt.Printf("[SSE] Broadcasted backend_ready event\n")
	}()
}

// CredentialManager handles Windows Credential Manager operations
type CredentialManager struct {
	targetPrefix string
}

// NewCredentialManager creates a new credential manager
func NewCredentialManager() *CredentialManager {
	return &CredentialManager{
		targetPrefix: "qmini/api/",
	}
}

// Store saves an API key to Windows Credential Manager
func (cm *CredentialManager) Store(service, apiKey string) error {
	target := cm.targetPrefix + service

	// Use cmdkey.exe to store credential
	cmd := exec.Command("cmdkey", "/add", target, "/user", "api_key", "/pass", apiKey)
	output, err := cmd.CombinedOutput()
	if err != nil {
		return fmt.Errorf("failed to store credential: %v (output: %s)", err, string(output))
	}
	return nil
}

// Load retrieves an API key from Windows Credential Manager
func (cm *CredentialManager) Load(service string) (string, error) {
	target := cm.targetPrefix + service

	// Use PowerShell to retrieve credential
	cmd := exec.Command("powershell", "-Command",
		fmt.Sprintf("$cred = Get-StoredCredential -Target '%s' -ErrorAction SilentlyContinue; if ($cred) { $cred.GetNetworkCredential().Password }", target))
	output, err := cmd.Output()
	if err != nil {
		return "", fmt.Errorf("failed to load credential: %v", err)
	}

	apiKey := strings.TrimSpace(string(output))
	if apiKey == "" {
		return "", fmt.Errorf("credential not found for service: %s", service)
	}
	return apiKey, nil
}

// Delete removes an API key from Windows Credential Manager
func (cm *CredentialManager) Delete(service string) error {
	target := cm.targetPrefix + service

	cmd := exec.Command("cmdkey", "/delete", target)
	output, err := cmd.CombinedOutput()
	if err != nil {
		return fmt.Errorf("failed to delete credential: %v (output: %s)", err, string(output))
	}
	return nil
}

// List returns all stored API key services
func (cm *CredentialManager) List() ([]string, error) {
	cmd := exec.Command("cmdkey", "/list")
	output, err := cmd.Output()
	if err != nil {
		return nil, fmt.Errorf("failed to list credentials: %v", err)
	}

	var services []string
	lines := strings.Split(string(output), "\n")
	for _, line := range lines {
		if strings.Contains(line, cm.targetPrefix) {
			// Extract service name from target
			parts := strings.Split(line, cm.targetPrefix)
			if len(parts) > 1 {
				service := strings.TrimSpace(parts[1])
				if service != "" {
					services = append(services, service)
				}
			}
		}
	}
	return services, nil
}

// Global credential manager
var credManager = NewCredentialManager()
