package main

import (
	"bufio"
	"encoding/json"
	"fmt"
	"log"
	"os"
	"sync"
	"time"

	"github.com/gorilla/websocket"
)

// MCPRequest represents a JSON-RPC request from MCP client
type MCPRequest struct {
	JSONRPC string          `json:"jsonrpc"`
	ID      interface{}     `json:"id"`
	Method  string          `json:"method"`
	Params  json.RawMessage `json:"params,omitempty"`
}

// MCPResponse represents a JSON-RPC response to MCP client
type MCPResponse struct {
	JSONRPC string      `json:"jsonrpc"`
	ID      interface{} `json:"id,omitempty"`
	Result  interface{} `json:"result,omitempty"`
	Error   *MCPError   `json:"error,omitempty"`
}

// MCPError represents a JSON-RPC error
type MCPError struct {
	Code    int         `json:"code"`
	Message string      `json:"message"`
	Data    interface{} `json:"data,omitempty"`
}

// WebSocketClient manages connection to WUI backend
type WebSocketClient struct {
	conn      *websocket.Conn
	url       string
	mu        sync.RWMutex
	connected bool
	responses map[string]chan interface{}
	muResp    sync.RWMutex
}

// NewWebSocketClient creates a new WebSocket client
func NewWebSocketClient(host string, port int) *WebSocketClient {
	url := fmt.Sprintf("ws://%s:%d/ws", host, port)
	return &WebSocketClient{
		url:       url,
		responses: make(map[string]chan interface{}),
	}
}

// Connect establishes WebSocket connection
func (c *WebSocketClient) Connect(timeout time.Duration) error {
	dialer := websocket.Dialer{
		HandshakeTimeout: timeout,
	}

	conn, _, err := dialer.Dial(c.url, nil)
	if err != nil {
		return fmt.Errorf("failed to connect to %s: %w", c.url, err)
	}

	c.mu.Lock()
	c.conn = conn
	c.connected = true
	c.mu.Unlock()

	// Start message handler
	go c.handleMessages()

	// Send registration
	c.Send(map[string]interface{}{
		"type":         "register",
		"client":       "mcp-bridge",
		"capabilities": []string{"control", "metrics", "automation"},
	})

	return nil
}

// handleMessages processes incoming WebSocket messages
func (c *WebSocketClient) handleMessages() {
	for {
		c.mu.RLock()
		conn := c.conn
		c.mu.RUnlock()

		if conn == nil {
			return
		}

		var msg map[string]interface{}
		if err := conn.ReadJSON(&msg); err != nil {
			log.Printf("WebSocket read error: %v", err)
			c.mu.Lock()
			c.connected = false
			c.mu.Unlock()
			return
		}

		// Handle response for awaited operations
		if msgType, ok := msg["type"].(string); ok {
			c.muResp.RLock()
			if ch, exists := c.responses[msgType]; exists {
				ch <- msg
			}
			c.muResp.RUnlock()
		}
	}
}

// Send transmits a message to the WUI backend
func (c *WebSocketClient) Send(msg map[string]interface{}) error {
	c.mu.RLock()
	defer c.mu.RUnlock()

	if !c.connected || c.conn == nil {
		return fmt.Errorf("not connected to WUI backend")
	}

	return c.conn.WriteJSON(msg)
}

// SendAndWait sends a message and waits for response
func (c *WebSocketClient) SendAndWait(msg map[string]interface{}, responseType string, timeout time.Duration) (map[string]interface{}, error) {
	// Create response channel
	ch := make(chan interface{}, 1)

	c.muResp.Lock()
	c.responses[responseType] = ch
	c.muResp.Unlock()

	defer func() {
		c.muResp.Lock()
		delete(c.responses, responseType)
		c.muResp.Unlock()
	}()

	// Send message
	if err := c.Send(msg); err != nil {
		return nil, err
	}

	// Wait for response
	select {
	case resp := <-ch:
		if m, ok := resp.(map[string]interface{}); ok {
			return m, nil
		}
		return nil, fmt.Errorf("unexpected response type")
	case <-time.After(timeout):
		return nil, fmt.Errorf("timeout waiting for response")
	}
}

// Disconnect closes the WebSocket connection
func (c *WebSocketClient) Disconnect() {
	c.mu.Lock()
	defer c.mu.Unlock()

	if c.conn != nil {
		c.conn.Close()
		c.conn = nil
	}
	c.connected = false
}

