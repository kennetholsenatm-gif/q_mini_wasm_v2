// Package main implements the q_mini_wasm_v2 API gateway service
// Provides REST API for orchestration, C++ engine interface, RAG service, and MCP server
package main

import (
	"context"
	"encoding/json"
	"flag"
	"fmt"
	"log"
	"net/http"
	"os"
	"os/signal"
	"syscall"
	"time"

	"github.com/q_mini_wasm_v2/gateway/pkg/grpc"
	"github.com/q_mini_wasm_v2/gateway/pkg/mcp"
	"github.com/q_mini_wasm_v2/gateway/pkg/rag"
)

// GatewayConfig holds the gateway service configuration
type GatewayConfig struct {
	Port            int    `json:"port"`
	GRPCPort        int    `json:"grpc_port"`
	EngineLibPath   string `json:"engine_lib_path"`
	MaxConnections  int    `json:"max_connections"`
	RequestTimeout  int    `json:"request_timeout_seconds"`
	QdrantHost      string `json:"qdrant_host"`
	QdrantPort      int    `json:"qdrant_port"`
	ProjectRoot     string `json:"project_root"`
}

// Gateway represents the API gateway service
type Gateway struct {
	config     GatewayConfig
	httpServer *http.Server
	grpcServer *grpc.Server
	ragService *rag.Service
}

// NewGateway creates a new gateway instance
func NewGateway(config GatewayConfig) *Gateway {
	return &Gateway{
		config: config,
	}
}

// Start initializes and starts the gateway service
func (g *Gateway) Start() error {
	mux := http.NewServeMux()
	
	// Register API routes
	mux.HandleFunc("/api/v1/health", g.handleHealth)
	mux.HandleFunc("/api/v1/tableau/create", g.handleCreateTableau)
	mux.HandleFunc("/api/v1/tableau/apply", g.handleApplyGate)
	mux.HandleFunc("/api/v1/tableau/measure", g.handleMeasure)
	mux.HandleFunc("/api/v1/moe/route", g.handleMoERoute)
	mux.HandleFunc("/api/v1/ff/train", g.handleForwardForwardTrain)
	mux.HandleFunc("/api/v1/config", g.handleConfig)
	
	g.httpServer = &http.Server{
		Addr:         fmt.Sprintf(":%d", g.config.Port),
		Handler:      mux,
		ReadTimeout:  time.Duration(g.config.RequestTimeout) * time.Second,
		WriteTimeout: time.Duration(g.config.RequestTimeout) * time.Second,
	}
	
	log.Printf("Starting q_mini_wasm_v2 gateway on port %d", g.config.Port)
	return g.httpServer.ListenAndServe()
}

// Shutdown gracefully stops the gateway
func (g *Gateway) Shutdown(ctx context.Context) error {
	return g.httpServer.Shutdown(ctx)
}

// ============================================================================
// API Handlers
// ============================================================================

func (g *Gateway) handleHealth(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		http.Error(w, "Method not allowed", http.StatusMethodNotAllowed)
		return
	}
	
	response := map[string]interface{}{
		"status": "healthy",
		"service": "q_mini_wasm_v2_gateway",
		"version": "1.0.0",
	}
	
	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(response)
}

func (g *Gateway) handleCreateTableau(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, "Method not allowed", http.StatusMethodNotAllowed)
		return
	}
	
	var request struct {
		NumQutrits int `json:"num_qutrits"`
	}
	
	if err := json.NewDecoder(r.Body).Decode(&request); err != nil {
		http.Error(w, "Invalid request body", http.StatusBadRequest)
		return
	}
	
	// TODO: Call C++ engine via CGo or DLL
	response := map[string]interface{}{
		"status": "created",
		"tableau_id": fmt.Sprintf("tableau_%d", time.Now().UnixNano()),
		"num_qutrits": request.NumQutrits,
	}
	
	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(response)
}

func (g *Gateway) handleApplyGate(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, "Method not allowed", http.StatusMethodNotAllowed)
		return
	}
	
	var request struct {
		TableauID string `json:"tableau_id"`
		Gate      string `json:"gate"`       // "hadamard", "phase", "csum"
		Qubit     int    `json:"qubit"`
		Control   int    `json:"control,omitempty"` // For CSUM
		Target    int    `json:"target,omitempty"`  // For CSUM
	}
	
	if err := json.NewDecoder(r.Body).Decode(&request); err != nil {
		http.Error(w, "Invalid request body", http.StatusBadRequest)
		return
	}
	
	// TODO: Call C++ engine via CGo or DLL
	response := map[string]interface{}{
		"status": "applied",
		"gate": request.Gate,
		"tableau_id": request.TableauID,
	}
	
	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(response)
}

