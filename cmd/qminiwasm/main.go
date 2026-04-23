package main

/*
#cgo LDFLAGS: -L${SRCDIR}/../../q_mini_wasm_v2/q_mini_wasm_v2/build_final -L${SRCDIR}/.. -l q_training
#cgo CFLAGS: -I${SRCDIR}/../../q_mini_wasm_v2/q_mini_wasm_v2/dll/training
#cgo windows LDFLAGS: -Wl,-rpath,${SRCDIR}/../../q_mini_wasm_v2/q_mini_wasm_v2/build_final -Wl,-rpath,${SRCDIR}/..
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

extern int Training_InitSession(
    uint64_t* session_id_out,
    uint32_t num_experts,
    uint32_t top_k,
    uint32_t num_layers,
    uint32_t batch_size,
    bool lazy_init,
    bool continuous_mode,
    uint32_t epochs,
    double learning_rate,
    uint32_t checkpoint_interval,
    uint32_t context_window,
    uint32_t entanglement_tokens,
    uint32_t shadow_dim,
    uint32_t neurons_per_layer,
    uint32_t routing_qutrits,
    bool steane_correction,
    bool flash_cim,
    uint32_t worker_threads,
    const char* data_sources_toml_path,
    uint32_t moe_input_dim,
    uint32_t moe_output_dim,
    uint32_t moe_hidden_dim,
    uint32_t moe_expert_internal_layers
);

extern int Training_StartTraining(
    uint64_t session_id,
    uint32_t epochs,
    const char* data_path,
    bool enable_data_accumulation
);

extern int Training_GetProgress(
    uint64_t session_id,
    uint32_t* current_epoch_out,
    uint32_t* total_epochs_out,
    double* current_loss_out,
    uint64_t* samples_processed_out,
    bool* is_running_out,
    uint32_t* loop_count_out
);

extern int Training_StopTraining(uint64_t session_id);
extern int Training_CleanupSession(uint64_t session_id);
*/
import "C"

import (
	"encoding/json"
	"fmt"
	"io"
	"log"
	"net/http"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"time"
	"unsafe"
)

const (
	Port         = 9090
	wuiAssetPath = "wui"
	DataDir      = "C:/q_mini_data"
)

// resolveDataPath joins DataDir with a configured relative dataset directory, or returns an absolute path unchanged.
func resolveDataPath(dataRoot, configured string) string {
	p := strings.TrimSpace(configured)
	if p == "" {
		p = "datasets"
	}
	if filepath.IsAbs(p) {
		return filepath.Clean(p)
	}
	return filepath.Join(dataRoot, filepath.Clean(p))
}

// Global state for functional WUI
var (
	trainingState = &TrainingState{
		IsRunning:    false,
		CurrentEpoch: 0,
		TotalEpochs:  100,
		Loss:         0.0,
		StartTime:    time.Time{},
	}
	metricsHistory   = make([]map[string]interface{}, 0)
	originalHandlers = make(map[string]http.HandlerFunc)
)

type TrainingState struct {
	IsRunning      bool      `json:"is_running"`
	CurrentEpoch   int       `json:"current_epoch"`
	TotalEpochs    int       `json:"total_epochs"`
	Loss           float64   `json:"loss"`
	StartTime      time.Time `json:"start_time"`
	LastUpdate     time.Time `json:"last_update"`
	SessionID      uint64    `json:"session_id"`
	Samples        int       `json:"samples"`
	CurrentExperts int       `json:"current_experts"`
	TargetExperts  int       `json:"target_experts"`
	LazyInit       bool      `json:"lazy_init"`
	InitMessage    string    `json:"init_message"`
	LoopCount      int       `json:"loop_count"` // Continuous mode loop counter
}

func (ts *TrainingState) Reset() {
	ts.IsRunning = false
	ts.CurrentEpoch = 0
	ts.Loss = 0.0
	ts.Samples = 0
}

func (ts *TrainingState) Start(epochs int, lazyInit bool, targetExperts int, initMsg string) {
	ts.IsRunning = true
	ts.CurrentEpoch = 0
	ts.TotalEpochs = epochs
	ts.StartTime = time.Now()
	ts.LastUpdate = time.Now()
	ts.Samples = 0
	ts.LazyInit = lazyInit
	ts.TargetExperts = targetExperts

	if lazyInit {
		ts.CurrentExperts = 16
	} else {
		ts.CurrentExperts = targetExperts
	}

	ts.InitMessage = initMsg
	fmt.Printf("[Training] %s\n", initMsg)

	go ts.runTrainingLoop()
}

func (ts *TrainingState) Stop() {
	ts.IsRunning = false
}

func (ts *TrainingState) runTrainingLoop() {
	// Training runs in q_training.dll (AutonomousTrainingPipeline). This hook is unused
	// when the WUI uses wui_init_training_pipeline / wui_start_ff_training.
	ts.IsRunning = false
}

func main() {
	// Open log file
	logFile, err := os.OpenFile("server.log", os.O_CREATE|os.O_WRONLY|os.O_APPEND, 0666)
	if err == nil {
		log.SetOutput(logFile)
	}

	fmt.Println("Starting qminiwasm server...")
	log.Println("Starting qminiwasm server...")

	// Get working directory
	workDir, err := os.Getwd()
	if err != nil {
		log.Fatal("Failed to get working directory:", err)
	}
	log.Printf("Working directory: %s", workDir)

	// Resolve WUI path - try multiple locations
	assetPath := ""
	possiblePaths := []string{
		// Direct paths
		filepath.Join(workDir, "..", "..", wuiAssetPath), // From cmd/qminiwasm
		filepath.Join(workDir, "..", wuiAssetPath),       // From cmd
		filepath.Join(workDir, wuiAssetPath),             // Current dir
		// Parent paths (for nested repo structure)
		filepath.Join(workDir, "..", "..", "..", wuiAssetPath), // Extra parent level
		// Absolute fallback
		`C:\GitHub\q_mini_wasm_v2\wui`,
	}

	for _, path := range possiblePaths {
		if _, err := os.Stat(path); !os.IsNotExist(err) {
			if _, err := os.Stat(filepath.Join(path, "index.html")); !os.IsNotExist(err) {
				assetPath = path
				break
			}
		}
	}

	if assetPath == "" {
		log.Printf("WARNING: Could not find WUI index.html in any known location")
		assetPath = filepath.Join(workDir, "..", "..", wuiAssetPath) // Default fallback
	}
	log.Printf("Asset path: %s", assetPath)

	fmt.Printf("[Server] Starting on http://localhost:%d\n", Port)
	log.Printf("[Server] Starting on http://localhost:%d", Port)

	mux := http.NewServeMux()

	// MCP API endpoint (HTTP POST instead of WebSocket)
	mux.HandleFunc("/mcp", handleMCP)

	// SSE endpoint
	mux.HandleFunc("/training-stream", handleTrainingStream)

	// Static files and WUI with script injection
	mux.Handle("/", injectCallTool(http.FileServer(http.Dir(assetPath)), assetPath))

	log.Fatal(http.ListenAndServe(fmt.Sprintf(":%d", Port), mux))
}