// IsConnected returns connection status
func (c *WebSocketClient) IsConnected() bool {
	c.mu.RLock()
	defer c.mu.RUnlock()
	return c.connected
}

// MCPServer handles MCP protocol
type MCPServer struct {
	wsClient *WebSocketClient
	pipeline *TrainingPipelineCGO
	scanner  *bufio.Scanner
	writer   *bufio.Writer
	tools    map[string]ToolHandler
}

// ToolHandler is a function that handles an MCP tool call
type ToolHandler func(params json.RawMessage) (interface{}, error)

// NewMCPServer creates a new MCP server instance
func NewMCPServer() *MCPServer {
	s := &MCPServer{
		scanner: bufio.NewScanner(os.Stdin),
		writer:  bufio.NewWriter(os.Stdout),
		tools:   make(map[string]ToolHandler),
	}
	s.registerTools()
	return s
}

// registerTools registers all available tool handlers
func (s *MCPServer) registerTools() {
	s.tools["wui_connect"] = s.handleWUIConnect
	s.tools["wui_disconnect"] = s.handleWUIDisconnect
	s.tools["wui_apply_hadamard"] = s.handleApplyHadamard
	s.tools["wui_apply_phase"] = s.handleApplyPhase
	s.tools["wui_apply_csum"] = s.handleApplyCSUM
	s.tools["wui_apply_pauli_x"] = s.handleApplyPauliX
	s.tools["wui_apply_pauli_z"] = s.handleApplyPauliZ
	s.tools["wui_measure"] = s.handleMeasure
	s.tools["wui_set_config"] = s.handleSetConfig
	s.tools["wui_set_num_qutrits"] = s.handleSetNumQutrits
	s.tools["wui_set_entanglement_graph"] = s.handleSetEntanglementGraph
	s.tools["wui_run_inference"] = s.handleRunInference
	s.tools["wui_get_metrics"] = s.handleGetMetrics
	s.tools["wui_get_pipeline_status"] = s.handleGetPipelineStatus
	s.tools["wui_trigger_pipeline"] = s.handleTriggerPipeline
	s.tools["wui_compute_betti"] = s.handleComputeBetti
	s.tools["wui_start_ff_training"] = s.handleStartFFTraining
	s.tools["wui_stop_ff_training"] = s.handleStopFFTraining
	s.tools["wui_init_graph"] = s.handleInitGraph
	s.tools["wui_add_graph_node"] = s.handleAddGraphNode
	s.tools["wui_add_graph_edge"] = s.handleAddGraphEdge
	s.tools["wui_read_memory"] = s.handleReadMemory
	s.tools["wui_write_memory"] = s.handleWriteMemory
	s.tools["wui_init_training_pipeline"] = s.handleInitTrainingPipeline
	s.tools["wui_set_pipeline_config"] = s.handleSetPipelineConfig
	s.tools["wui_get_training_metrics"] = s.handleGetTrainingMetrics
	s.tools["wui_apply_betti_guidance"] = s.handleApplyBettiGuidance
	s.tools["wui_pause_training"] = s.handlePauseTraining
	s.tools["wui_resume_training"] = s.handleResumeTraining
	s.tools["wui_export_model"] = s.handleExportModel
	s.tools["wui_import_model"] = s.handleImportModel
}

// Run starts the MCP server loop
func (s *MCPServer) Run() {
	for s.scanner.Scan() {
		line := s.scanner.Text()
		if line == "" {
			continue
		}

		var req MCPRequest
		if err := json.Unmarshal([]byte(line), &req); err != nil {
			s.sendError(nil, -32700, "Parse error", err.Error())
			continue
		}

		s.handleRequest(&req)
	}
}

// handleRequest processes a single MCP request
func (s *MCPServer) handleRequest(req *MCPRequest) {
	// Handle JSON-RPC 2.0 methods
	switch req.Method {
	case "initialize":
		s.handleInitialize(req)
	case "initialized":
		s.handleInitialized(req)
	case "tools/list":
		s.handleToolsList(req)
	case "tools/call":
		s.handleToolCall(req)
	case "resources/list":
		s.handleResourcesList(req)
	case "ping":
		s.sendResult(req.ID, map[string]interface{}{"pong": true})
	default:
		s.sendError(req.ID, -32601, "Method not found", req.Method)
	}
}

