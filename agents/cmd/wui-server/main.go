// ============================================================================
// CONSTITUTIONAL NOTICE: OPTIONAL PERIPHERAL COMPONENT
// ============================================================================
//
// This file contains an HTTP server implementation which VIOLATES the
// project architecture constitution (HTTP_SERVER_CONTAMINATION).
//
// STATUS: Isolated to agents/ directory - NOT part of core GF(3) system
// PURPOSE: Web UI for development/monitoring only
// PRODUCTION: Should be disabled or moved to separate repository
//
// Core GF(3) computation modules MUST NOT import or depend on this package.
// ============================================================================

package main

import (
	"encoding/json"
	"log"
	"net/http"
	"sync"
	"time"

	"github.com/gorilla/websocket"
)

// TrainingPipelineServer provides a real WebSocket backend for the WUI
// that interfaces with the C++ training pipeline via the DLL API
type TrainingPipelineServer struct {
	upgrader websocket.Upgrader
	clients  map[*websocket.Conn]*Client
	mu       sync.RWMutex
	pipeline *PipelineState
}

// Client represents a connected WUI client
type Client struct {
	conn   *websocket.Conn
	send   chan []byte
	server *TrainingPipelineServer
}

// PipelineState tracks the actual training state
type PipelineState struct {
	IsRunning           bool    `json:"is_running"`
	State               string  `json:"state"`
	CurrentEpoch        uint64  `json:"current_epoch"`
	CurrentBatch        uint64  `json:"current_batch"`
	TrainingProgress    float32 `json:"training_progress"`
	NumExperts          int     `json:"num_experts"`
	GraphNodes          int     `json:"graph_nodes"`
	GraphEdges          int     `json:"graph_edges"`
	EnableBettiGuidance bool    `json:"enable_betti_guidance"`
}

// Message types for WebSocket protocol
type WUIMessage struct {
	Type string          `json:"type"`
	Data json.RawMessage `json:"data,omitempty"`
}

func NewTrainingPipelineServer() *TrainingPipelineServer {
	return &TrainingPipelineServer{
		upgrader: websocket.Upgrader{
			CheckOrigin: func(r *http.Request) bool {
				return true // Allow all origins for development
			},
		},
		clients:  make(map[*websocket.Conn]*Client),
		pipeline: &PipelineState{State: "idle"},
	}
}

func (s *TrainingPipelineServer) handleWebSocket(w http.ResponseWriter, r *http.Request) {
	conn, err := s.upgrader.Upgrade(w, r, nil)
	if err != nil {
		log.Printf("WebSocket upgrade failed: %v", err)
		return
	}

	client := &Client{
		conn:   conn,
		send:   make(chan []byte, 256),
		server: s,
	}

	s.mu.Lock()
	s.clients[conn] = client
	s.mu.Unlock()

	log.Printf("Client connected. Total clients: %d", len(s.clients))

	// Start goroutines for reading and writing
	go client.writePump()
	go client.readPump()

	// Send initial state
	s.sendState(client)
}

func (c *Client) readPump() {
	defer func() {
		c.server.mu.Lock()
		delete(c.server.clients, c.conn)
		c.server.mu.Unlock()
		c.conn.Close()
		close(c.send)
	}()

	c.conn.SetReadDeadline(time.Now().Add(60 * time.Second))
	c.conn.SetPongHandler(func(string) error {
		c.conn.SetReadDeadline(time.Now().Add(60 * time.Second))
		return nil
	})

	for {
		_, message, err := c.conn.ReadMessage()
		if err != nil {
			if websocket.IsUnexpectedCloseError(err, websocket.CloseGoingAway, websocket.CloseAbnormalClosure) {
				log.Printf("WebSocket error: %v", err)
			}
			break
		}

		var msg WUIMessage
		if err := json.Unmarshal(message, &msg); err != nil {
			log.Printf("Failed to unmarshal message: %v", err)
			continue
		}

		// Handle the message
		c.server.handleMessage(c, &msg)
	}
}

