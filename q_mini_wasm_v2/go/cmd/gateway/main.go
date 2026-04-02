// Package main implements the q_mini_wasm_v2 API gateway service
// Provides REST API for orchestration and C++ engine interface
package main

import (
	"context"
	"encoding/json"
	"fmt"
	"log"
	"net/http"
	"os"
	"os/signal"
	"syscall"
	"time"
)

// GatewayConfig holds the gateway service configuration
type GatewayConfig struct {
	Port            int    `json:"port"`
	EngineLibPath   string `json:"engine_lib_path"`
	MaxConnections  int    `json:"max_connections"`
	RequestTimeout  int    `json:"request_timeout_seconds"`
}

// Gateway represents the API gateway service
type Gateway struct {
	config     GatewayConfig
	httpServer *http.Server
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
// Main Entry Point
// ============================================================================

func main() {
	config := GatewayConfig{
		Port:           8080,
		EngineLibPath:  "./libq_mini_wasm_v2_core.so",
		MaxConnections: 100,
		RequestTimeout: 30,
	}
	
	gateway := NewGateway(config)
	
	// Graceful shutdown handling
	sigChan := make(chan os.Signal, 1)
	signal.Notify(sigChan, syscall.SIGINT, syscall.SIGTERM)
	
	go func() {
		if err := gateway.Start(); err != nil && err != http.ErrServerClosed {
			log.Fatalf("Gateway failed: %v", err)
		}
	}()
	
	<-sigChan
	log.Println("Shutting down gateway...")
	
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	
	if err := gateway.Shutdown(ctx); err != nil {
		log.Fatalf("Shutdown failed: %v", err)
	}
	
	log.Println("Gateway stopped")
}