// handleInitialize handles MCP initialization
func (s *MCPServer) handleInitialize(req *MCPRequest) {
	result := map[string]interface{}{
		"protocolVersion": "2024-11-05",
		"capabilities": map[string]interface{}{
			"tools": map[string]interface{}{},
			"resources": map[string]interface{}{
				"listChanged": true,
			},
		},
		"serverInfo": map[string]interface{}{
			"name":    "qminiwasm-wui-automation",
			"version": "1.0.0",
		},
	}
	s.sendResult(req.ID, result)
}

// handleInitialized handles MCP initialized notification
func (s *MCPServer) handleInitialized(req *MCPRequest) {
	// No response needed for notifications
}

// handleToolsList returns list of available tools
func (s *MCPServer) handleToolsList(req *MCPRequest) {
	// Load tools from JSON definition
	tools := []map[string]interface{}{
		{
			"name":        "wui_connect",
			"description": "Establish WebSocket connection to WUI backend engine",
			"inputSchema": map[string]interface{}{
				"type": "object",
				"properties": map[string]interface{}{
					"host":       map[string]interface{}{"type": "string", "default": "localhost"},
					"port":       map[string]interface{}{"type": "integer", "default": 8080},
					"timeout_ms": map[string]interface{}{"type": "integer", "default": 5000},
				},
			},
		},
		{
			"name":        "wui_disconnect",
			"description": "Close WebSocket connection to WUI backend",
			"inputSchema": map[string]interface{}{"type": "object", "properties": map[string]interface{}{}},
		},
		{
			"name":        "wui_apply_hadamard",
			"description": "Apply Hadamard gate to specified qutrit",
			"inputSchema": map[string]interface{}{
				"type": "object",
				"properties": map[string]interface{}{
					"qutrit_index": map[string]interface{}{"type": "integer", "minimum": 0},
					"await_result": map[string]interface{}{"type": "boolean", "default": true},
				},
				"required": []string{"qutrit_index"},
			},
		},
		{
			"name":        "wui_apply_phase",
			"description": "Apply Phase gate to specified qutrit",
			"inputSchema": map[string]interface{}{
				"type": "object",
				"properties": map[string]interface{}{
					"qutrit_index": map[string]interface{}{"type": "integer", "minimum": 0},
					"await_result": map[string]interface{}{"type": "boolean", "default": true},
				},
				"required": []string{"qutrit_index"},
			},
		},
		{
			"name":        "wui_apply_csum",
			"description": "Apply Controlled-SUM gate between two qutrits",
			"inputSchema": map[string]interface{}{
				"type": "object",
				"properties": map[string]interface{}{
					"control":      map[string]interface{}{"type": "integer", "minimum": 0},
					"target":       map[string]interface{}{"type": "integer", "minimum": 0},
					"await_result": map[string]interface{}{"type": "boolean", "default": true},
				},
				"required": []string{"control", "target"},
			},
		},
		{
			"name":        "wui_measure",
			"description": "Measure qutrit in computational basis",
			"inputSchema": map[string]interface{}{
				"type": "object",
				"properties": map[string]interface{}{
					"qutrit_index": map[string]interface{}{"type": "integer", "minimum": 0},
					"await_result": map[string]interface{}{"type": "boolean", "default": true},
				},
				"required": []string{"qutrit_index"},
			},
		},
		{
			"name":        "wui_get_metrics",
			"description": "Retrieve current system metrics and status",
			"inputSchema": map[string]interface{}{
				"type": "object",
				"properties": map[string]interface{}{
					"subscribe":   map[string]interface{}{"type": "boolean", "default": false},
					"interval_ms": map[string]interface{}{"type": "integer", "default": 5000},
				},
			},
		},
	}

	s.sendResult(req.ID, map[string]interface{}{"tools": tools})
}

// handleToolCall executes a tool handler
func (s *MCPServer) handleToolCall(req *MCPRequest) {
	var params struct {
		Name      string          `json:"name"`
		Arguments json.RawMessage `json:"arguments"`
	}

	if err := json.Unmarshal(req.Params, &params); err != nil {
		s.sendError(req.ID, -32602, "Invalid params", err.Error())
		return
	}

	handler, exists := s.tools[params.Name]
	if !exists {
		s.sendError(req.ID, -32601, "Tool not found", params.Name)
		return
	}

	result, err := handler(params.Arguments)
	if err != nil {
		s.sendError(req.ID, -32603, "Tool execution error", err.Error())
		return
	}

	s.sendResult(req.ID, result)
}

