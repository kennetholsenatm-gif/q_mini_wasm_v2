package main

import (
	"encoding/json"
	"fmt"
	"time"
)

// PipelineState represents the training pipeline state
type PipelineState int

const (
	StateIdle PipelineState = iota
	StateInitializing
	StateReady
	StateAcquiringData
	StateTraining
	StateEvaluatingTopology
	StateOptimizingGraph
	StateCheckpointing
	StatePaused
	StateStopping
	StateComplete
	StateError
)

func (s PipelineState) String() string {
	switch s {
	case StateIdle:
		return "idle"
	case StateInitializing:
		return "initializing"
	case StateReady:
		return "ready"
	case StateAcquiringData:
		return "acquiring_data"
	case StateTraining:
		return "training"
	case StateEvaluatingTopology:
		return "evaluating_topology"
	case StateOptimizingGraph:
		return "optimizing_graph"
	case StateCheckpointing:
		return "checkpointing"
	case StatePaused:
		return "paused"
	case StateStopping:
		return "stopping"
	case StateComplete:
		return "complete"
	case StateError:
		return "error"
	default:
		return "unknown"
	}
}

// PipelineConfig holds training pipeline configuration
type PipelineConfig struct {
	AcquisitionThreads      int     `json:"acquisition_threads"`
	NumExperts             int     `json:"num_experts"`
	GraphNodes             int     `json:"graph_nodes"`
	GraphEdges             int     `json:"graph_edges"`
	EnableBettiGuidance    bool    `json:"enable_betti_guidance"`
	EnableKnowledgeEngine  bool    `json:"enable_knowledge_engine"`
	EnableWUIStreaming     bool    `json:"enable_wui_streaming"`
	Epochs                 int     `json:"epochs"`
	BatchSize              int     `json:"batch_size"`
	LearningRate           float64 `json:"learning_rate"`
	BettiGuidanceThreshold int     `json:"betti_guidance_threshold"`
	FFNumLayers            int     `json:"ff_num_layers"`
	FFLayerWidth           int     `json:"ff_layer_width"`
	MoETopK                int     `json:"moe_top_k"`
	MoEInputDim            int     `json:"moe_input_dim"`
	MoEOutputDim           int     `json:"moe_output_dim"`
	MoEHiddenDim           int     `json:"moe_hidden_dim"`
}

// DefaultPipelineConfig returns default configuration
func DefaultPipelineConfig() PipelineConfig {
	return PipelineConfig{
		AcquisitionThreads:      4,
		NumExperts:             243,
		GraphNodes:             64,
		GraphEdges:             112,
		EnableBettiGuidance:    true,
		EnableKnowledgeEngine:  true,
		EnableWUIStreaming:     true,
		Epochs:                 1000,
		BatchSize:              32,
		LearningRate:           0.001,
		BettiGuidanceThreshold: 15,
		FFNumLayers:            3,
		FFLayerWidth:           128,
		MoETopK:                3,
		MoEInputDim:            64,
		MoEOutputDim:           64,
		MoEHiddenDim:           128,
	}
}

// FFMetrics holds Forward-Forward training metrics
type FFMetrics struct {
	PositiveGoodness uint32  `json:"positive_goodness"`
	NegativeGoodness uint32  `json:"negative_goodness"`
	GoodnessDelta    int32   `json:"goodness_delta"`
	TotalTrainCalls  uint64  `json:"total_train_calls"`
}

// MoEMetrics holds MoE routing metrics
type MoEMetrics struct {
	LoadBalanceScore      float32   `json:"load_balance_score"`
	AvgRoutingLatencyMs   float32   `json:"avg_routing_latency_ms"`
	ExpertUtilization     []float32  `json:"expert_utilization"`
	ExpertDeltas          []int32    `json:"expert_deltas"`
	ExpertRequestCounts   []uint32   `json:"expert_request_counts"`
}

// BettiNumbers holds topological data analysis results
type BettiNumbers struct {
	Beta0                uint32 `json:"beta_0"`
	Beta1                uint32 `json:"beta_1"`
	Beta2                uint32 `json:"beta_2"`
	EulerCharacteristic  int32  `json:"euler_characteristic"`
}

// GraphState holds quantum graph topology state
type GraphState struct {
	Nodes     int    `json:"nodes"`
	Edges     int    `json:"edges"`
	Topology  string `json:"topology"`
}

