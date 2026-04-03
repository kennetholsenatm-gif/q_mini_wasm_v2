// AgentConfig represents the configuration for an agent
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
	Config            AgentConfig
	State             AgentState
	Memory            *AgentMemory
	Logger            *AgentLogger
	StartTime         *time.Time
	TaskCount         int
	mu                sync.RWMutex
	
	// Rate limiting state
	requestTimestamps []time.Time
	tokenUsage        int
	dailyRequests     int
	
	// Performance tracking
	performanceMetrics map[string][]float64
}

// AgentLogger is a simple logger for agents
type AgentLogger struct {
	agentName string
}

// Info logs an info message
func (l *AgentLogger) Info(msg string, fields ...interface{}) {
	fmt.Printf("[INFO] [%s] %s %v\n", l.agentName, msg, fields)
}

// Warning logs a warning message
func (l *AgentLogger) Warning(msg string, fields ...interface{}) {
	fmt.Printf("[WARN] [%s] %s %v\n", l.agentName, msg, fields)
}

// Error logs an error message
func (l *AgentLogger) Error(msg string, fields ...interface{}) {
	fmt.Printf("[ERROR] [%s] %s %v\n", l.agentName, msg, fields)
}

// NewBaseAgent creates a new BaseAgent instance
func NewBaseAgent(config AgentConfig) *BaseAgent {
	return &BaseAgent{
		Config: config,
		State:  AgentStateIdle,
		Memory: NewAgentMemory(),
		Logger: &AgentLogger{agentName: config.Name},
		performanceMetrics: map[string][]float64{
			"response_time": {},
			"token_usage":    {},
			"success_rate":   {},
		},
	}
}