// handleResourcesList returns available resources
func (s *MCPServer) handleResourcesList(req *MCPRequest) {
	resources := []map[string]interface{}{
		{
			"uri":      "file://q_mini_wasm_v2/wui/js/websocket-bridge.js",
			"name":     "websocket_bridge",
			"mimeType": "text/javascript",
		},
		{
			"uri":      "file://q_mini_wasm_v2/q_mini_wasm_v2/dll/q_mini_wasm_v2_api.hpp",
			"name":     "dll_api",
			"mimeType": "text/x-c++hdr",
		},
	}
	s.sendResult(req.ID, map[string]interface{}{"resources": resources})
}

// Tool Handlers

func (s *MCPServer) handleWUIConnect(params json.RawMessage) (interface{}, error) {
	var args struct {
		Host      string `json:"host"`
		Port      int    `json:"port"`
		TimeoutMs int    `json:"timeout_ms"`
	}
	if err := json.Unmarshal(params, &args); err != nil {
		return nil, err
	}

	if args.Host == "" {
		args.Host = "localhost"
	}
	if args.Port == 0 {
		args.Port = 8080
	}
	if args.TimeoutMs == 0 {
		args.TimeoutMs = 5000
	}

	if s.wsClient != nil && s.wsClient.IsConnected() {
		return map[string]interface{}{"connected": true, "message": "Already connected"}, nil
	}

	s.wsClient = NewWebSocketClient(args.Host, args.Port)
	timeout := time.Duration(args.TimeoutMs) * time.Millisecond

	if err := s.wsClient.Connect(timeout); err != nil {
		return nil, err
	}

	return map[string]interface{}{
		"connected": true,
		"url":       fmt.Sprintf("ws://%s:%d/ws", args.Host, args.Port),
	}, nil
}

func (s *MCPServer) handleWUIDisconnect(params json.RawMessage) (interface{}, error) {
	if s.wsClient != nil {
		s.wsClient.Disconnect()
	}
	return map[string]interface{}{"connected": false}, nil
}

func (s *MCPServer) handleApplyHadamard(params json.RawMessage) (interface{}, error) {
	var args struct {
		QutritIndex int  `json:"qutrit_index"`
		AwaitResult bool `json:"await_result"`
	}
	if err := json.Unmarshal(params, &args); err != nil {
		return nil, err
	}

	if s.wsClient == nil || !s.wsClient.IsConnected() {
		return nil, fmt.Errorf("not connected to WUI backend")
	}

	msg := map[string]interface{}{
		"type":      "quantum_operation",
		"operation": "hadamard",
		"target":    args.QutritIndex,
	}

	if args.AwaitResult {
		resp, err := s.wsClient.SendAndWait(msg, "quantum_operation_complete", 5*time.Second)
		if err != nil {
			return nil, err
		}
		return resp, nil
	}

	s.wsClient.Send(msg)
	return map[string]interface{}{"status": "sent"}, nil
}

func (s *MCPServer) handleApplyPhase(params json.RawMessage) (interface{}, error) {
	var args struct {
		QutritIndex int  `json:"qutrit_index"`
		AwaitResult bool `json:"await_result"`
	}
	if err := json.Unmarshal(params, &args); err != nil {
		return nil, err
	}

	if s.wsClient == nil || !s.wsClient.IsConnected() {
		return nil, fmt.Errorf("not connected to WUI backend")
	}

	msg := map[string]interface{}{
		"type":      "quantum_operation",
		"operation": "phase",
		"target":    args.QutritIndex,
	}

	if args.AwaitResult {
		resp, err := s.wsClient.SendAndWait(msg, "quantum_operation_complete", 5*time.Second)
		if err != nil {
			return nil, err
		}
		return resp, nil
	}

	s.wsClient.Send(msg)
	return map[string]interface{}{"status": "sent"}, nil
}