// DataSynthesizerStats holds data acquisition statistics
type DataSynthesizerStats struct {
	TotalAcquired   int `json:"total_acquired"`
	TotalPerturbed  int `json:"total_perturbed"`
	APIFailures     int `json:"api_failures"`
	QueueDepth      int `json:"queue_depth"`
}

// TrainingMetrics holds comprehensive pipeline metrics
type TrainingMetrics struct {
	FFMetrics       FFMetrics            `json:"ff_metrics"`
	MoEMetrics      MoEMetrics           `json:"moe_metrics"`
	BettiNumbers    BettiNumbers         `json:"betti_numbers"`
	GraphState      GraphState           `json:"graph_state"`
	DataSynthesizer DataSynthesizerStats `json:"data_synthesizer"`
	CurrentEpoch    uint64               `json:"current_epoch"`
	CurrentBatch    uint64               `json:"current_batch"`
	TrainingProgress float32             `json:"training_progress"`
	IsRunning       bool                 `json:"is_running"`
	StatusMessage   string               `json:"status_message"`
}

// PipelineController manages the training pipeline lifecycle
type PipelineController struct {
	state      PipelineState
	config     PipelineConfig
	metrics    TrainingMetrics
	wsClient   *WebSocketClient
	startedAt  time.Time
	pausedAt   *time.Time
	stopChan   chan bool
}

// NewPipelineController creates a new pipeline controller
func NewPipelineController(wsClient *WebSocketClient) *PipelineController {
	return &PipelineController{
		state:     StateIdle,
		config:    DefaultPipelineConfig(),
		wsClient:  wsClient,
		stopChan:  make(chan bool),
	}
}

// InitializePipeline initializes the complete training pipeline
func (c *PipelineController) InitializePipeline(config PipelineConfig) error {
	if c.state != StateIdle {
		return fmt.Errorf("pipeline already initialized, current state: %s", c.state.String())
	}

	c.state = StateInitializing
	c.config = config

	// Send initialization message to WUI backend
	if c.wsClient != nil && c.wsClient.IsConnected() {
		initMsg := map[string]interface{}{
			"type":                     "init_training_pipeline",
			"acquisition_threads":      config.AcquisitionThreads,
			"num_experts":             config.NumExperts,
			"graph_nodes":             config.GraphNodes,
			"graph_edges":             config.GraphEdges,
			"enable_betti_guidance":   config.EnableBettiGuidance,
			"enable_knowledge_engine": config.EnableKnowledgeEngine,
			"epochs":                  config.Epochs,
			"batch_size":              config.BatchSize,
		}
		c.wsClient.Send(initMsg)
	}

	// Simulate initialization (would be async in production)
	c.state = StateReady
	return nil
}

// StartTraining starts the training loop
func (c *PipelineController) StartTraining(layers []int, epochs int) error {
	if c.state != StateReady && c.state != StatePaused {
		return fmt.Errorf("pipeline not ready, current state: %s", c.state.String())
	}

	c.state = StateAcquiringData
	c.startedAt = time.Now()

	// Send start message to WUI backend
	if c.wsClient != nil && c.wsClient.IsConnected() {
		startMsg := map[string]interface{}{
			"type":   "start_ff_training",
			"layers": layers,
			"epochs": epochs,
		}
		c.wsClient.Send(startMsg)
	}

	// Start training goroutine
	go c.trainingLoop()

	c.state = StateTraining
	return nil
}

// StopTraining gracefully stops the training
func (c *PipelineController) StopTraining() error {
	if c.state != StateTraining && c.state != StateAcquiringData && c.state != StatePaused {
		return fmt.Errorf("pipeline not running, current state: %s", c.state.String())
	}

	c.state = StateStopping
	close(c.stopChan)

	// Send stop message
	if c.wsClient != nil && c.wsClient.IsConnected() {
		c.wsClient.Send(map[string]interface{}{"type": "stop_ff_training"})
	}

	c.state = StateIdle
	return nil
}

// PauseTraining pauses the training (can be resumed)
func (c *PipelineController) PauseTraining() error {
	if c.state != StateTraining {
		return fmt.Errorf("pipeline not training, current state: %s", c.state.String())
	}

	c.state = StatePaused
	now := time.Now()
	c.pausedAt = &now

	// Send pause message
	if c.wsClient != nil && c.wsClient.IsConnected() {
		c.wsClient.Send(map[string]interface{}{"type": "pause_training"})
	}

	return nil
}

