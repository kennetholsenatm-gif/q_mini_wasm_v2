package pkg

import (
	"encoding/json"
	"net/http"
	"sync"

	"github.com/gorilla/mux"
	"github.com/rs/cors"
)

// APIServer provides HTTP API for agent integration
type APIServer struct {
	kanbanAgent *KanbanReviewFixAgent
	agentLock   sync.Mutex
	config      *AgentConfig
}

// NewAPIServer creates a new APIServer
func NewAPIServer(config *AgentConfig) *APIServer {
	return &APIServer{
		config: config,
	}
}

// getKanbanAgent returns singleton kanban agent instance
func (s *APIServer) getKanbanAgent() *KanbanReviewFixAgent {
	s.agentLock.Lock()
	defer s.agentLock.Unlock()

	if s.kanbanAgent == nil {
		s.kanbanAgent = NewKanbanReviewFixAgent(s.config, "config/kanban-config.json")
	}

	return s.kanbanAgent
}

// HealthHandler returns server health status
func (s *APIServer) HealthHandler(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(map[string]string{
		"status": "healthy",
	})
}

// ScanHandler runs kanban scan operation
func (s *APIServer) ScanHandler(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "application/json")

	agent := s.getKanbanAgent()

	if err := agent.Initialize(); err != nil {
		http.Error(w, err.Error(), http.StatusInternalServerError)
		return
	}

	result, err := agent.ExecuteTask(map[string]interface{}{
		"action": "scan",
	})

	agent.Shutdown()

	if err != nil {
		w.WriteHeader(http.StatusInternalServerError)
		json.NewEncoder(w).Encode(map[string]interface{}{
			"success": false,
			"error":   err.Error(),
		})
		return
	}

	json.NewEncoder(w).Encode(map[string]interface{}{
		"success": result.Success,
		"data":    result.Data,
		"errors":  result.Errors,
	})
}

// FixHandler runs kanban fix operation
func (s *APIServer) FixHandler(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "application/json")

	var data map[string]interface{}
	if err := json.NewDecoder(r.Body).Decode(&data); err != nil {
		data = make(map[string]interface{})
	}

	agent := s.getKanbanAgent()

	if err := agent.Initialize(); err != nil {
		http.Error(w, err.Error(), http.StatusInternalServerError)
		return
	}

	task := map[string]interface{}{
		"action": "fix",
	}

	if cardID, ok := data["card_id"].(string); ok && cardID != "" {
		task["card_id"] = cardID
	}

	result, err := agent.ExecuteTask(task)

	agent.Shutdown()

	if err != nil {
		w.WriteHeader(http.StatusInternalServerError)
		json.NewEncoder(w).Encode(map[string]interface{}{
			"success": false,
			"error":   err.Error(),
		})
		return
	}

	json.NewEncoder(w).Encode(map[string]interface{}{
		"success": result.Success,
		"data":    result.Data,
		"errors":  result.Errors,
	})
}

// ReportHandler generates kanban report
func (s *APIServer) ReportHandler(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "application/json")

	agent := s.getKanbanAgent()

	if err := agent.Initialize(); err != nil {
		http.Error(w, err.Error(), http.StatusInternalServerError)
		return
	}

	result, err := agent.ExecuteTask(map[string]interface{}{
		"action": "report",
	})

	agent.Shutdown()

	if err != nil {
		w.WriteHeader(http.StatusInternalServerError)
		json.NewEncoder(w).Encode(map[string]interface{}{
			"success": false,
			"error":   err.Error(),
		})
		return
	}

	json.NewEncoder(w).Encode(map[string]interface{}{
		"success": result.Success,
		"data":    result.Data,
		"errors":  result.Errors,
	})
}

// Start starts the API server on specified address
func (s *APIServer) Start(addr string) error {
	r := mux.NewRouter()

	// API routes
	r.HandleFunc("/api/v1/health", s.HealthHandler).Methods("GET")
	r.HandleFunc("/api/v1/kanban/scan", s.ScanHandler).Methods("POST")
	r.HandleFunc("/api/v1/kanban/fix", s.FixHandler).Methods("POST")
	r.HandleFunc("/api/v1/kanban/report", s.ReportHandler).Methods("POST")

	// CORS configuration
	c := cors.New(cors.Options{
		AllowedOrigins:   []string{"*"},
		AllowedMethods:   []string{"GET", "POST", "OPTIONS"},
		AllowedHeaders:   []string{"Content-Type"},
		AllowCredentials: true,
	})

	handler := c.Handler(r)

	return http.ListenAndServe(addr, handler)
}