func (g *Gateway) handleMeasure(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, "Method not allowed", http.StatusMethodNotAllowed)
		return
	}
	
	var request struct {
		TableauID string `json:"tableau_id"`
		Qubit     int    `json:"qubit"`
	}
	
	if err := json.NewDecoder(r.Body).Decode(&request); err != nil {
		http.Error(w, "Invalid request body", http.StatusBadRequest)
		return
	}
	
	// TODO: Call C++ engine via CGo or DLL
	// Simulate measurement outcome
	outcome := 0 // GF(3) value
	
	response := map[string]interface{}{
		"status": "measured",
		"outcome": outcome,
		"qubit": request.Qubit,
	}
	
	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(response)
}

func (g *Gateway) handleMoERoute(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, "Method not allowed", http.StatusMethodNotAllowed)
		return
	}
	
	var request struct {
		Input       []int `json:"input"`       // Ternary input values
		TopK        int   `json:"top_k"`
		NumExperts  int   `json:"num_experts"`
	}
	
	if err := json.NewDecoder(r.Body).Decode(&request); err != nil {
		http.Error(w, "Invalid request body", http.StatusBadRequest)
		return
	}
	
	// TODO: Call C++ engine via CGo or DLL
	// Simulate expert selection
	selected := []int{0, 1} // Top-K experts
	
	response := map[string]interface{}{
		"status": "routed",
		"selected_experts": selected,
		"input_size": len(request.Input),
	}
	
	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(response)
}

func (g *Gateway) handleForwardForwardTrain(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, "Method not allowed", http.StatusMethodNotAllowed)
		return
	}
	
	var request struct {
		LayerIdx      int     `json:"layer_idx"`
		PositiveData  [][]int `json:"positive_data"`
		NegativeData  [][]int `json:"negative_data"`
		LearningRate  float64 `json:"learning_rate"`
	}
	
	if err := json.NewDecoder(r.Body).Decode(&request); err != nil {
		http.Error(w, "Invalid request body", http.StatusBadRequest)
		return
	}
	
	// TODO: Call C++ engine via CGo or DLL
	response := map[string]interface{}{
		"status": "trained",
		"layer": request.LayerIdx,
		"positive_goodness": 1.5,
		"negative_goodness": 0.5,
		"delta": 1.0,
	}
	
	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(response)
}

func (g *Gateway) handleConfig(w http.ResponseWriter, r *http.Request) {
	switch r.Method {
	case http.MethodGet:
		w.Header().Set("Content-Type", "application/json")
		json.NewEncoder(w).Encode(g.config)
	case http.MethodPut:
		var newConfig GatewayConfig
		if err := json.NewDecoder(r.Body).Decode(&newConfig); err != nil {
			http.Error(w, "Invalid request body", http.StatusBadRequest)
			return
		}
		g.config = newConfig
		w.Header().Set("Content-Type", "application/json")
		json.NewEncoder(w).Encode(map[string]string{"status": "updated"})
	default:
		http.Error(w, "Method not allowed", http.StatusMethodNotAllowed)
	}
}

// ============================================================================
// RAG API Handlers
// ============================================================================

func (g *Gateway) handleRAGRetrieve(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, "Method not allowed", http.StatusMethodNotAllowed)
		return
	}

	var request struct {
		Query       string `json:"query"`
		MaxTokens   int    `json:"max_tokens"`
		ContextType string `json:"context_type"`
	}

	if err := json.NewDecoder(r.Body).Decode(&request); err != nil {
		http.Error(w, "Invalid request body", http.StatusBadRequest)
		return
	}

	if g.ragService == nil {
		http.Error(w, "RAG service not available", http.StatusServiceUnavailable)
		return
	}

	result, err := g.ragService.RetrieveContext(r.Context(), request.Query, request.MaxTokens, request.ContextType)
	if err != nil {
		http.Error(w, fmt.Sprintf("RAG retrieval failed: %v", err), http.StatusInternalServerError)
		return
	}

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(result)
}

func (g *Gateway) handleRAGMetrics(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		http.Error(w, "Method not allowed", http.StatusMethodNotAllowed)
		return
	}

	if g.ragService == nil {
		http.Error(w, "RAG service not available", http.StatusServiceUnavailable)
		return
	}

	metrics := g.ragService.GetMetrics()
	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(metrics)
}

func (g *Gateway) handleRAGIndex(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, "Method not allowed", http.StatusMethodNotAllowed)
		return
	}

	var request struct {
		FilePath string `json:"file_path"`
	}

	if err := json.NewDecoder(r.Body).Decode(&request); err != nil {
		http.Error(w, "Invalid request body", http.StatusBadRequest)
		return
	}

	if g.ragService == nil {
		http.Error(w, "RAG service not available", http.StatusServiceUnavailable)
		return
	}

	if err := g.ragService.IndexDocument(r.Context(), request.FilePath); err != nil {
		http.Error(w, fmt.Sprintf("Indexing failed: %v", err), http.StatusInternalServerError)
		return
	}

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(map[string]string{"status": "indexed", "file": request.FilePath})
}

// ============================================================================
// Main Entry Point
// ============================================================================