// ResumeTraining resumes paused training
func (c *PipelineController) ResumeTraining() error {
	if c.state != StatePaused {
		return fmt.Errorf("pipeline not paused, current state: %s", c.state.String())
	}

	c.state = StateTraining
	c.pausedAt = nil

	// Send resume message
	if c.wsClient != nil && c.wsClient.IsConnected() {
		c.wsClient.Send(map[string]interface{}{"type": "resume_training"})
	}

	return nil
}

// GetComprehensiveMetrics retrieves metrics from all pipeline stages
func (c *PipelineController) GetComprehensiveMetrics(includeBetti, includeGraph, includeExpertStats bool) TrainingMetrics {
	// Request metrics from WUI backend
	if c.wsClient != nil && c.wsClient.IsConnected() {
		c.wsClient.Send(map[string]interface{}{
			"type":                 "get_training_metrics",
			"include_betti":        includeBetti,
			"include_graph_state":  includeGraph,
			"include_expert_stats": includeExpertStats,
		})
	}

	// Return current cached metrics
	return c.metrics
}

// UpdateConfig updates pipeline configuration
func (c *PipelineController) UpdateConfig(config PipelineConfig) error {
	// Can only update when paused or idle
	if c.state != StateIdle && c.state != StatePaused {
		return fmt.Errorf("can only update config when idle or paused, current state: %s", c.state.String())
	}

	c.config = config

	// Send config update
	if c.wsClient != nil && c.wsClient.IsConnected() {
		c.wsClient.Send(map[string]interface{}{
			"type":                     "set_pipeline_config",
			"batch_size":              config.BatchSize,
			"learning_rate":           config.LearningRate,
			"betti_guidance_threshold": config.BettiGuidanceThreshold,
			"enable_wui_streaming":    config.EnableWUIStreaming,
		})
	}

	return nil
}

// ApplyBettiGuidance triggers Betti-guided topology optimization
func (c *PipelineController) ApplyBettiGuidance(force bool) error {
	if c.state != StateTraining && c.state != StatePaused && c.state != StateReady {
		return fmt.Errorf("cannot apply guidance in current state: %s", c.state.String())
	}

	prevState := c.state
	c.state = StateEvaluatingTopology

	// Send Betti guidance request
	if c.wsClient != nil && c.wsClient.IsConnected() {
		c.wsClient.Send(map[string]interface{}{
			"type":  "apply_betti_guidance",
			"force": force,
		})
	}

	// Simulate evaluation then optimization
	c.state = StateOptimizingGraph
	
	// Would wait for backend response here in production
	
	c.state = prevState
	return nil
}

// ExportModel exports trained model to file
func (c *PipelineController) ExportModel(path string, includeTopology bool) error {
	if c.wsClient != nil && c.wsClient.IsConnected() {
		c.wsClient.Send(map[string]interface{}{
			"type":              "export_model",
			"path":              path,
			"include_topology":  includeTopology,
		})
	}
	return nil
}

// ImportModel imports model from file
func (c *PipelineController) ImportModel(path string) error {
	if c.state != StateIdle && c.state != StateReady {
		return fmt.Errorf("can only import when idle or ready, current state: %s", c.state.String())
	}

	if c.wsClient != nil && c.wsClient.IsConnected() {
		c.wsClient.Send(map[string]interface{}{
			"type": "import_model",
			"path": path,
		})
	}
	return nil
}

// GetState returns current pipeline state
func (c *PipelineController) GetState() PipelineState {
	return c.state
}

// GetConfig returns current configuration
func (c *PipelineController) GetConfig() PipelineConfig {
	return c.config
}

// trainingLoop simulates the training loop
func (c *PipelineController) trainingLoop() {
	ticker := time.NewTicker(5 * time.Second)
	defer ticker.Stop()

	for {
		select {
		case <-c.stopChan:
			return
		case <-ticker.C:
			if c.state == StateTraining {
				// Simulate metrics updates
				c.updateSimulatedMetrics()
			}
		}
	}
}

