package agents

import (
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"sync"
	"time"

	"github.com/rs/zerolog"
	"github.com/rs/zerolog/log"
)

// AgentState represents possible states for an agent
type AgentState string

const (
	AgentStateIdle      AgentState = "idle"
	AgentStateRunning   AgentState = "running"
	AgentStatePaused    AgentState = "paused"
	AgentStateError     AgentState = "error"
	AgentStateCompleted AgentState = "completed"
)

// TaskResult represents the result of an agent task execution
type TaskResult struct {
	Success   bool                   `json:"success"`
	Data      map[string]interface{} `json:"data"`
	Errors    []string               `json:"errors"`
	Metrics   map[string]float64     `json:"metrics"`
	Timestamp time.Time              `json:"timestamp"`
}

// NewTaskResult creates a new TaskResult with default values
func NewTaskResult() *TaskResult {
	return &TaskResult{
		Success:   true,
		Data:      make(map[string]interface{}),
		Errors:    make([]string, 0),
		Metrics:   make(map[string]float64),
		Timestamp: time.Now(),
	}
}

// AgentMemory is the memory system for storing agent state and learning
type AgentMemory struct {
	ShortTerm    map[string]interface{}   `json:"short_term"`
	LongTerm     map[string]interface{}   `json:"long_term"`
	Patterns     []map[string]interface{} `json:"patterns"`
	Improvements []map[string]interface{} `json:"improvements"`
	mu           sync.RWMutex
}

// NewAgentMemory creates a new AgentMemory with default values
func NewAgentMemory() *AgentMemory {
	return &AgentMemory{
		ShortTerm:    make(map[string]interface{}),
		LongTerm:     make(map[string]interface{}),
		Patterns:     make([]map[string]interface{}, 0),
		Improvements: make([]map[string]interface{}, 0),
	}
}

// StorePattern stores a detected pattern
func (m *AgentMemory) StorePattern(pattern map[string]interface{}) {
	m.mu.Lock()
	defer m.mu.Unlock()

	pattern["timestamp"] = time.Now().Format(time.RFC3339)
	m.Patterns = append(m.Patterns, pattern)

	// Keep only last 100 patterns
	if len(m.Patterns) > 100 {
		m.Patterns = m.Patterns[len(m.Patterns)-100:]
	}
}

// StoreImprovement stores an implemented improvement
func (m *AgentMemory) StoreImprovement(improvement map[string]interface{}) {
	m.mu.Lock()
	defer m.mu.Unlock()

	improvement["timestamp"] = time.Now().Format(time.RFC3339)
	m.Improvements = append(m.Improvements, improvement)

	// Keep only last 50 improvements
	if len(m.Improvements) > 50 {
		m.Improvements = m.Improvements[len(m.Improvements)-50:]
	}
}

// GetRecentPatterns returns recent patterns
func (m *AgentMemory) GetRecentPatterns(limit int) []map[string]interface{} {
	m.mu.RLock()
	defer m.mu.RUnlock()

	if limit <= 0 || limit > len(m.Patterns) {
		limit = len(m.Patterns)
	}
	return m.Patterns[len(m.Patterns)-limit:]
}

// GetRecentImprovements returns recent improvements
func (m *AgentMemory) GetRecentImprovements(limit int) []map[string]interface{} {
	m.mu.RLock()
	defer m.mu.RUnlock()

	if limit <= 0 || limit > len(m.Improvements) {
		limit = len(m.Improvements)
	}
	return m.Improvements[len(m.Improvements)-limit:]
}

// AgentConfig is the configuration for an agent
type AgentConfig struct {
	Name                     string   `json:"name"`
	Description              string   `json:"description"`
	SystemPrompt             string   `json:"system_prompt"`
	Tools                    []string `json:"tools"`
	MaxIterations            int      `json:"max_iterations"`
	ImprovementCycleFrequency string  `json:"improvement_cycle_frequency"`
	RateLimitRPM             int      `json:"rate_limit_rpm"`
	RateLimitTPM             int      `json:"rate_limit_tpm"`
	CacheTTL                 int      `json:"cache_ttl"`
}