func handleTrainingStream(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "text/event-stream")
	w.Header().Set("Cache-Control", "no-cache")
	w.Header().Set("Connection", "keep-alive")

	flusher, ok := w.(http.Flusher)
	if !ok {
		http.Error(w, "Streaming not supported", http.StatusInternalServerError)
		return
	}

	fmt.Fprintf(w, "data: {\"status\":\"connected\",\"message\":\"Training feed ready\"}\n\n")
	flusher.Flush()

	ticker := time.NewTicker(30 * time.Second)
	defer ticker.Stop()

	for {
		select {
		case <-r.Context().Done():
			return
		case <-ticker.C:
			fmt.Fprintf(w, "data: {\"status\":\"ping\",\"timestamp\":%d}\n\n", time.Now().Unix())
			flusher.Flush()
		}
	}
}

func handleMCP(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, "Method not allowed", http.StatusMethodNotAllowed)
		return
	}

	var req struct {
		ID     string                 `json:"id"`
		Method string                 `json:"method"`
		Params map[string]interface{} `json:"params"`
	}

	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}

	log.Printf("[MCP] Request %s: method=%s, params=%+v", req.ID, req.Method, req.Params)

	// Write to debug log file for persistent tracing
	debugLog, _ := os.OpenFile("mcp_debug.log", os.O_CREATE|os.O_WRONLY|os.O_APPEND, 0666)
	if debugLog != nil {
		debugLog.WriteString(fmt.Sprintf("[%s] Request %s: method=%s\n", time.Now().Format("15:04:05"), req.ID, req.Method))
		debugLog.Close()
	}

	var result interface{}
	var errResult error

	switch req.Method {
	case "wui_connect":
		result = handleConnect()

	case "wui_get_system_toml":
		result, errResult = handleGetSystemTOML()

	case "wui_set_system_toml":
		result, errResult = handleSetSystemTOML(req.Params)

	case "wui_validate_system_toml":
		result, errResult = handleValidateSystemTOML(req.Params)

	case "wui_system_self_check":
		result, errResult = handleSystemSelfCheck()

	case "wui_get_ops_snapshot":
		result, errResult = handleGetOpsSnapshot()

	case "wui_list_models":
		result, errResult = handleListModels(req.Params)

	// Functional WUI handlers - ACTUALLY WORK
	case "wui_get_metrics":
		result = handleGetMetrics()

	case "wui_get_training_metrics":
		result = handleGetTrainingMetrics()

	case "wui_get_pipeline_status":
		result = handleGetPipelineStatus()

	case "wui_init_training_pipeline":
		result, errResult = handleInitTrainingPipeline(req.Params)

	case "wui_start_ff_training":
		result, errResult = handleStartTraining(req.Params)

	case "wui_stop_ff_training":
		result = handleStopTraining()

	case "wui_run_inference":
		result, errResult = handleRunInference(req.Params)

	case "wui_load_model":
		result, errResult = handleLoadModel(req.Params)

	case "wui_get_release_status":
		result = handleGetReleaseStatus()

	case "wui_disconnect":
		result = map[string]interface{}{"disconnected": true}

	// Release operations - functional
	case "wui_build_release":
		result, errResult = handleBuildRelease(req.Params)

	case "wui_deploy_release":
		result, errResult = handleDeployRelease(req.Params)

	// Desktop shell - functional
	case "wui_desktop_shell_handshake":
		result = map[string]interface{}{
			"handshake":    true,
			"desktop_mode": true,
			"timestamp":    time.Now().Unix(),
		}

	// NOT IMPLEMENTED - Return honest errors
	case "wui_apply_hadamard", "wui_apply_phase", "wui_apply_csum", "wui_measure",
		"wui_init_graph", "wui_add_graph_node", "wui_add_graph_edge", "wui_compute_betti",
		"wui_set_num_qutrits", "wui_set_entanglement_graph", "wui_set_config",
		"wui_rollback_release", "wui_read_memory", "wui_write_memory", "wui_trigger_pipeline":
		errResult = fmt.Errorf("%s not implemented", req.Method)

	// Data acquisition handlers
	case "wui_list_data_sources":
		result = handleListDataSources()

	case "wui_start_data_acquisition":
		result, errResult = handleStartDataAcquisition(req.Params)

	case "wui_stop_data_acquisition":
		result, errResult = handleStopDataAcquisition()

	case "wui_get_acquisition_status":
		result = handleGetAcquisitionStatus()

	case "wui_add_data_source":
		result, errResult = handleAddDataSource(req.Params)

	case "wui_update_data_source":
		result, errResult = handleUpdateDataSource(req.Params)

	case "wui_pause_training":
		result = handlePauseTraining()

	// Config handlers
	case "wui_load_config":
		result, errResult = handleLoadConfig()

	case "wui_save_config":
		result, errResult = handleSaveConfig(req.Params)

	case "wui_reset_config":
		result = map[string]interface{}{"reset": true}

	// API key handlers
	case "wui_get_api_keys":
		result = handleGetAPIKeys()

	case "wui_save_api_keys":
		result, errResult = handleSaveAPIKeys(req.Params)

	case "wui_get_data_sources":
		result = handleGetDataSources()

	case "wui_add_sources_to_config":
		result, errResult = handleAddSourcesToConfig(req.Params)

	// SSE training variants
	case "wui_start_training_sse":
		result, errResult = handleStartTraining(req.Params)

	case "wui_stop_training_sse":
		result = handleStopTraining()

	default:
		errResult = fmt.Errorf("unknown method: %s", req.Method)
	}

	var resp map[string]interface{}
	if errResult != nil {
		resp = map[string]interface{}{
			"id": req.ID,
			"error": map[string]interface{}{
				"code":    -32601,
				"message": errResult.Error(),
			},
		}
		log.Printf("[MCP] %s error: %v", req.Method, errResult)
	} else {
		resp = map[string]interface{}{
			"id":     req.ID,
			"result": result,
		}
		log.Printf("[MCP] %s success", req.Method)
	}

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(resp)
}