func (s *MCPServer) handleApplyCSUM(params json.RawMessage) (interface{}, error) {
	var args struct {
		Control     int  `json:"control"`
		Target      int  `json:"target"`
		AwaitResult bool `json:"await_result"`
	}
	if err := json.Unmarshal(params, &args); err != nil {
		return nil, err
	}

	if s.wsClient == nil || !s.wsClient.IsConnected() {
		return nil, fmt.Errorf("not connected to WUI backend")
	}

	msg := map[string]interface{}{
		"type":      "quantum_operation",
		"operation": "csum",
		"control":   args.Control,
		"target":    args.Target,
	}

	if args.AwaitResult {
		resp, err := s.wsClient.SendAndWait(msg, "quantum_operation_complete", 5*time.Second)
		if err != nil {
			return nil, err
		}
		return resp, nil
	}

	s.wsClient.Send(msg)
	return map[string]interface{}{"status": "sent"}, nil
}

func (s *MCPServer) handleApplyPauliX(params json.RawMessage) (interface{}, error) {
	var args struct {
		QutritIndex int `json:"qutrit_index"`
	}
	if err := json.Unmarshal(params, &args); err != nil {
		return nil, err
	}

	if s.wsClient == nil || !s.wsClient.IsConnected() {
		return nil, fmt.Errorf("not connected to WUI backend")
	}

	s.wsClient.Send(map[string]interface{}{
		"type":      "quantum_operation",
		"operation": "pauli_x",
		"target":    args.QutritIndex,
	})
	return map[string]interface{}{"status": "sent"}, nil
}

func (s *MCPServer) handleApplyPauliZ(params json.RawMessage) (interface{}, error) {
	var args struct {
		QutritIndex int `json:"qutrit_index"`
	}
	if err := json.Unmarshal(params, &args); err != nil {
		return nil, err
	}

	if s.wsClient == nil || !s.wsClient.IsConnected() {
		return nil, fmt.Errorf("not connected to WUI backend")
	}

	s.wsClient.Send(map[string]interface{}{
		"type":      "quantum_operation",
		"operation": "pauli_z",
		"target":    args.QutritIndex,
	})
	return map[string]interface{}{"status": "sent"}, nil
}

func (s *MCPServer) handleMeasure(params json.RawMessage) (interface{}, error) {
	var args struct {
		QutritIndex int  `json:"qutrit_index"`
		AwaitResult bool `json:"await_result"`
	}
	if err := json.Unmarshal(params, &args); err != nil {
		return nil, err
	}

	if s.wsClient == nil || !s.wsClient.IsConnected() {
		return nil, fmt.Errorf("not connected to WUI backend")
	}

	msg := map[string]interface{}{
		"type":      "quantum_operation",
		"operation": "measure",
		"target":    args.QutritIndex,
	}

	if args.AwaitResult {
		resp, err := s.wsClient.SendAndWait(msg, "measurement_result", 5*time.Second)
		if err != nil {
			return nil, err
		}
		return resp, nil
	}

	s.wsClient.Send(msg)
	return map[string]interface{}{"status": "sent"}, nil
}

func (s *MCPServer) handleSetConfig(params json.RawMessage) (interface{}, error) {
	var args struct {
		Section string      `json:"section"`
		Key     string      `json:"key"`
		Value   interface{} `json:"value"`
	}
	if err := json.Unmarshal(params, &args); err != nil {
		return nil, err
	}

	if s.wsClient == nil || !s.wsClient.IsConnected() {
		return nil, fmt.Errorf("not connected to WUI backend")
	}

	s.wsClient.Send(map[string]interface{}{
		"type":    "config_update",
		"section": args.Section,
		"key":     args.Key,
		"value":   args.Value,
	})
	return map[string]interface{}{"status": "config_updated"}, nil
}

func (s *MCPServer) handleSetNumQutrits(params json.RawMessage) (interface{}, error) {
	var args struct {
		Count int `json:"count"`
	}
	if err := json.Unmarshal(params, &args); err != nil {
		return nil, err
	}

	if s.wsClient == nil || !s.wsClient.IsConnected() {
		return nil, fmt.Errorf("not connected to WUI backend")
	}

	s.wsClient.Send(map[string]interface{}{
		"type":    "config_update",
		"section": "system.qgnn",
		"key":     "default_num_qutrits",
		"value":   args.Count,
	})
	return map[string]interface{}{"num_qutrits": args.Count}, nil
}

func (s *MCPServer) handleSetEntanglementGraph(params json.RawMessage) (interface{}, error) {
	var args struct {
		GraphType string `json:"graph_type"`
	}
	if err := json.Unmarshal(params, &args); err != nil {
		return nil, err
	}

	if s.wsClient == nil || !s.wsClient.IsConnected() {
		return nil, fmt.Errorf("not connected to WUI backend")
	}

	s.wsClient.Send(map[string]interface{}{
		"type":    "config_update",
		"section": "system.qgnn",
		"key":     "entanglement_graph",
		"value":   args.GraphType,
	})
	return map[string]interface{}{"graph_type": args.GraphType}, nil
}