// BaseAgent provides common functionality for all agents
type BaseAgent struct {
	Config        AgentConfig
	State         AgentState
	Memory        *AgentMemory
	Logger        zerolog.Logger

	startTime          time.Time
	taskCount          int64
	requestTimestamps  []time.Time
	tokenUsage         int64
	dailyRequests      int64
	performanceMetrics map[string][]float64
	mu                 sync.RWMutex
}

// NewBaseAgent creates a new BaseAgent with default values
func NewBaseAgent(config AgentConfig) *BaseAgent {
	logger := log.With().Str("agent", config.Name).Logger()

	return &BaseAgent{
		Config:        config,
		State:         AgentStateIdle,
		Memory:        NewAgentMemory(),
		Logger:        logger,
		performanceMetrics: map[string][]float64{
			"response_time": {},
			"token_usage":   {},
			"success_rate":  {},
		},
		requestTimestamps: make([]time.Time, 0),
	}
}

// Name returns the agent name
func (a *BaseAgent) Name() string {
	return a.Config.Name
}

// Description returns the agent description
func (a *BaseAgent) Description() string {
	return a.Config.Description
}

// Uptime returns the agent uptime in seconds
func (a *BaseAgent) Uptime() float64 {
	a.mu.RLock()
	defer a.mu.RUnlock()
	if a.startTime.IsZero() {
		return 0
	}
	return time.Since(a.startTime).Seconds()
}

// Initialize initializes the agent
func (a *BaseAgent) Initialize() error {
	a.mu.Lock()
	defer a.mu.Unlock()

	a.Logger.Info().Msg("Initializing agent")
	a.State = AgentStateIdle
	a.startTime = time.Now()

	if err := a.loadMemory(); err != nil {
		a.Logger.Error().Err(err).Msg("Failed to load memory")
		return err
	}

	a.Logger.Info().Msg("Agent initialized successfully")
	return nil
}

// Shutdown shuts down the agent gracefully
func (a *BaseAgent) Shutdown() error {
	a.mu.Lock()
	defer a.mu.Unlock()

	a.Logger.Info().Msg("Shutting down agent")
	a.State = AgentStateIdle

	if err := a.saveMemory(); err != nil {
		a.Logger.Error().Err(err).Msg("Failed to save memory")
		return err
	}

	a.Logger.Info().Msg("Agent shut down successfully")
	return nil
}

// loadMemory loads agent memory from persistent storage
func (a *BaseAgent) loadMemory() error {
	memoryPath := filepath.Join("agents", "memory", fmt.Sprintf("%s_memory.json", a.Name()))
	
	if _, err := os.Stat(memoryPath); os.IsNotExist(err) {
		return nil
	}

	data, err := os.ReadFile(memoryPath)
	if err != nil {
		return err
	}

	return json.Unmarshal(data, a.Memory)
}

// saveMemory saves agent memory to persistent storage
func (a *BaseAgent) saveMemory() error {
	memoryDir := filepath.Join("agents", "memory")
	if err := os.MkdirAll(memoryDir, 0755); err != nil {
		return err
	}

	memoryPath := filepath.Join(memoryDir, fmt.Sprintf("%s_memory.json", a.Name()))
	
	data, err := json.MarshalIndent(a.Memory, "", "  ")
	if err != nil {
		return err
	}

	return os.WriteFile(memoryPath, data, 0644)
}