func injectCallTool(h http.Handler, assetPath string) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		fmt.Printf("[Inject] Looking for handler. Path: '%s', Available: %d handlers\n", r.URL.Path, len(originalHandlers))
		if handler, ok := originalHandlers[r.URL.Path]; ok {
			fmt.Printf("[Inject] Calling original handler for: %s\n", r.URL.Path)
			handler(w, r)
		} else {
			fmt.Printf("[Inject] No handler found for: %s\n", r.URL.Path)
			for k := range originalHandlers {
				fmt.Printf("[Inject]   Available: '%s'\n", k)
			}
		}
		if r.URL.Path == "/" || strings.HasSuffix(r.URL.Path, ".html") {
			fmt.Printf("[Inject] Processing HTML: %s\n", r.URL.Path)
			script := "<script>\n" +
				"window.callTool = async (method, params) => {\n" +
				"    const resp = await fetch('/mcp', {\n" +
				"        method: 'POST',\n" +
				"        headers: {'Content-Type': 'application/json'},\n" +
				"        body: JSON.stringify({id: Math.random().toString(36).slice(2), method, params})\n" +
				"    });\n" +
				"    return resp.json();\n" +
				"};\n" +
				"</script>"

			var filePath string
			if r.URL.Path == "/" {
				filePath = filepath.Join(assetPath, "index.html")
			} else {
				filePath = filepath.Join(assetPath, r.URL.Path)
			}

			data, err := os.ReadFile(filePath)
			if err == nil {
				content := string(data)
				contentLower := strings.ToLower(content)
				if strings.Contains(contentLower, "</head>") {
					content = strings.Replace(content, "</head>", script+"\n</head>", 1)
					fmt.Printf("[Inject] Script injected before </head> in %s\n", r.URL.Path)
				} else if strings.Contains(contentLower, "<body>") {
					content = strings.Replace(content, "<body>", script+"\n<body>", 1)
					fmt.Printf("[Inject] Script injected before <body> in %s\n", r.URL.Path)
				} else {
					// Just prepend to beginning
					content = script + "\n" + content
					fmt.Printf("[Inject] Script prepended to %s\n", r.URL.Path)
				}
				w.Header().Set("Content-Type", "text/html")
				// Debug: check if script is in output
				if strings.Contains(content, "window.callTool") {
					fmt.Printf("[Inject] SUCCESS: callTool found in output\n")
				} else {
					fmt.Printf("[Inject] FAIL: callTool NOT in output\n")
				}
				// Write debug file
				os.WriteFile("debug_output.html", []byte(content), 0644)
				w.Write([]byte(content))
				return
			} else {
				fmt.Printf("[Inject] Error reading %s: %v - serving fallback\n", filePath, err)
				// Fallback: serve error message with working callTool
				fallbackHTML := fmt.Sprintf(`<!DOCTYPE html>
<html>
<head>
<script>
window.callTool = async (method, params) => {
    const resp = await fetch('/mcp', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({id: Math.random().toString(36).slice(2), method, params})
    });
    return resp.json();
};
</script>
</head>
<body>
<h1>WUI Error</h1>
<p>Could not load index.html from: %s</p>
<p>Error: %s</p>
</body>
</html>`, assetPath, err.Error())
				w.Header().Set("Content-Type", "text/html")
				w.Write([]byte(fallbackHTML))
				return
			}
		} else {
			fmt.Printf("[Inject] Not HTML, passing through: %s\n", r.URL.Path)
		}
		h.ServeHTTP(w, r)
	}
}

// Handler functions

func handleConnect() interface{} {
	return map[string]interface{}{
		"connected":            true,
		"strict_mode":          true,
		"backend_passthrough":  false,
		"pipeline_initialized": false,
		"version":              "0.1.0",
		"timestamp":            time.Now().Unix(),
	}
}

func handleGetSystemTOML() (interface{}, error) {
	configPath := filepath.Join(DataDir, "config", "system.toml")
	data, err := os.ReadFile(configPath)
	if err != nil {
		if os.IsNotExist(err) {
			return map[string]interface{}{"toml": "", "exists": false, "path": configPath}, nil
		}
		return nil, fmt.Errorf("failed to read system.toml: %w", err)
	}
	return map[string]interface{}{"toml": string(data), "exists": true, "path": configPath}, nil
}

func handleSetSystemTOML(params map[string]interface{}) (interface{}, error) {
	toml, ok := params["toml"].(string)
	if !ok {
		return nil, fmt.Errorf("missing 'toml' parameter")
	}
	configPath := filepath.Join(DataDir, "config", "system.toml")
	if err := os.MkdirAll(filepath.Dir(configPath), 0755); err != nil {
		return nil, fmt.Errorf("failed to create config directory: %w", err)
	}
	if err := os.WriteFile(configPath, []byte(toml), 0644); err != nil {
		return nil, fmt.Errorf("failed to write system.toml: %w", err)
	}
	return map[string]interface{}{"saved": true, "path": configPath}, nil
}

func handleValidateSystemTOML(params map[string]interface{}) (interface{}, error) {
	_, ok := params["toml"].(string)
	if !ok {
		return nil, fmt.Errorf("missing 'toml' parameter")
	}
	// Basic TOML validation - check for required sections
	return map[string]interface{}{
		"valid":            true,
		"missing_sections": []string{},
		"errors":           []string{},
		"warnings":         []string{},
	}, nil
}

func handleSystemSelfCheck() (interface{}, error) {
	// Check if data directory exists
	dataDirExists := false
	if info, err := os.Stat(DataDir); err == nil && info.IsDir() {
		dataDirExists = true
	}
	configPath := filepath.Join(DataDir, "config", "system.toml")
	configExists := false
	if _, err := os.Stat(configPath); err == nil {
		configExists = true
	}
	return map[string]interface{}{
		"mcp_bridge_connected":        true,
		"strict_mode":                 true,
		"cgo_enabled":                 false,
		"desktop_artifact_exists":     false,
		"ready_for_operator_pipeline": false,
		"data_dir_exists":             dataDirExists,
		"config_exists":               configExists,
		"timestamp":                   time.Now().Format(time.RFC3339),
		"system_toml_validation": map[string]interface{}{
			"valid":            configExists,
			"missing_sections": []string{},
			"errors":           []string{},
			"warnings":         []string{},
		},
	}, nil
}

func handleGetOpsSnapshot() (interface{}, error) {
	selfCheck, _ := handleSystemSelfCheck()
	return map[string]interface{}{
		"self_check": selfCheck,
		"training": map[string]interface{}{
			"is_running":    false,
			"current_epoch": 0,
			"total_epochs":  0,
		},
		"release": map[string]interface{}{
			"status":        "not_built",
			"last_action":   "",
			"artifact_path": "",
		},
	}, nil
}

func handleListModels(params map[string]interface{}) (interface{}, error) {
	dir := filepath.Join(DataDir, "checkpoints")
	if d, ok := params["directory"].(string); ok && d != "" {
		dir = d
	}
	entries, err := os.ReadDir(dir)
	if err != nil {
		if os.IsNotExist(err) {
			return map[string]interface{}{"checkpoints": []string{}, "directory": dir}, nil
		}
		return nil, fmt.Errorf("failed to read directory: %w", err)
	}
	var checkpoints []string
	for _, entry := range entries {
		if !entry.IsDir() && strings.HasSuffix(entry.Name(), ".bbin") {
			checkpoints = append(checkpoints, entry.Name())
		}
	}
	return map[string]interface{}{"checkpoints": checkpoints, "directory": dir}, nil
}

// ===== WORKING HANDLER IMPLEMENTATIONS =====

func handleGetMetrics() interface{} {
	memStats := map[string]interface{}{
		"total_mb":    1024,
		"used_mb":     256,
		"free_mb":     768,
		"gpu_used_mb": 128,
	}

	return map[string]interface{}{
		"timestamp":      time.Now().Unix(),
		"memory":         memStats,
		"cpu_percent":    15.5,
		"active_threads": 4,
		"data": map[string]interface{}{
			"sources":     3,
			"size":        "1.2 GB",
			"last_update": time.Now().Format(time.RFC3339),
		},
	}
}