func (s *MCPServer) handleRunInference(params json.RawMessage) (interface{}, error) {
	var args struct {
		Input     []int `json:"input"`
		TimeoutMs int   `json:"timeout_ms"`
	}
	if err := json.Unmarshal(params, &args); err != nil {
		return nil, err
	}

	if s.wsClient == nil || !s.wsClient.IsConnected() {
		return nil, fmt.Errorf("not connected to WUI backend")
	}

	if args.TimeoutMs == 0 {
		args.TimeoutMs = 5000
	}

	msg := map[string]interface{}{
		"type":  "inference",
		"input": args.Input,
	}

	resp, err := s.wsClient.SendAndWait(msg, "inference_result", time.Duration(args.TimeoutMs)*time.Millisecond)
	if err != nil {
		return nil, err
	}
	return resp, nil
}

func (s *MCPServer) handleGetMetrics(params json.RawMessage) (interface{}, error) {
	var args struct {
		Subscribe  bool `json:"subscribe"`
		IntervalMs int  `json:"interval_ms"`
	}
	if err := json.Unmarshal(params, &args); err != nil {
		return nil, err
	}

	if s.wsClient == nil || !s.wsClient.IsConnected() {
		return nil, fmt.Errorf("not connected to WUI backend")
	}

	s.wsClient.Send(map[string]interface{}{
		"type":     "subscribe_metrics",
		"interval": args.IntervalMs,
	})

	// Request immediate metrics
	resp, err := s.wsClient.SendAndWait(
		map[string]interface{}{"type": "get_metrics"},
		"metrics",
		5*time.Second,
	)
	if err != nil {
		return nil, err
	}
	return resp, nil
}

func (s *MCPServer) handleGetPipelineStatus(params json.RawMessage) (interface{}, error) {
	if s.wsClient == nil || !s.wsClient.IsConnected() {
		return nil, fmt.Errorf("not connected to WUI backend")
	}

	resp, err := s.wsClient.SendAndWait(
		map[string]interface{}{"type": "get_pipeline_status"},
		"pipeline_status",
		5*time.Second,
	)
	if err != nil {
		return nil, err
	}
	return resp, nil
}

func (s *MCPServer) handleTriggerPipeline(params json.RawMessage) (interface{}, error) {
	var args struct {
		PipelineType string `json:"pipeline_type"`
		Branch       string `json:"branch"`
	}
	if err := json.Unmarshal(params, &args); err != nil {
		return nil, err
	}

	if s.wsClient == nil || !s.wsClient.IsConnected() {
		return nil, fmt.Errorf("not connected to WUI backend")
	}

	s.wsClient.Send(map[string]interface{}{
		"type":     "trigger_pipeline",
		"pipeline": args.PipelineType,
		"branch":   args.Branch,
	})
	return map[string]interface{}{
		"triggered":     true,
		"pipeline_type": args.PipelineType,
		"branch":        args.Branch,
	}, nil
}

func (s *MCPServer) handleComputeBetti(params json.RawMessage) (interface{}, error) {
	var args struct {
		Nodes int `json:"nodes"`
		Edges int `json:"edges"`
	}
	if err := json.Unmarshal(params, &args); err != nil {
		return nil, err
	}

	if s.wsClient == nil || !s.wsClient.IsConnected() {
		return nil, fmt.Errorf("not connected to WUI backend")
	}

	resp, err := s.wsClient.SendAndWait(
		map[string]interface{}{
			"type":  "compute_betti",
			"nodes": args.Nodes,
			"edges": args.Edges,
		},
		"betti_result",
		5*time.Second,
	)
	if err != nil {
		return nil, err
	}
	return resp, nil
}

func (s *MCPServer) handleStartFFTraining(params json.RawMessage) (interface{}, error) {
	var args struct {
		Layers []int `json:"layers"`
		Epochs int   `json:"epochs"`
	}
	if err := json.Unmarshal(params, &args); err != nil {
		return nil, err
	}

	if s.wsClient == nil || !s.wsClient.IsConnected() {
		return nil, fmt.Errorf("not connected to WUI backend")
	}

	s.wsClient.Send(map[string]interface{}{
		"type":   "start_ff_training",
		"layers": args.Layers,
		"epochs": args.Epochs,
	})
	return map[string]interface{}{"training_started": true}, nil
}