// updateSimulatedMetrics generates simulated metrics for demonstration
func (c *PipelineController) updateSimulatedMetrics() {
	c.metrics.CurrentBatch++
	
	if c.metrics.CurrentBatch%100 == 0 {
		c.metrics.CurrentEpoch++
	}

	// Simulate FF metrics
	c.metrics.FFMetrics.PositiveGoodness = 45 + uint32(c.metrics.CurrentBatch%10)
	c.metrics.FFMetrics.NegativeGoodness = 12 + uint32(c.metrics.CurrentBatch%5)
	c.metrics.FFMetrics.GoodnessDelta = int32(c.metrics.FFMetrics.PositiveGoodness) - int32(c.metrics.FFMetrics.NegativeGoodness)
	c.metrics.FFMetrics.TotalTrainCalls = c.metrics.CurrentBatch * uint64(c.config.BatchSize)

	// Simulate MoE metrics
	c.metrics.MoEMetrics.LoadBalanceScore = 0.85
	c.metrics.MoEMetrics.AvgRoutingLatencyMs = 2.3
	
	// Simulate Betti numbers (periodically)
	if c.metrics.CurrentBatch%10 == 0 {
		c.metrics.BettiNumbers.Beta0 = 1
		c.metrics.BettiNumbers.Beta1 = 14 + uint32(c.metrics.CurrentBatch%5)
		c.metrics.BettiNumbers.Beta2 = 0
		c.metrics.BettiNumbers.EulerCharacteristic = int32(c.metrics.BettiNumbers.Beta0) - int32(c.metrics.BettiNumbers.Beta1) + int32(c.metrics.BettiNumbers.Beta2)
	}

	// Graph state
	c.metrics.GraphState.Nodes = c.config.GraphNodes
	c.metrics.GraphState.Edges = c.config.GraphEdges
	c.metrics.GraphState.Topology = "scale_free"

	// Progress
	if c.config.Epochs > 0 {
		c.metrics.TrainingProgress = float32(c.metrics.CurrentEpoch) / float32(c.config.Epochs) * 100.0
	}
	c.metrics.IsRunning = c.state == StateTraining
	c.metrics.StatusMessage = c.state.String()

	// Stream metrics if enabled
	if c.config.EnableWUIStreaming && c.wsClient != nil && c.wsClient.IsConnected() {
		metricsJSON, _ := json.Marshal(c.metrics)
		c.wsClient.Send(map[string]interface{}{
			"type":    "training_metrics_update",
			"metrics": string(metricsJSON),
		})
	}
}

// Pipeline tool handlers for MCP server

func (s *MCPServer) handleInitTrainingPipeline(params json.RawMessage) (interface{}, error) {
	var args struct {
		AcquisitionThreads      int  `json:"acquisition_threads"`
		NumExperts             int  `json:"num_experts"`
		GraphNodes             int  `json:"graph_nodes"`
		EnableBettiGuidance    bool `json:"enable_betti_guidance"`
		EnableKnowledgeEngine  bool `json:"enable_knowledge_engine"`
		Epochs                 int  `json:"epochs"`
		BatchSize              int  `json:"batch_size"`
	}
	if err := json.Unmarshal(params, &args); err != nil {
		return nil, err
	}

	config := DefaultPipelineConfig()
	if args.AcquisitionThreads > 0 {
		config.AcquisitionThreads = args.AcquisitionThreads
	}
	if args.NumExperts > 0 {
		config.NumExperts = args.NumExperts
	}
	if args.GraphNodes > 0 {
		config.GraphNodes = args.GraphNodes
		config.GraphEdges = args.GraphNodes + args.GraphNodes/2
	}
	config.EnableBettiGuidance = args.EnableBettiGuidance
	config.EnableKnowledgeEngine = args.EnableKnowledgeEngine
	if args.Epochs > 0 {
		config.Epochs = args.Epochs
	}
	if args.BatchSize > 0 {
		config.BatchSize = args.BatchSize
	}

	// Create pipeline controller
	pipelineController := NewPipelineController(s.wsClient)
	
	if err := pipelineController.InitializePipeline(config); err != nil {
		return nil, err
	}

	return map[string]interface{}{
		"initialized":             true,
		"state":                   pipelineController.GetState().String(),
		"config":                  config,
		"num_experts":            config.NumExperts,
		"enable_betti_guidance":  config.EnableBettiGuidance,
		"enable_knowledge_engine": config.EnableKnowledgeEngine,
	}, nil
}