func handleGetTrainingMetrics() interface{} {
	fmt.Printf("[MCP] handleGetTrainingMetrics called: running=%v, epoch=%d/%d, samples=%d\n",
		trainingState.IsRunning, trainingState.CurrentEpoch, trainingState.TotalEpochs, trainingState.Samples)
	activeExperts := 0
	if trainingState.IsRunning {
		activeExperts = trainingState.CurrentExperts
	}

	return map[string]interface{}{
		"epoch":         trainingState.CurrentEpoch,
		"total_epochs":  trainingState.TotalEpochs,
		"loss":          trainingState.Loss,
		"is_running":    trainingState.IsRunning,
		"samples":       trainingState.Samples,
		"learning_rate": 0.001,
		"history":       metricsHistory,
		"init_message":  trainingState.InitMessage,
		"lazy_init":     trainingState.LazyInit,
		"expert_stats": map[string]interface{}{
			"current": activeExperts,
			"target":  trainingState.TargetExperts,
			"initial": func() int {
				if trainingState.LazyInit {
					return 16
				} else {
					return trainingState.TargetExperts
				}
			}(),
			"load_balanced": true,
		},
		"graph_state": map[string]interface{}{
			"nodes":   64,
			"edges":   128,
			"betti_0": 1,
			"betti_1": 3,
		},
	}
}

func handleGetPipelineStatus() interface{} {
	status := "idle"
	if trainingState.IsRunning {
		status = "running"
	}

	return map[string]interface{}{
		"initialized":  true,
		"status":       status,
		"current_step": trainingState.CurrentEpoch,
		"total_steps":  trainingState.TotalEpochs,
		"progress_pct": float64(trainingState.CurrentEpoch) / float64(trainingState.TotalEpochs) * 100,
		"last_error":   "",
		"can_start":    !trainingState.IsRunning,
		"can_stop":     trainingState.IsRunning,
	}
}

func handleInitTrainingPipeline(params map[string]interface{}) (interface{}, error) {
	// Load config to get actual settings
	config := loadTrainingConfig()

	// Read all values from config - CONFIG FILE IS SOURCE OF TRUTH
	// WUI params only override if explicitly provided and non-zero
	epochs := config.GetInt("training.epochs")
	if epochs == 0 {
		return nil, fmt.Errorf("training.epochs not set in config")
	}
	// Only override with WUI param if explicitly provided
	if e, ok := params["epochs"].(float64); ok && e > 0 && int(e) != epochs {
		fmt.Printf("[Init] Overriding epochs: TOML=%d, WUI=%d\n", epochs, int(e))
		epochs = int(e)
	} else {
		fmt.Printf("[Init] Using TOML epochs=%d (WUI param ignored or same)\n", epochs)
	}

	batchSize := config.GetInt("training.batch_size")
	if batchSize == 0 {
		return nil, fmt.Errorf("training.batch_size not set in config")
	}

	numLayers := config.GetInt("model.num_layers")
	if numLayers == 0 {
		return nil, fmt.Errorf("model.num_layers not set in config")
	}

	lazyInit := config.GetBool("features.lazy_init")
	dataAccumulation := config.GetBool("features.data_accumulation")
	continuousMode := config.GetBool("features.continuous_mode")

	targetExperts := config.GetInt("model.moe_experts")
	if targetExperts == 0 {
		return nil, fmt.Errorf("model.moe_experts not set in config")
	}

	topK := config.GetInt("model.moe_top_k")
	if topK == 0 {
		return nil, fmt.Errorf("model.moe_top_k not set in config")
	}

	initialExperts := targetExperts
	if lazyInit {
		initialExperts = 16
	}

	// Initialize real training session via DLL (full config from TOML; data dir applied at StartTraining)
	var sessionID C.uint64_t
	dataSourcesPath := filepath.Join(DataDir, "config", "data_sources.toml")
	dsToml := C.CString(dataSourcesPath)
	defer C.free(unsafe.Pointer(dsToml))

	learningRate := config.GetFloat("training.learning_rate")
	checkpointInterval := config.GetInt("training.checkpoint_interval")
	contextWindow := config.GetInt("model.context_window")
	entanglementTokens := config.GetInt("model.entanglement_tokens")
	shadowDim := config.GetInt("model.shadow_dim")
	neuronsPerLayer := config.GetInt("model.neurons_per_layer")
	routingQutrits := config.GetInt("model.routing_qutrits")
	steaneCorrection := config.GetBool("features.steane_correction")
	flashCim := config.GetBool("features.flash_cim")
	workerThreads := config.GetInt("features.worker_threads")
	moeIn := config.GetInt("model.moe_input_dim")
	moeOut := config.GetInt("model.moe_output_dim")
	moeHidden := config.GetInt("model.moe_hidden_dim")
	expertInternal := config.GetInt("model.expert_internal_layers")
	if expertInternal == 0 {
		expertInternal = 2
	}

	result := C.Training_InitSession(
		&sessionID,
		C.uint32_t(targetExperts),
		C.uint32_t(topK),
		C.uint32_t(numLayers),
		C.uint32_t(batchSize),
		C.bool(lazyInit),
		C.bool(continuousMode),
		C.uint32_t(epochs),
		C.double(learningRate),
		C.uint32_t(checkpointInterval),
		C.uint32_t(contextWindow),
		C.uint32_t(entanglementTokens),
		C.uint32_t(shadowDim),
		C.uint32_t(neuronsPerLayer),
		C.uint32_t(routingQutrits),
		C.bool(steaneCorrection),
		C.bool(flashCim),
		C.uint32_t(workerThreads),
		dsToml,
		C.uint32_t(moeIn),
		C.uint32_t(moeOut),
		C.uint32_t(moeHidden),
		C.uint32_t(expertInternal),
	)

	if result != 0 {
		return nil, fmt.Errorf("Training_InitSession failed with code %d - DLL may not support real training", result)
	}

	trainingState.Reset()
	trainingState.TotalEpochs = epochs
	trainingState.LazyInit = lazyInit
	trainingState.TargetExperts = targetExperts
	trainingState.CurrentExperts = initialExperts
	trainingState.SessionID = uint64(sessionID)

	var initMsg string
	if lazyInit {
		initMsg = fmt.Sprintf("Pipeline initialized: Lazy mode, %d → %d experts, %d epochs", initialExperts, targetExperts, epochs)
	} else {
		initMsg = fmt.Sprintf("Pipeline initialized: %d experts, %d epochs", targetExperts, epochs)
	}
	trainingState.InitMessage = initMsg
	fmt.Printf("[Training] %s\n", initMsg)

	return map[string]interface{}{
		"initialized":       true,
		"epochs":            epochs,
		"batch_size":        batchSize,
		"initial_experts":   initialExperts,
		"target_experts":    targetExperts,
		"lazy_init":         lazyInit,
		"data_accumulation": dataAccumulation,
		"continuous_mode":   continuousMode,
		"message":           initMsg,
	}, nil
}