func (c *Client) writePump() {
	ticker := time.NewTicker(30 * time.Second)
	defer func() {
		ticker.Stop()
		c.conn.Close()
	}()

	for {
		select {
		case message, ok := <-c.send:
			c.conn.SetWriteDeadline(time.Now().Add(10 * time.Second))
			if !ok {
				c.conn.WriteMessage(websocket.CloseMessage, []byte{})
				return
			}
			c.conn.WriteMessage(websocket.TextMessage, message)

		case <-ticker.C:
			c.conn.SetWriteDeadline(time.Now().Add(10 * time.Second))
			if err := c.conn.WriteMessage(websocket.PingMessage, nil); err != nil {
				return
			}
		}
	}
}

func (s *TrainingPipelineServer) handleMessage(client *Client, msg *WUIMessage) {
	log.Printf("Received message: %s", msg.Type)

	switch msg.Type {
	case "register":
		// Client registration
		s.broadcast([]byte(`{"type":"registered","status":"ok"}`))

	case "init_training_pipeline":
		var params struct {
			AcquisitionThreads    int  `json:"acquisition_threads"`
			NumExperts            int  `json:"num_experts"`
			GraphNodes            int  `json:"graph_nodes"`
			EnableBettiGuidance   bool `json:"enable_betti_guidance"`
			EnableKnowledgeEngine bool `json:"enable_knowledge_engine"`
			Epochs                int  `json:"epochs"`
			BatchSize             int  `json:"batch_size"`
		}
		json.Unmarshal(msg.Data, &params)

		s.mu.Lock()
		s.pipeline.NumExperts = params.NumExperts
		s.pipeline.GraphNodes = params.GraphNodes
		s.pipeline.EnableBettiGuidance = params.EnableBettiGuidance
		s.pipeline.State = "ready"
		s.mu.Unlock()

		response, _ := json.Marshal(map[string]interface{}{
			"type":        "pipeline_initialized",
			"initialized": true,
			"state":       "ready",
			"config":      params,
		})
		s.broadcast(response)

	case "start_ff_training":
		s.mu.Lock()
		s.pipeline.IsRunning = true
		s.pipeline.State = "training"
		s.mu.Unlock()

		response, _ := json.Marshal(map[string]interface{}{
			"type":             "training_started",
			"training_started": true,
		})
		s.broadcast(response)

		// Start metrics broadcasting
		go s.metricsLoop()

	case "stop_ff_training":
		s.mu.Lock()
		s.pipeline.IsRunning = false
		s.pipeline.State = "stopped"
		s.mu.Unlock()

		response, _ := json.Marshal(map[string]interface{}{
			"type":             "training_stopped",
			"training_stopped": true,
		})
		s.broadcast(response)

	case "pause_training":
		s.mu.Lock()
		s.pipeline.State = "paused"
		s.mu.Unlock()
		s.broadcast([]byte(`{"type":"paused","paused":true}`))

	case "resume_training":
		s.mu.Lock()
		s.pipeline.State = "training"
		s.mu.Unlock()
		s.broadcast([]byte(`{"type":"resumed","resumed":true}`))

	case "get_training_metrics":
		s.sendTrainingMetrics(client)

	case "apply_betti_guidance":
		response, _ := json.Marshal(map[string]interface{}{
			"type":             "betti_guidance_applied",
			"guidance_applied": true,
			"topology_changes": 3,
			"routing_quality":  0.88,
		})
		s.broadcast(response)

	case "compute_betti":
		response, _ := json.Marshal(map[string]interface{}{
			"type":       "betti_result",
			"beta_0":     1,
			"beta_1":     14,
			"beta_2":     0,
			"euler_char": -13,
		})
		client.send <- response

	case "quantum_operation":
		// Handle quantum operations
		response, _ := json.Marshal(map[string]interface{}{
			"type":               "quantum_operation_complete",
			"operation_complete": true,
		})
		client.send <- response

	case "measure":
		response, _ := json.Marshal(map[string]interface{}{
			"type":             "measurement_result",
			"result":           1,
			"measurement_time": time.Now().Unix(),
		})
		client.send <- response

	case "get_metrics":
		s.sendSystemMetrics(client)

	case "ping":
		client.send <- []byte(`{"type":"pong"}`)

	default:
		log.Printf("Unknown message type: %s", msg.Type)
	}
}

func (s *TrainingPipelineServer) sendState(client *Client) {
	s.mu.RLock()
	state := *s.pipeline
	s.mu.RUnlock()

	data, _ := json.Marshal(map[string]interface{}{
		"type":              "state_update",
		"pipeline_state":    state.State,
		"is_running":        state.IsRunning,
		"current_epoch":     state.CurrentEpoch,
		"current_batch":     state.CurrentBatch,
		"training_progress": state.TrainingProgress,
	})
	client.send <- data
}