func (s *MCPServer) handleSetPipelineConfig(params json.RawMessage) (interface{}, error) {
	var args struct {
		BatchSize              int     `json:"batch_size"`
		LearningRate           float64 `json:"learning_rate"`
		BettiGuidanceThreshold int     `json:"betti_guidance_threshold"`
		EnableWUIStreaming     bool    `json:"enable_wui_streaming"`
	}
	if err := json.Unmarshal(params, &args); err != nil {
		return nil, err
	}

	if s.wsClient != nil && s.wsClient.IsConnected() {
		s.wsClient.Send(map[string]interface{}{
			"type":                     "set_pipeline_config",
			"batch_size":              args.BatchSize,
			"learning_rate":           args.LearningRate,
			"betti_guidance_threshold": args.BettiGuidanceThreshold,
			"enable_wui_streaming":    args.EnableWUIStreaming,
		})
	}

	return map[string]interface{}{
		"config_updated": true,
		"batch_size":     args.BatchSize,
		"learning_rate":  args.LearningRate,
	}, nil
}

func (s *MCPServer) handleGetTrainingMetrics(params json.RawMessage) (interface{}, error) {
	var args struct {
		IncludeBetti       bool `json:"include_betti"`
		IncludeGraphState  bool `json:"include_graph_state"`
		IncludeExpertStats bool `json:"include_expert_stats"`
	}
	if err := json.Unmarshal(params, &args); err != nil {
		// Use defaults
		args.IncludeBetti = true
		args.IncludeGraphState = true
		args.IncludeExpertStats = true
	}

	if s.wsClient != nil && s.wsClient.IsConnected() {
		resp, err := s.wsClient.SendAndWait(
			map[string]interface{}{
				"type":                   "get_training_metrics",
				"include_betti":          args.IncludeBetti,
				"include_graph_state":    args.IncludeGraphState,
				"include_expert_stats":   args.IncludeExpertStats,
			},
			"training_metrics",
			5*time.Second,
		)
		if err != nil {
			return nil, err
		}
		return resp, nil
	}

	return nil, fmt.Errorf("not connected to WUI backend")
}

func (s *MCPServer) handleApplyBettiGuidance(params json.RawMessage) (interface{}, error) {
	var args struct {
		Force bool `json:"force"`
	}
	if err := json.Unmarshal(params, &args); err != nil {
		args.Force = false
	}

	if s.wsClient != nil && s.wsClient.IsConnected() {
		s.wsClient.Send(map[string]interface{}{
			"type":  "apply_betti_guidance",
			"force": args.Force,
		})
		return map[string]interface{}{
			"guidance_applied": true,
			"forced":          args.Force,
		}, nil
	}

	return nil, fmt.Errorf("not connected to WUI backend")
}

func (s *MCPServer) handlePauseTraining(params json.RawMessage) (interface{}, error) {
	if s.wsClient != nil && s.wsClient.IsConnected() {
		s.wsClient.Send(map[string]interface{}{"type": "pause_training"})
		return map[string]interface{}{"paused": true}, nil
	}
	return nil, fmt.Errorf("not connected to WUI backend")
}

func (s *MCPServer) handleResumeTraining(params json.RawMessage) (interface{}, error) {
	if s.wsClient != nil && s.wsClient.IsConnected() {
		s.wsClient.Send(map[string]interface{}{"type": "resume_training"})
		return map[string]interface{}{"resumed": true}, nil
	}
	return nil, fmt.Errorf("not connected to WUI backend")
}

func (s *MCPServer) handleExportModel(params json.RawMessage) (interface{}, error) {
	var args struct {
		Path            string `json:"path"`
		IncludeTopology bool   `json:"include_topology"`
	}
	if err := json.Unmarshal(params, &args); err != nil {
		return nil, err
	}

	if s.wsClient != nil && s.wsClient.IsConnected() {
		s.wsClient.Send(map[string]interface{}{
			"type":              "export_model",
			"path":              args.Path,
			"include_topology":  args.IncludeTopology,
		})
		return map[string]interface{}{
			"exported":         true,
			"path":             args.Path,
			"include_topology": args.IncludeTopology,
		}, nil
	}

	return nil, fmt.Errorf("not connected to WUI backend")
}

func (s *MCPServer) handleImportModel(params json.RawMessage) (interface{}, error) {
	var args struct {
		Path string `json:"path"`
	}
	if err := json.Unmarshal(params, &args); err != nil {
		return nil, err
	}

	if s.wsClient != nil && s.wsClient.IsConnected() {
		s.wsClient.Send(map[string]interface{}{
			"type": "import_model",
			"path": args.Path,
		})
		return map[string]interface{}{
			"imported": true,
			"path":     args.Path,
		}, nil
	}

	return nil, fmt.Errorf("not connected to WUI backend")
}