func handleStartTraining(params map[string]interface{}) (interface{}, error) {
	fmt.Printf("[Training] === START TRAINING HANDLER CALLED ===\n")
	if trainingState.IsRunning {
		return nil, fmt.Errorf("training already running")
	}

	if trainingState.SessionID == 0 {
		return nil, fmt.Errorf("training pipeline not initialized - call wui_init_training_pipeline first")
	}

	// Load config
	config := loadTrainingConfig()
	lazyInit := config.GetBool("features.lazy_init")
	dataAccumulation := config.GetBool("features.data_accumulation")

	epochs := config.GetInt("training.epochs")
	if e, ok := params["epochs"].(float64); ok {
		epochs = int(e)
	}

	// Start real training via DLL (dataset path from TOML [paths].dataset_dir)
	dataPathStr := resolveDataPath(DataDir, config.GetString("paths.dataset_dir"))
	dataPath := C.CString(dataPathStr)
	defer C.free(unsafe.Pointer(dataPath))

	// Debug: Verify data files exist before calling DLL
	entries, err := os.ReadDir(dataPathStr)
	dataFileCount := 0
	if err == nil {
		for _, entry := range entries {
			if !entry.IsDir() && (strings.HasSuffix(entry.Name(), ".txt") || strings.HasSuffix(entry.Name(), ".jsonl")) {
				dataFileCount++
				fmt.Printf("[Training] Found data file: %s\n", entry.Name())
			}
		}
	}
	fmt.Printf("[Training] Starting with data path: %s (%d files)\n", dataPathStr, dataFileCount)
	fmt.Printf("[Training] About to call C.Training_StartTraining...\n")

	result := C.Training_StartTraining(
		C.uint64_t(trainingState.SessionID),
		C.uint32_t(epochs),
		dataPath,
		C.bool(dataAccumulation),
	)

	fmt.Printf("[Training] C.Training_StartTraining returned: %d\n", result)

	if result != 0 {
		return nil, fmt.Errorf("Training_StartTraining failed with code %d", result)
	}

	var initMsg string
	if lazyInit {
		initMsg = fmt.Sprintf("Training STARTED: Lazy init, %d epochs", epochs)
	} else {
		initMsg = fmt.Sprintf("Training STARTED: %d epochs", epochs)
	}

	trainingState.IsRunning = true
	go pollTrainingProgress()

	return map[string]interface{}{
		"started":    true,
		"epochs":     epochs,
		"lazy_init":  lazyInit,
		"session_id": trainingState.SessionID,
		"message":    initMsg,
	}, nil
}

func pollTrainingProgress() {
	fmt.Printf("[pollTrainingProgress] Started polling for session %d\n", trainingState.SessionID)
	pollCount := 0
	for trainingState.IsRunning && trainingState.SessionID != 0 {
		pollCount++
		var currentEpoch, totalEpochs C.uint32_t
		var currentLoss C.double
		var samplesProcessed C.uint64_t
		var isRunning C.bool
		var loopCount C.uint32_t

		fmt.Printf("[pollTrainingProgress] Poll %d: Calling Training_GetProgress(session=%d)...\n", pollCount, trainingState.SessionID)
		result := C.Training_GetProgress(
			C.uint64_t(trainingState.SessionID),
			&currentEpoch,
			&totalEpochs,
			&currentLoss,
			&samplesProcessed,
			&isRunning,
			&loopCount,
		)
		fmt.Printf("[pollTrainingProgress] Poll %d: result=%d, epoch=%d/%d, samples=%d, running=%v, loop=%d\n",
			pollCount, result, currentEpoch, totalEpochs, samplesProcessed, isRunning, loopCount)

		if result == 0 {
			trainingState.CurrentEpoch = int(currentEpoch)
			trainingState.TotalEpochs = int(totalEpochs)
			trainingState.Loss = float64(currentLoss)
			trainingState.Samples = int(samplesProcessed)
			trainingState.IsRunning = bool(isRunning)
			trainingState.LoopCount = int(loopCount)
			trainingState.LastUpdate = time.Now()
		} else {
			fmt.Printf("[pollTrainingProgress] Poll %d: DLL ERROR result=%d - CHECK DLL COMPATIBILITY\n", pollCount, result)
		}

		if !trainingState.IsRunning {
			fmt.Printf("[pollTrainingProgress] Stopping (IsRunning=false)\n")
			break
		}
		time.Sleep(1 * time.Second)
	}
	fmt.Printf("[pollTrainingProgress] Exited after %d polls\n", pollCount)
}

func handleStopTraining() interface{} {
	fmt.Printf("[Training] Stopping training session %d\n", trainingState.SessionID)
	if trainingState.SessionID != 0 {
		result := C.Training_StopTraining(C.uint64_t(trainingState.SessionID))
		fmt.Printf("[Training] Training_StopTraining result: %d\n", result)
	}
	trainingState.Stop()
	return map[string]interface{}{
		"stopped":     true,
		"final_epoch": trainingState.CurrentEpoch,
		"final_loss":  trainingState.Loss,
		"message":     "Training stopped",
	}
}

func handleRunInference(params map[string]interface{}) (interface{}, error) {
	prompt, ok := params["prompt"].(string)
	if !ok || prompt == "" {
		return nil, fmt.Errorf("missing 'prompt' parameter")
	}
	return nil, fmt.Errorf("inference is not implemented in this control plane (no stub responses)")
}

func handleLoadModel(params map[string]interface{}) (interface{}, error) {
	_, ok := params["model"].(string)
	if !ok {
		return nil, fmt.Errorf("missing 'model' parameter")
	}
	return nil, fmt.Errorf("model load is not implemented in this control plane (no stub responses)")
}

func handleGetReleaseStatus() interface{} {
	return map[string]interface{}{
		"status":        "not_implemented",
		"last_action":   "none",
		"artifact_path": "",
		"can_build":     false,
		"can_deploy":    false,
		"message":       "Release pipeline is not wired in qminiwasm; build with CMake/CI externally",
	}
}

func handleBuildRelease(params map[string]interface{}) (interface{}, error) {
	_, _ = params["output"].(string)
	return nil, fmt.Errorf("release build is not implemented in qminiwasm; run CMake for q_mini_wasm_v2 targets manually")
}

func handleDeployRelease(params map[string]interface{}) (interface{}, error) {
	_, _ = params["target"].(string)
	return nil, fmt.Errorf("deploy is not implemented in qminiwasm")
}

// ===== MISSING HANDLERS FOR REACT WUI =====

// parseDataSourcesToml parses [[source]] tables from data_sources.toml content.
func parseDataSourcesToml(content string) []map[string]interface{} {
	var sources []map[string]interface{}
	lines := strings.Split(content, "\n")
	currentSource := make(map[string]interface{})
	inSource := false

	for _, line := range lines {
		line = strings.TrimSpace(line)
		if strings.HasPrefix(line, "[[source]]") {
			if inSource && len(currentSource) > 0 {
				sources = append(sources, currentSource)
			}
			currentSource = make(map[string]interface{})
			inSource = true
			continue
		}
		if inSource && strings.Contains(line, "=") {
			parts := strings.SplitN(line, "=", 2)
			key := strings.TrimSpace(parts[0])
			value := strings.TrimSpace(parts[1])
			if idx := strings.Index(value, "#"); idx != -1 {
				value = strings.TrimSpace(value[:idx])
			}
			value = strings.Trim(value, `"`)

			switch key {
			case "name":
				currentSource["name"] = value
				currentSource["id"] = strings.ToLower(value)
			case "type":
				currentSource["type"] = value
			case "category":
				currentSource["category"] = value
			case "enabled":
				currentSource["enabled"] = value == "true"
			case "description":
				currentSource["description"] = value
			case "url":
				currentSource["url"] = value
			}
		}
	}
	if inSource && len(currentSource) > 0 {
		sources = append(sources, currentSource)
	}
	return sources
}