func main() {
	// Parse command line flags
	mcpMode := flag.Bool("mcp", false, "Run in MCP server mode (stdio)")
	flag.Parse()

	config := GatewayConfig{
		Port:           8088,  // Changed from 8080 (avoid conflicts)
		GRPCPort:       9090,
		EngineLibPath:  "./libq_mini_wasm_v2_core.so",
		MaxConnections: 100,
		RequestTimeout: 30,
		QdrantHost:     "localhost",
		QdrantPort:     6334,
		ProjectRoot:    ".",
	}

	gateway := NewGateway(config)

	// Initialize RAG service with improved configuration
	ragConfig := rag.ServiceConfig{
		Qdrant: rag.QdrantConfig{
			Host:           config.QdrantHost,
			Port:           config.QdrantPort,
			CollectionName: "qminiwasm_docs",
			VectorSize:     384,
		},
		Chunker: rag.ChunkConfig{
			MaxChunkSize:      512,
			OverlapSize:       50,
			MinChunkSize:      50,
			RespectCodeBlocks: true,
		},
		Scaler: rag.TokenScalerConfig{
			MinTokens:        256,
			MaxTokens:        8192,
			DefaultTokens:    2048,
			ScalingFactor:    1.5,
			ComplexityWeight: 0.6,
			ContextWeight:    0.4,
		},
		HybridSearch: rag.HybridSearchConfig{
			SemanticWeight:  0.7,
			KeywordWeight:   0.3,
			RerankTopK:      20,
			MinScore:        0.3,
		},
		Cache: rag.CacheConfig{
			MaxSize:        1000,
			TTL:            5 * time.Minute,
			EvictionPolicy: "lru",
		},
		ProjectRoot:   config.ProjectRoot,
		AutoIndex:     true,
		EmbeddingType: "placeholder", // Can be changed to "tfidf" or "composite"
	}

	ragService, err := rag.NewService(ragConfig)
	if err != nil {
		log.Printf("Warning: Failed to initialize RAG service: %v", err)
		log.Println("Continuing without RAG service...")
	} else {
		gateway.ragService = ragService

		// Start RAG service
		ctx := context.Background()
		if err := ragService.Start(ctx); err != nil {
			log.Printf("Warning: Failed to start RAG service: %v", err)
		} else {
			log.Println("RAG service started successfully")
		}
	}

	// Check for MCP mode
	if *mcpMode {
		log.Println("Starting in MCP server mode (stdio)")
		mcpServer := mcp.NewServer(ragService)
		if err := mcpServer.Run(context.Background()); err != nil {
			log.Fatalf("MCP server failed: %v", err)
		}
		return
	}

	// Start gRPC server (only in gateway mode)
	if gateway.ragService != nil {
		grpcServer := grpc.NewServer(ragService, config.GRPCPort)
		gateway.grpcServer = grpcServer

		go func() {
			if err := grpcServer.Start(); err != nil {
				log.Printf("gRPC server failed: %v", err)
			}
		}()
	}

	// Register RAG API routes
	mux := http.NewServeMux()
	mux.HandleFunc("/api/v1/health", gateway.handleHealth)
	mux.HandleFunc("/api/v1/tableau/create", gateway.handleCreateTableau)
	mux.HandleFunc("/api/v1/tableau/apply", gateway.handleApplyGate)
	mux.HandleFunc("/api/v1/tableau/measure", gateway.handleMeasure)
	mux.HandleFunc("/api/v1/moe/route", gateway.handleMoERoute)
	mux.HandleFunc("/api/v1/ff/train", gateway.handleForwardForwardTrain)
	mux.HandleFunc("/api/v1/config", gateway.handleConfig)
	mux.HandleFunc("/api/v1/rag/retrieve", gateway.handleRAGRetrieve)
	mux.HandleFunc("/api/v1/rag/metrics", gateway.handleRAGMetrics)
	mux.HandleFunc("/api/v1/rag/index", gateway.handleRAGIndex)

	gateway.httpServer = &http.Server{
		Addr:         fmt.Sprintf(":%d", config.Port),
		Handler:      mux,
		ReadTimeout:  time.Duration(config.RequestTimeout) * time.Second,
		WriteTimeout: time.Duration(config.RequestTimeout) * time.Second,
	}

	// Graceful shutdown handling
	sigChan := make(chan os.Signal, 1)
	signal.Notify(sigChan, syscall.SIGINT, syscall.SIGTERM)

	go func() {
		log.Printf("Starting q_mini_wasm_v2 gateway on port %d (gRPC on %d)", config.Port, config.GRPCPort)
		if err := gateway.httpServer.ListenAndServe(); err != nil && err != http.ErrServerClosed {
			log.Fatalf("Gateway failed: %v", err)
		}
	}()

	<-sigChan
	log.Println("Shutting down gateway...")

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	// Stop RAG service
	if gateway.ragService != nil {
		gateway.ragService.Stop()
	}

	// Stop gRPC server
	if gateway.grpcServer != nil {
		gateway.grpcServer.Stop()
	}

	// Stop HTTP server
	if err := gateway.Shutdown(ctx); err != nil {
		log.Fatalf("Shutdown failed: %v", err)
	}

	log.Println("Gateway stopped")
}