func (s *MCPServer) handleStopFFTraining(params json.RawMessage) (interface{}, error) {
	if s.wsClient == nil || !s.wsClient.IsConnected() {
		return nil, fmt.Errorf("not connected to WUI backend")
	}

	s.wsClient.Send(map[string]interface{}{"type": "stop_ff_training"})
	return map[string]interface{}{"training_stopped": true}, nil
}

func (s *MCPServer) handleInitGraph(params json.RawMessage) (interface{}, error) {
	var args struct {
		Nodes int `json:"nodes"`
		Edges int `json:"edges"`
	}
	if err := json.Unmarshal(params, &args); err != nil {
		return nil, err
	}

	if s.wsClient == nil || !s.wsClient.IsConnected() {
		return nil, fmt.Errorf("not connected to WUI backend")
	}

	s.wsClient.Send(map[string]interface{}{
		"type":  "init_graph",
		"nodes": args.Nodes,
		"edges": args.Edges,
	})
	return map[string]interface{}{
		"initialized": true,
		"nodes":       args.Nodes,
		"edges":       args.Edges,
	}, nil
}

func (s *MCPServer) handleAddGraphNode(params json.RawMessage) (interface{}, error) {
	if s.wsClient == nil || !s.wsClient.IsConnected() {
		return nil, fmt.Errorf("not connected to WUI backend")
	}

	s.wsClient.Send(map[string]interface{}{"type": "add_graph_node"})
	return map[string]interface{}{"node_added": true}, nil
}

func (s *MCPServer) handleAddGraphEdge(params json.RawMessage) (interface{}, error) {
	if s.wsClient == nil || !s.wsClient.IsConnected() {
		return nil, fmt.Errorf("not connected to WUI backend")
	}

	s.wsClient.Send(map[string]interface{}{"type": "add_graph_edge"})
	return map[string]interface{}{"edge_added": true}, nil
}

func (s *MCPServer) handleReadMemory(params json.RawMessage) (interface{}, error) {
	var args struct {
		Offset int `json:"offset"`
		Length int `json:"length"`
	}
	if err := json.Unmarshal(params, &args); err != nil {
		return nil, err
	}

	if s.wsClient == nil || !s.wsClient.IsConnected() {
		return nil, fmt.Errorf("not connected to WUI backend")
	}

	resp, err := s.wsClient.SendAndWait(
		map[string]interface{}{
			"type":   "read_memory",
			"offset": args.Offset,
			"length": args.Length,
		},
		"memory_data",
		5*time.Second,
	)
	if err != nil {
		return nil, err
	}
	return resp, nil
}

func (s *MCPServer) handleWriteMemory(params json.RawMessage) (interface{}, error) {
	var args struct {
		Offset int   `json:"offset"`
		Data   []int `json:"data"`
	}
	if err := json.Unmarshal(params, &args); err != nil {
		return nil, err
	}

	if s.wsClient == nil || !s.wsClient.IsConnected() {
		return nil, fmt.Errorf("not connected to WUI backend")
	}

	s.wsClient.Send(map[string]interface{}{
		"type":   "write_memory",
		"offset": args.Offset,
		"data":   args.Data,
	})
	return map[string]interface{}{
		"written": true,
		"offset":  args.Offset,
		"length":  len(args.Data),
	}, nil
}

// Response helpers

func (s *MCPServer) sendResult(id interface{}, result interface{}) {
	resp := MCPResponse{
		JSONRPC: "2.0",
		ID:      id,
		Result:  result,
	}
	s.writeResponse(resp)
}

func (s *MCPServer) sendError(id interface{}, code int, message string, data interface{}) {
	resp := MCPResponse{
		JSONRPC: "2.0",
		ID:      id,
		Error: &MCPError{
			Code:    code,
			Message: message,
			Data:    data,
		},
	}
	s.writeResponse(resp)
}

func (s *MCPServer) writeResponse(resp MCPResponse) {
	data, err := json.Marshal(resp)
	if err != nil {
		log.Printf("Failed to marshal response: %v", err)
		return
	}

	fmt.Fprintln(os.Stdout, string(data))
}

func main() {
	server := NewMCPServer()
	server.Run()
}