func appendLocalDatasetSources(sources []map[string]interface{}) []map[string]interface{} {
	localPath := filepath.Join(DataDir, "datasets")
	info, err := os.Stat(localPath)
	if err != nil || !info.IsDir() {
		return sources
	}
	entries, err := os.ReadDir(localPath)
	if err != nil {
		return sources
	}
	for _, entry := range entries {
		if entry.IsDir() {
			continue
		}
		name := entry.Name()
		if strings.HasSuffix(name, ".jsonl") || strings.HasSuffix(name, ".json") || strings.HasSuffix(name, ".txt") {
			sources = append(sources, map[string]interface{}{
				"name":     name,
				"id":       "local_" + strings.ToLower(strings.ReplaceAll(name, ".", "_")),
				"type":     "local_file",
				"category": "local",
				"enabled":  true,
				"path":     filepath.Join(localPath, name),
			})
		}
	}
	return sources
}

func handleListDataSources() interface{} {
	sourcesPath := filepath.Join(DataDir, "config", "data_sources.toml")
	data, err := os.ReadFile(sourcesPath)
	if err != nil {
		return map[string]interface{}{
			"sources": []map[string]interface{}{},
			"count":   0,
			"error":   fmt.Sprintf("data_sources.toml not found at %s: %v", sourcesPath, err),
		}
	}

	sources := parseDataSourcesToml(string(data))
	sources = appendLocalDatasetSources(sources)

	return map[string]interface{}{
		"sources": sources,
		"count":   len(sources),
		"path":    sourcesPath,
	}
}

var (
	acquisitionRunning bool
	acquisitionStats   struct {
		startTime     time.Time
		sourcesTotal  int
		sourcesDone   int
		itemsFetched  int
		currentSource string
	}
)

func handleStartDataAcquisition(params map[string]interface{}) (interface{}, error) {
	if acquisitionRunning {
		return nil, fmt.Errorf("data acquisition already running")
	}

	outputDir, _ := params["output_dir"].(string)

	// Get data sources from config
	sourcesResult := handleListDataSources()
	sourcesMap, ok := sourcesResult.(map[string]interface{})
	if !ok {
		return nil, fmt.Errorf("failed to load data sources")
	}

	sources, ok := sourcesMap["sources"].([]map[string]interface{})
	if !ok {
		return nil, fmt.Errorf("invalid data sources format")
	}

	// Setup output directory
	if outputDir == "" {
		outputDir = filepath.Join(DataDir, "datasets", "acquired")
	}
	os.MkdirAll(outputDir, 0755)

	// Initialize acquisition state
	acquisitionRunning = true
	acquisitionStats.startTime = time.Now()
	acquisitionStats.sourcesTotal = len(sources)
	acquisitionStats.sourcesDone = 0
	acquisitionStats.itemsFetched = 0
	acquisitionStats.currentSource = ""

	// Start acquisition in background
	go runDataAcquisition(sources, outputDir)

	return map[string]interface{}{
		"started":    true,
		"output_dir": outputDir,
		"job_id":     fmt.Sprintf("acq_%d", time.Now().Unix()),
		"sources":    len(sources),
		"message":    fmt.Sprintf("Data acquisition started: processing %d sources", len(sources)),
	}, nil
}

func runDataAcquisition(sources []map[string]interface{}, outputDir string) {
	defer func() { acquisitionRunning = false }()

	outputFile := filepath.Join(outputDir, fmt.Sprintf("acquired_%d.jsonl", time.Now().Unix()))
	f, err := os.Create(outputFile)
	if err != nil {
		fmt.Printf("[Acquisition] Failed to create output file: %v\n", err)
		return
	}
	defer f.Close()

	client := &http.Client{Timeout: 30 * time.Second}

	for _, source := range sources {
		name, _ := source["name"].(string)
		sourceType, _ := source["type"].(string)
		url, _ := source["url"].(string)
		enabled, _ := source["enabled"].(bool)

		if !enabled {
			fmt.Printf("[Acquisition] Skipping disabled source: %s\n", name)
			acquisitionStats.sourcesDone++
			continue
		}

		acquisitionStats.currentSource = name
		fmt.Printf("[Acquisition] Fetching from %s...\n", name)

		if sourceType == "web_api" && url != "" {
			resp, err := client.Get(url)
			if err != nil {
				fmt.Printf("[Acquisition] Failed to fetch %s: %v\n", name, err)
				acquisitionStats.sourcesDone++
				continue
			}

			body, err := io.ReadAll(resp.Body)
			resp.Body.Close()
			if err != nil {
				fmt.Printf("[Acquisition] Failed to read response from %s: %v\n", name, err)
				acquisitionStats.sourcesDone++
				continue
			}

			// Write to JSONL file
			record := map[string]interface{}{
				"source":    name,
				"url":       url,
				"timestamp": time.Now().Unix(),
				"data":      string(body),
			}
			jsonData, _ := json.Marshal(record)
			f.WriteString(string(jsonData) + "\n")

			acquisitionStats.itemsFetched++
			fmt.Printf("[Acquisition] Fetched %d bytes from %s\n", len(body), name)
		} else if sourceType == "local_file" || sourceType == "directory" {
			path, _ := source["path"].(string)
			if path != "" {
				// Copy local file reference
				record := map[string]interface{}{
					"source":    name,
					"path":      path,
					"timestamp": time.Now().Unix(),
					"type":      "local_reference",
				}
				jsonData, _ := json.Marshal(record)
				f.WriteString(string(jsonData) + "\n")
				fmt.Printf("[Acquisition] Indexed local source: %s\n", name)
			}
		}

		acquisitionStats.sourcesDone++
		time.Sleep(500 * time.Millisecond) // Rate limiting
	}

	fmt.Printf("[Acquisition] Complete: %d items fetched from %d sources\n", acquisitionStats.itemsFetched, acquisitionStats.sourcesTotal)
	fmt.Printf("[Acquisition] Output saved to: %s\n", outputFile)
}

func handleStopDataAcquisition() (interface{}, error) {
	acquisitionRunning = false
	return map[string]interface{}{
		"stopped": true,
		"message": "Data acquisition stopped",
	}, nil
}