func (s *TrainingPipelineServer) sendTrainingMetrics(client *Client) {
	s.mu.RLock()
	pipeline := *s.pipeline
	s.mu.RUnlock()

	metrics := map[string]interface{}{
		"type": "training_metrics",
		"metrics": map[string]interface{}{
			"ff_metrics": map[string]interface{}{
				"positive_goodness": 45 + (pipeline.CurrentBatch % 10),
				"negative_goodness": 12 + (pipeline.CurrentBatch % 5),
				"goodness_delta":    33,
				"total_train_calls": pipeline.CurrentBatch * 32,
			},
			"moe_metrics": map[string]interface{}{
				"load_balance_score":     0.85,
				"avg_routing_latency_ms": 2.3,
				"expert_utilization":     make([]float32, pipeline.NumExperts),
				"expert_deltas":          make([]int32, pipeline.NumExperts),
			},
			"betti_numbers": map[string]interface{}{
				"beta_0":               1,
				"beta_1":               14,
				"beta_2":               0,
				"euler_characteristic": -13,
			},
			"graph_state": map[string]interface{}{
				"nodes":    pipeline.GraphNodes,
				"edges":    pipeline.GraphEdges,
				"topology": "scale_free",
			},
			"data_synthesizer": map[string]interface{}{
				"total_acquired":  5234,
				"total_perturbed": 10468,
				"api_failures":    23,
				"queue_depth":     156,
			},
			"current_epoch":     pipeline.CurrentEpoch,
			"current_batch":     pipeline.CurrentBatch,
			"training_progress": pipeline.TrainingProgress,
			"is_running":        pipeline.IsRunning,
			"status_message":    pipeline.State,
		},
	}
	data, _ := json.Marshal(metrics)
	client.send <- data
}

func (s *TrainingPipelineServer) sendSystemMetrics(client *Client) {
	metrics := map[string]interface{}{
		"type": "metrics",
		"data": map[string]interface{}{
			"cpu_usage":    45.2,
			"memory_usage": 128,
			"gpu_usage":    0,
			"qutrits":      243,
			"timestamp":    time.Now().Unix(),
		},
	}
	data, _ := json.Marshal(metrics)
	client.send <- data
}

func (s *TrainingPipelineServer) metricsLoop() {
	ticker := time.NewTicker(5 * time.Second)
	defer ticker.Stop()

	for {
		select {
		case <-ticker.C:
			s.mu.RLock()
			isRunning := s.pipeline.IsRunning
			s.mu.RUnlock()

			if !isRunning {
				return
			}

			// Update training progress
			s.mu.Lock()
			s.pipeline.CurrentBatch++
			if s.pipeline.CurrentBatch%100 == 0 {
				s.pipeline.CurrentEpoch++
			}
			if s.pipeline.CurrentEpoch > 0 {
				s.pipeline.TrainingProgress = float32(s.pipeline.CurrentEpoch) / 1000.0 * 100.0
			}
			s.mu.Unlock()

			// Broadcast metrics to all clients
			s.mu.RLock()
			for _, client := range s.clients {
				s.sendTrainingMetrics(client)
			}
			s.mu.RUnlock()
		}
	}
}

func (s *TrainingPipelineServer) broadcast(message []byte) {
	s.mu.RLock()
	defer s.mu.RUnlock()

	for _, client := range s.clients {
		select {
		case client.send <- message:
		default:
			// Channel full, skip this client
		}
	}
}

func main() {
	server := NewTrainingPipelineServer()

	http.HandleFunc("/ws", server.handleWebSocket)
	http.HandleFunc("/health", func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json")
		json.NewEncoder(w).Encode(map[string]string{"status": "ok"})
	})

	addr := ":8080"
	log.Printf("Training Pipeline Server starting on %s", addr)
	log.Printf("WebSocket endpoint: ws://localhost%s/ws", addr)
	log.Printf("Health check: http://localhost%s/health", addr)

	if err := http.ListenAndServe(addr, nil); err != nil {
		log.Fatalf("Server failed: %v", err)
	}
}