// CheckRateLimit checks if request is within rate limits
func (a *BaseAgent) CheckRateLimit(estimatedTokens int) bool {
	a.mu.Lock()
	defer a.mu.Unlock()

	now := time.Now()

	// Clean old timestamps (older than 1 minute)
	cutoff := now.Add(-60 * time.Second)
	filtered := make([]time.Time, 0)
	for _, ts := range a.requestTimestamps {
		if ts.After(cutoff) {
			filtered = append(filtered, ts)
		}
	}
	a.requestTimestamps = filtered

	// Check RPM limit
	if len(a.requestTimestamps) >= a.Config.RateLimitRPM {
		a.Logger.Warn().
			Int("current", len(a.requestTimestamps)).
			Int("limit", a.Config.RateLimitRPM).
			Msg("RPM limit reached")
		return false
	}

	// Check TPM limit
	if a.tokenUsage+int64(estimatedTokens) > int64(a.Config.RateLimitTPM) {
		a.Logger.Warn().
			Int64("current", a.tokenUsage).
			Int("limit", a.Config.RateLimitTPM).
			Msg("TPM limit approaching")
		return false
	}

	return true
}

// RecordRequest records a request for rate limiting
func (a *BaseAgent) RecordRequest(tokensUsed int) {
	a.mu.Lock()
	defer a.mu.Unlock()

	a.requestTimestamps = append(a.requestTimestamps, time.Now())
	a.tokenUsage += int64(tokensUsed)
	a.dailyRequests++
}

// ExecuteTask is the base implementation that can be overridden by specific agents
func (a *BaseAgent) ExecuteTask(task map[string]interface{}) (*TaskResult, error) {
	a.mu.Lock()
	defer a.mu.Unlock()

	a.State = AgentStateRunning
	a.taskCount++
	result := NewTaskResult()

	a.Logger.Debug().Interface("task", task).Msg("Executing task")

	// Base implementation - override in specific agents
	result.Success = true
	result.Data["status"] = "executed"
	result.Metrics["execution_time"] = 0.0

	a.State = AgentStateIdle
	return result, nil
}

// AnalyzePerformance returns performance analysis for the agent
func (a *BaseAgent) AnalyzePerformance() (map[string]interface{}, error) {
	a.mu.RLock()
	defer a.mu.RUnlock()

	analysis := make(map[string]interface{})
	analysis["uptime_seconds"] = a.Uptime()
	analysis["task_count"] = a.taskCount
	analysis["daily_requests"] = a.dailyRequests
	analysis["token_usage_total"] = a.tokenUsage

	// Calculate average metrics
	for metric, values := range a.performanceMetrics {
		if len(values) == 0 {
			analysis[metric+"_avg"] = 0.0
			continue
		}
		sum := 0.0
		for _, v := range values {
			sum += v
		}
		analysis[metric+"_avg"] = sum / float64(len(values))
	}

	return analysis, nil
}

// SuggestImprovements returns suggested improvements based on performance
func (a *BaseAgent) SuggestImprovements() ([]map[string]interface{}, error) {
	a.mu.RLock()
	defer a.mu.RUnlock()

	improvements := make([]map[string]interface{}, 0)

	// Base improvement logic
	if a.taskCount > 100 {
		improvements = append(improvements, map[string]interface{}{
			"type":        "performance",
			"suggestion":  "Consider caching frequent operations",
			"confidence":  0.75,
		})
	}

	return improvements, nil
}

// RecordPerformanceMetric records a performance metric
func (a *BaseAgent) RecordPerformanceMetric(name string, value float64) {
	a.mu.Lock()
	defer a.mu.Unlock()

	if _, exists := a.performanceMetrics[name]; !exists {
		a.performanceMetrics[name] = make([]float64, 0)
	}

	a.performanceMetrics[name] = append(a.performanceMetrics[name], value)

	// Keep last 1000 metrics
	if len(a.performanceMetrics[name]) > 1000 {
		a.performanceMetrics[name] = a.performanceMetrics[name][len(a.performanceMetrics[name])-1000:]
	}
}

// AgentInterface defines the interface that all agents must implement
type AgentInterface interface {
	Initialize() error
	Shutdown() error
	ExecuteTask(task map[string]interface{}) (*TaskResult, error)
	AnalyzePerformance() (map[string]interface{}, error)
	SuggestImprovements() ([]map[string]interface{}, error)
}