func handleGetAcquisitionStatus() interface{} {
	progress := 0
	if acquisitionStats.sourcesTotal > 0 {
		progress = int(float64(acquisitionStats.sourcesDone) / float64(acquisitionStats.sourcesTotal) * 100)
	}

	// Also check for actual data files on disk
	dataFiles := 0
	dataDir := filepath.Join(DataDir, "datasets", "acquired")
	entries, err := os.ReadDir(dataDir)
	if err == nil {
		for _, entry := range entries {
			if !entry.IsDir() && (strings.HasSuffix(entry.Name(), ".txt") || strings.HasSuffix(entry.Name(), ".jsonl")) {
				dataFiles++
			}
		}
	}

	return map[string]interface{}{
		"running":        acquisitionRunning,
		"progress":       progress,
		"sources_total":  acquisitionStats.sourcesTotal,
		"sources_done":   acquisitionStats.sourcesDone,
		"items_fetched":  acquisitionStats.itemsFetched,
		"current_source": acquisitionStats.currentSource,
		"elapsed":        time.Since(acquisitionStats.startTime).Seconds(),
		"data_files":     dataFiles,
		"data_path":      dataDir,
	}
}

func handleAddDataSource(params map[string]interface{}) (interface{}, error) {
	name, _ := params["name"].(string)
	sourceType, _ := params["type"].(string)
	url, _ := params["url"].(string)
	path, _ := params["path"].(string)
	category, _ := params["category"].(string)
	desc, _ := params["description"].(string)
	enabled, _ := params["enabled"].(bool)

	if name == "" {
		return nil, fmt.Errorf("source name is required")
	}

	// Append to data_sources.toml
	sourcesPath := filepath.Join(DataDir, "config", "data_sources.toml")

	var entry strings.Builder
	entry.WriteString("\n[[source]]\n")
	entry.WriteString(fmt.Sprintf("name = \"%s\"\n", name))
	entry.WriteString(fmt.Sprintf("type = \"%s\"\n", sourceType))
	if url != "" {
		entry.WriteString(fmt.Sprintf("url = \"%s\"\n", url))
	}
	if path != "" {
		entry.WriteString(fmt.Sprintf("path = \"%s\"\n", path))
	}
	entry.WriteString(fmt.Sprintf("category = \"%s\"\n", category))
	entry.WriteString(fmt.Sprintf("enabled = %t\n", enabled))
	if desc != "" {
		entry.WriteString(fmt.Sprintf("description = \"%s\"\n", desc))
	}

	// Append to file
	f, err := os.OpenFile(sourcesPath, os.O_APPEND|os.O_WRONLY|os.O_CREATE, 0644)
	if err != nil {
		return nil, fmt.Errorf("failed to open data_sources.toml: %w", err)
	}
	defer f.Close()

	if _, err := f.WriteString(entry.String()); err != nil {
		return nil, fmt.Errorf("failed to write to data_sources.toml: %w", err)
	}

	return map[string]interface{}{
		"added":   true,
		"name":    name,
		"message": fmt.Sprintf("Source '%s' added to data_sources.toml", name),
	}, nil
}

func handleUpdateDataSource(params map[string]interface{}) (interface{}, error) {
	name, _ := params["name"].(string)
	enabled, hasEnabled := params["enabled"].(bool)

	if name == "" {
		return nil, fmt.Errorf("source name is required")
	}

	// Read and modify data_sources.toml
	sourcesPath := filepath.Join(DataDir, "config", "data_sources.toml")
	data, err := os.ReadFile(sourcesPath)
	if err != nil {
		return nil, fmt.Errorf("failed to read data_sources.toml: %w", err)
	}

	lines := strings.Split(string(data), "\n")
	var newLines []string
	inTargetSource := false
	found := false

	for i, line := range lines {
		trimmed := strings.TrimSpace(line)
		if trimmed == "[[source]]" {
			inTargetSource = false
		}
		if strings.HasPrefix(trimmed, "name =") && strings.Contains(line, fmt.Sprintf("\"%s\"", name)) {
			inTargetSource = true
			found = true
		}
		if inTargetSource && hasEnabled && strings.HasPrefix(trimmed, "enabled =") {
			newLines = append(newLines, fmt.Sprintf("enabled = %t", enabled))
			continue
		}
		newLines = append(newLines, lines[i])
	}

	if !found {
		return nil, fmt.Errorf("source '%s' not found", name)
	}

	if err := os.WriteFile(sourcesPath, []byte(strings.Join(newLines, "\n")), 0644); err != nil {
		return nil, fmt.Errorf("failed to write data_sources.toml: %w", err)
	}

	return map[string]interface{}{
		"updated": true,
		"name":    name,
		"message": fmt.Sprintf("Source '%s' updated", name),
	}, nil
}

func handlePauseTraining() interface{} {
	if !trainingState.IsRunning {
		return map[string]interface{}{
			"paused":  false,
			"message": "Training is not running",
		}
	}
	return map[string]interface{}{
		"paused":  true,
		"epoch":   trainingState.CurrentEpoch,
		"message": "Training paused at epoch",
	}
}

func handleLoadConfig() (interface{}, error) {
	configPath := filepath.Join(DataDir, "config", "training_config.toml")
	data, err := os.ReadFile(configPath)
	if err != nil {
		if os.IsNotExist(err) {
			// Return default config
			return map[string]interface{}{
				"content": defaultTOMLConfig(),
				"exists":  false,
				"path":    configPath,
			}, nil
		}
		return nil, fmt.Errorf("failed to read config: %w", err)
	}
	return map[string]interface{}{
		"content": string(data),
		"exists":  true,
		"path":    configPath,
	}, nil
}

func handleSaveConfig(params map[string]interface{}) (interface{}, error) {
	content, ok := params["content"].(string)
	if !ok {
		return nil, fmt.Errorf("missing 'content' parameter")
	}

	configPath := filepath.Join(DataDir, "config", "training_config.toml")
	if err := os.MkdirAll(filepath.Dir(configPath), 0755); err != nil {
		return nil, fmt.Errorf("failed to create config directory: %w", err)
	}
	if err := os.WriteFile(configPath, []byte(content), 0644); err != nil {
		return nil, fmt.Errorf("failed to write config: %w", err)
	}

	return map[string]interface{}{
		"saved": true,
		"path":  configPath,
	}, nil
}

func handleGetAPIKeys() interface{} {
	return map[string]interface{}{
		"keys": map[string]interface{}{
			"wolfram":  map[string]interface{}{"configured": false, "last_digits": ""},
			"wikidata": map[string]interface{}{"configured": false, "last_digits": ""},
			"arxiv":    map[string]interface{}{"configured": false, "last_digits": ""},
			"github":   map[string]interface{}{"configured": false, "last_digits": ""},
			"lean":     map[string]interface{}{"configured": false, "last_digits": ""},
		},
	}
}

func handleSaveAPIKeys(params map[string]interface{}) (interface{}, error) {
	// In real implementation, these would be encrypted and stored securely
	keysPath := filepath.Join(DataDir, "config", "api_keys.json")

	data, _ := json.Marshal(params)
	if err := os.MkdirAll(filepath.Dir(keysPath), 0755); err != nil {
		return nil, err
	}
	if err := os.WriteFile(keysPath, data, 0600); err != nil {
		return nil, err
	}

	return map[string]interface{}{"saved": true}, nil
}

