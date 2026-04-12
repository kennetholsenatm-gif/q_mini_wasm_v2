package pkg

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"sync"
	"time"
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

// TaskResult represents result of an agent task execution
type TaskResult struct {
	SuccessInt int8                   `json:"success"` // 0/1 instead of bool
	Data       map[string]interface{} `json:"data"`
	Errors     []string               `json:"errors"`
	Metrics    map[string]int64       `json:"metrics"` // Fixed-point (scale 1000 = 1.0)
	Timestamp  time.Time              `json:"timestamp"`
}

// NewTaskResult creates a new TaskResult with default values
func NewTaskResult() *TaskResult {
	return &TaskResult{
		SuccessInt: 1, // true = 1
		Data:       make(map[string]interface{}),
		Errors:     make([]string, 0),
		Metrics:    make(map[string]int64),
		Timestamp:  time.Now(),
	}
}

// AgentMemory stores agent state and learning data
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

// AgentConfig contains configuration for an agent
type AgentConfig struct {
	Name                      string   `json:"name"`
	Description               string   `json:"description"`
	SystemPrompt              string   `json:"system_prompt"`
	Tools                     []string `json:"tools"`
	MaxIterations             int      `json:"max_iterations"`
	ImprovementCycleFrequency string   `json:"improvement_cycle_frequency"`
	RateLimitRPM              int      `json:"rate_limit_rpm"`
	RateLimitTPM              int      `json:"rate_limit_tpm"`
	CacheTTL                  int      `json:"cache_ttl"`
}

// BaseAgent is the base agent class providing common functionality
type BaseAgent struct {
	Config    *AgentConfig
	State     AgentState
	Memory    *AgentMemory
	StartTime time.Time
	TaskCount int
	mu        sync.Mutex

	// Rate limiting state
	requestTimestamps []time.Time
	tokenUsage        int
	dailyRequests     int

	// Performance tracking (fixed-point)
	performanceMetrics map[string][]int64
}

// NewBaseAgent creates a new BaseAgent
func NewBaseAgent(config *AgentConfig) *BaseAgent {
	// Set defaults
	if config.RateLimitRPM == 0 {
		config.RateLimitRPM = 15 // Gemini free tier
	}
	if config.RateLimitTPM == 0 {
		config.RateLimitTPM = 1_000_000
	}
	if config.MaxIterations == 0 {
		config.MaxIterations = 5
	}

	return &BaseAgent{
		Config:             config,
		State:              AgentStateIdle,
		Memory:             NewAgentMemory(),
		performanceMetrics: make(map[string][]int64),
	}
}

// Name returns the agent name
func (a *BaseAgent) Name() string {
	return a.Config.Name
}

// Uptime returns agent uptime in seconds
func (a *BaseAgent) Uptime() float64 {
	if a.StartTime.IsZero() {
		return 0
	}
	return time.Since(a.StartTime).Seconds()
}

// Initialize initializes the agent
func (a *BaseAgent) Initialize() error {
	a.mu.Lock()
	defer a.mu.Unlock()

	a.State = AgentStateIdle
	a.StartTime = time.Now()

	if err := a.loadMemory(); err != nil {
		return fmt.Errorf("failed to load memory: %w", err)
	}

	return nil
}

// Shutdown shuts down the agent gracefully
func (a *BaseAgent) Shutdown() error {
	a.mu.Lock()
	defer a.mu.Unlock()

	a.State = AgentStateIdle

	if err := a.saveMemory(); err != nil {
		return fmt.Errorf("failed to save memory: %w", err)
	}

	return nil
}

// loadMemory loads agent memory from persistent storage
func (a *BaseAgent) loadMemory() error {
	memoryPath := filepath.Join("agents", "memory", fmt.Sprintf("%s_memory.json", a.Name()))

	if _, err := os.Stat(memoryPath); os.IsNotExist(err) {
		// Memory file doesn't exist yet, that's fine
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
// Returns int8: 1 = allowed, 0 = denied (GF(3) compliant)
func (a *BaseAgent) CheckRateLimit(estimatedTokens int) int8 {
	a.mu.Lock()
	defer a.mu.Unlock()

	now := time.Now()
	oneMinuteAgo := now.Add(-60 * time.Second)

	// Clean old timestamps
	filtered := make([]time.Time, 0, len(a.requestTimestamps))
	for _, ts := range a.requestTimestamps {
		if ts.After(oneMinuteAgo) {
			filtered = append(filtered, ts)
		}
	}
	a.requestTimestamps = filtered

	// Check RPM limit
	if len(a.requestTimestamps) >= a.Config.RateLimitRPM {
		return 0 // false = 0
	}

	// Check TPM limit
	if a.tokenUsage+estimatedTokens > a.Config.RateLimitTPM {
		return 0 // false = 0
	}

	return 1 // true = 1
}

// RecordRequest records a request for rate limiting
func (a *BaseAgent) RecordRequest(tokensUsed int) {
	a.mu.Lock()
	defer a.mu.Unlock()

	a.requestTimestamps = append(a.requestTimestamps, time.Now())
	a.tokenUsage += tokensUsed
	a.dailyRequests++
}

// GetPerformanceSummary returns performance summary for the agent
func (a *BaseAgent) GetPerformanceSummary() map[string]interface{} {
	a.mu.RLock()
	defer a.mu.RUnlock()

	summary := map[string]interface{}{
		"name":                a.Name(),
		"state":               string(a.State),
		"uptime_seconds":      a.Uptime(),
		"total_tasks":         a.TaskCount,
		"daily_requests":      a.dailyRequests,
		"token_usage":         a.tokenUsage,
		"patterns_stored":     len(a.Memory.Patterns),
		"improvements_stored": len(a.Memory.Improvements),
	}

	// Calculate average response time (fixed-point)
	rt, ok := a.performanceMetrics["response_time"]
	if ok && len(rt) > 0 {
		sum := int64(0)
		for _, t := range rt {
			sum += t
		}
		summary["avg_response_time"] = sum / int64(len(rt))
	} else {
		summary["avg_response_time"] = 0
	}

	return summary
}

// ExecuteWithRetry executes a task with retry logic
func (a *BaseAgent) ExecuteWithRetry(taskFunc func() (*TaskResult, error), maxRetries int, delay time.Duration) (*TaskResult, error) {
	if maxRetries <= 0 {
		maxRetries = 3
	}
	if delay <= 0 {
		delay = 1 * time.Second
	}

	var lastError error

	for attempt := 0; attempt < maxRetries; attempt++ {
		result, err := taskFunc()
		if err == nil {
			a.mu.Lock()
			a.TaskCount++
			a.mu.Unlock()
			return result, nil
		}

		lastError = err

		if attempt < maxRetries-1 {
			time.Sleep(delay * time.Duration(attempt+1))
		}
	}

	return nil, fmt.Errorf("task failed after %d attempts: %w", maxRetries, lastError)
}

// Agent interface that all specialized agents must implement
type Agent interface {
	Initialize() error
	Shutdown() error
	ExecuteTask(task map[string]interface{}) (*TaskResult, error)
	AnalyzePerformance() (map[string]interface{}, error)
	SuggestImprovements() ([]map[string]interface{}, error)
}