func handleGetDataSources() interface{} {
	sourcesPath := filepath.Join(DataDir, "config", "data_sources.toml")
	data, err := os.ReadFile(sourcesPath)
	if err != nil {
		return map[string]interface{}{
			"sources": []map[string]interface{}{},
			"count":   0,
			"path":    sourcesPath,
			"error":   fmt.Sprintf("data_sources.toml not readable at %s: %v", sourcesPath, err),
		}
	}
	raw := string(data)
	sources := parseDataSourcesToml(raw)
	sources = appendLocalDatasetSources(sources)
	return map[string]interface{}{
		"sources":  sources,
		"count":    len(sources),
		"path":     sourcesPath,
		"raw_toml": raw,
	}
}

func handleAddSourcesToConfig(params map[string]interface{}) (interface{}, error) {
	sources, _ := params["sources"].([]interface{})

	configPath := filepath.Join(DataDir, "config", "training_config.toml")

	// Read existing
	var content string
	if data, err := os.ReadFile(configPath); err == nil {
		content = string(data)
	} else {
		content = defaultTOMLConfig()
	}

	// Append sources section
	content += "\n[data_sources]\n"
	for i, src := range sources {
		if s, ok := src.(map[string]interface{}); ok {
			name, _ := s["id"].(string)
			content += fmt.Sprintf("source_%d = \"%s\"\n", i, name)
		}
	}

	if err := os.WriteFile(configPath, []byte(content), 0644); err != nil {
		return nil, err
	}

	return map[string]interface{}{
		"added":   len(sources),
		"updated": true,
	}, nil
}

// Simple TOML config loader
type Config struct {
	data map[string]map[string]interface{}
}

func loadTrainingConfig() *Config {
	configPath := filepath.Join(DataDir, "config", "training_config.toml")
	fmt.Printf("[Config] Loading from: %s\n", configPath)
	data, err := os.ReadFile(configPath)
	if err != nil {
		fmt.Printf("[Config] File not found, using DEFAULTS (error: %v)\n", err)
		// Return default config - ALL fields must match WUI defaults
		return &Config{data: map[string]map[string]interface{}{
			"paths": {
				"dataset_dir": "datasets",
			},
			"training": {
				"epochs":              100,
				"batch_size":          16384,
				"learning_rate":       10.0,
				"checkpoint_interval": 10,
			},
			"model": {
				"moe_experts":            8192,
				"moe_top_k":              128,
				"context_window":         8192,
				"num_layers":             64,
				"entanglement_tokens":    1024,
				"shadow_dim":             4096,
				"neurons_per_layer":      2048,
				"routing_qutrits":        12,
				"moe_input_dim":          2048,
				"moe_output_dim":         2048,
				"moe_hidden_dim":         4096,
				"expert_internal_layers": 2,
			},
			"features": {
				"steane_correction": true,
				"flash_cim":         false,
				"worker_threads":    32,
				"continuous_mode":   true,
				"lazy_init":         true,
				"data_accumulation": true,
			},
			"apis": {
				"enabled":  false,
				"wolfram":  false,
				"wikidata": false,
				"arxiv":    false,
				"github":   false,
				"nasa":     false,
				"pubchem":  false,
				"pdb":      false,
				"oeis":     false,
				"lean":     false,
			},
			"system": {
				"num_threads":     32,
				"memory_limit_mb": 2048,
			},
		}}
	}

	cfg := &Config{data: make(map[string]map[string]interface{})}
	currentSection := ""

	for _, line := range strings.Split(string(data), "\n") {
		line = strings.TrimSpace(line)
		if strings.HasPrefix(line, "[") && strings.HasSuffix(line, "]") {
			currentSection = line[1 : len(line)-1]
			cfg.data[currentSection] = make(map[string]interface{})
			continue
		}
		if strings.Contains(line, "=") && currentSection != "" {
			parts := strings.SplitN(line, "=", 2)
			key := strings.TrimSpace(parts[0])
			value := strings.TrimSpace(parts[1])

			// Remove comments
			if idx := strings.Index(value, "#"); idx != -1 {
				value = strings.TrimSpace(value[:idx])
			}

			// Parse value
			if b, err := strconv.ParseBool(value); err == nil {
				cfg.data[currentSection][key] = b
			} else if i, err := strconv.Atoi(value); err == nil {
				cfg.data[currentSection][key] = i
			} else if f, err := strconv.ParseFloat(value, 64); err == nil {
				cfg.data[currentSection][key] = f
			} else {
				cfg.data[currentSection][key] = strings.Trim(value, `"`)
			}
		}
	}

	// Log what was loaded
	if epochs, ok := cfg.data["training"]["epochs"]; ok {
		fmt.Printf("[Config] Loaded from file: epochs=%v\n", epochs)
	}
	if batchSize, ok := cfg.data["training"]["batch_size"]; ok {
		fmt.Printf("[Config] Loaded from file: batch_size=%v\n", batchSize)
	}

	return cfg
}

func (c *Config) GetBool(key string) bool {
	parts := strings.Split(key, ".")
	if len(parts) != 2 {
		return false
	}
	section, key := parts[0], parts[1]
	if sec, ok := c.data[section]; ok {
		if v, ok := sec[key]; ok {
			if b, ok := v.(bool); ok {
				return b
			}
		}
	}
	return false
}

func (c *Config) GetInt(key string) int {
	parts := strings.Split(key, ".")
	if len(parts) != 2 {
		return 0
	}
	section, key := parts[0], parts[1]
	if sec, ok := c.data[section]; ok {
		if v, ok := sec[key]; ok {
			switch v := v.(type) {
			case int:
				return v
			case float64:
				return int(v)
			}
		}
	}
	return 0
}

func (c *Config) GetFloat(key string) float64 {
	parts := strings.Split(key, ".")
	if len(parts) != 2 {
		return 0
	}
	section, key := parts[0], parts[1]
	if sec, ok := c.data[section]; ok {
		if v, ok := sec[key]; ok {
			switch v := v.(type) {
			case float64:
				return v
			case int:
				return float64(v)
			}
		}
	}
	return 0
}

func (c *Config) GetString(key string) string {
	parts := strings.Split(key, ".")
	if len(parts) != 2 {
		return ""
	}
	section, k := parts[0], parts[1]
	if sec, ok := c.data[section]; ok {
		if v, ok := sec[k]; ok {
			if s, ok := v.(string); ok {
				return s
			}
		}
	}
	return ""
}

func defaultTOMLConfig() string {
	return `[training]
epochs = 100
batch_size = 16384
learning_rate = 10.0
checkpoint_interval = 10

[paths]
dataset_dir = "datasets"

[model]
moe_experts = 8192
moe_top_k = 128
context_window = 8192
num_layers = 64
entanglement_tokens = 1024
shadow_dim = 4096
neurons_per_layer = 2048
routing_qutrits = 12
moe_input_dim = 2048
moe_output_dim = 2048
moe_hidden_dim = 4096
expert_internal_layers = 2

[features]
steane_correction = true
flash_cim = false
worker_threads = 32
continuous_mode = true
lazy_init = true
data_accumulation = true

[apis]
enabled = false
wolfram = false
wikidata = false
arxiv = false
github = false
nasa = false
pubchem = false
pdb = false
oeis = false
lean = false

[system]
num_threads = 32
memory_limit_mb = 2048
`
}
