package pkg

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"sync"
	"time"
)

// CyclePhase represents phases of the improvement cycle
type CyclePhase string

const (
	CyclePhaseAnalysis     CyclePhase = "analysis"
	CyclePhasePlanning     CyclePhase = "planning"
	CyclePhaseImplementation CyclePhase = "implementation"
	CyclePhaseValidation   CyclePhase = "validation"
	CyclePhaseDeployment   CyclePhase = "deployment"
	CyclePhaseCompleted    CyclePhase = "completed"
	CyclePhaseFailed       CyclePhase = "failed"
)

// ImprovementStatus contains status of an improvement cycle
type ImprovementStatus struct {
	CycleID                string                 `json:"cycle_id"`
	Phase                  CyclePhase             `json:"phase"`
	StartedAt              time.Time              `json:"started_at"`
	CompletedAt            time.Time              `json:"completed_at,omitempty"`
	PatternsIdentified     int                    `json:"patterns_identified"`
	ImprovementsPlanned    int                    `json:"improvements_planned"`
	ImprovementsImplemented int                   `json:"improvements_implemented"`
	ImprovementsValidated  int                    `json:"improvements_validated"`
	SuccessRate            float64                `json:"success_rate"`
	Errors                 []string               `json:"errors"`
}

// ImprovementPlan contains plan for implementing improvements
type ImprovementPlan struct {
	PlanID         string                   `json:"plan_id"`
	Patterns       []map[string]interface{} `json:"patterns"`
	Improvements   []map[string]interface{} `json:"improvements"`
	PriorityOrder  []string                 `json:"priority_order"`
	EstimatedEffort map[string]float64      `json:"estimated_effort"`
	Risks          []map[string]interface{} `json:"risks"`
	SuccessCriteria map[string]interface{}  `json:"success_criteria"`
	CreatedAt      time.Time                `json:"created_at"`
}

// PhaseCriteria defines success criteria for each cycle phase
type PhaseCriteria struct {
	MinPatterns           int     `json:"min_patterns"`
	ConfidenceThreshold   float64 `json:"confidence_threshold"`
	PlanCompleteness      float64 `json:"plan_completeness"`
	RiskAssessment        bool    `json:"risk_assessment"`
	CodeQuality           float64 `json:"code_quality"`
	TestCoverage          float64 `json:"test_coverage"`
	AllTestsPass          bool    `json:"all_tests_pass"`
	PerformanceImprovement float64 `json:"performance_improvement"`
	DeploymentSuccess     bool    `json:"deployment_success"`
	RollbackPlan          bool    `json:"rollback_plan"`
}

// ImprovementCycle orchestrates the auto-improvement cycle
type ImprovementCycle struct {
	Config        map[string]interface{}
	Agents        map[string]Agent
	CycleHistory  []ImprovementStatus
	CurrentCycle  *ImprovementStatus
	mu            sync.Mutex
	running       bool

	phaseCriteria map[CyclePhase]PhaseCriteria
}

// NewImprovementCycle creates a new ImprovementCycle
func NewImprovementCycle(configPath string) (*ImprovementCycle, error) {
	cycle := &ImprovementCycle{
		Agents:       make(map[string]Agent),
		CycleHistory: make([]ImprovementStatus, 0),
		phaseCriteria: map[CyclePhase]PhaseCriteria{
			CyclePhaseAnalysis: {
				MinPatterns:         3,
				ConfidenceThreshold: 0.8,
			},
			CyclePhasePlanning: {
				PlanCompleteness: 0.9,
				RiskAssessment:   true,
			},
			CyclePhaseImplementation: {
				CodeQuality:  0.85,
				TestCoverage: 0.8,
			},
			CyclePhaseValidation: {
				AllTestsPass:          true,
				PerformanceImprovement: 0.1,
			},
			CyclePhaseDeployment: {
				DeploymentSuccess: true,
				RollbackPlan:      true,
			},
		},
	}

	// Load configuration
	if err := cycle.loadConfig(configPath); err != nil {
		return nil, err
	}

	return cycle, nil
}

// loadConfig loads configuration from file
func (c *ImprovementCycle) loadConfig(configPath string) error {
	// Try provided path first
	if configPath != "" {
		data, err := os.ReadFile(configPath)
		if err == nil {
			return json.Unmarshal(data, &c.Config)
		}
	}

	// Try default path
	defaultPath := filepath.Join("agents", "config.json")
	data, err := os.ReadFile(defaultPath)
	if err == nil {
		return json.Unmarshal(data, &c.Config)
	}

	// Use minimal default config
	c.Config = map[string]interface{}{
		"gemini": map[string]interface{}{
			"model": "gemini-3-flash-preview",
			"rate_limits": map[string]interface{}{
				"rpm": 15,
				"tpm": 1000000,
				"rpd": 1500,
			},
		},
	}

	return nil
}

// Initialize initializes the improvement cycle
func (c *ImprovementCycle) Initialize() error {
	c.mu.Lock()
	defer c.mu.Unlock()

	// Initialize LLM service and agents will be implemented in separate modules

	return nil
}

// RunCycle runs a complete improvement cycle
func (c *ImprovementCycle) RunCycle() (*ImprovementStatus, error) {
	c.mu.Lock()
	if c.running {
		c.mu.Unlock()
		return nil, fmt.Errorf("improvement cycle is already running")
	}
	c.running = true
	c.mu.Unlock()

	defer func() {
		c.mu.Lock()
		c.running = false
		c.mu.Unlock()
	}()

	cycleID := fmt.Sprintf("cycle_%s", time.Now().Format("20060102_150405"))

	c.CurrentCycle = &ImprovementStatus{
		CycleID:   cycleID,
		Phase:     CyclePhaseAnalysis,
		StartedAt: time.Now(),
		Errors:    make([]string, 0),
	}

	var err error

	// Phase 1: Analysis
	if err = c.runAnalysisPhase(); err != nil {
		c.CurrentCycle.Phase = CyclePhaseFailed
		c.CurrentCycle.CompletedAt = time.Now()
		c.CurrentCycle.Errors = append(c.CurrentCycle.Errors, err.Error())
		c.CycleHistory = append(c.CycleHistory, *c.CurrentCycle)
		_ = c.saveCycleHistory()
		return c.CurrentCycle, err
	}

	// Phase 2: Planning
	if err = c.runPlanningPhase(); err != nil {
		c.CurrentCycle.Phase = CyclePhaseFailed
		c.CurrentCycle.CompletedAt = time.Now()
		c.CurrentCycle.Errors = append(c.CurrentCycle.Errors, err.Error())
		c.CycleHistory = append(c.CycleHistory, *c.CurrentCycle)
		_ = c.saveCycleHistory()
		return c.CurrentCycle, err
	}

	// Phase 3: Implementation
	if err = c.runImplementationPhase(); err != nil {
		c.CurrentCycle.Phase = CyclePhaseFailed
		c.CurrentCycle.CompletedAt = time.Now()
		c.CurrentCycle.Errors = append(c.CurrentCycle.Errors, err.Error())
		c.CycleHistory = append(c.CycleHistory, *c.CurrentCycle)
		_ = c.saveCycleHistory()
		return c.CurrentCycle, err
	}

	// Phase 4: Validation
	if err = c.runValidationPhase(); err != nil {
		c.CurrentCycle.Phase = CyclePhaseFailed
		c.CurrentCycle.CompletedAt = time.Now()
		c.CurrentCycle.Errors = append(c.CurrentCycle.Errors, err.Error())
		c.CycleHistory = append(c.CycleHistory, *c.CurrentCycle)
		_ = c.saveCycleHistory()
		return c.CurrentCycle, err
	}

	// Phase 5: Deployment
	if err = c.runDeploymentPhase(); err != nil {
		c.CurrentCycle.Phase = CyclePhaseFailed
		c.CurrentCycle.CompletedAt = time.Now()
		c.CurrentCycle.Errors = append(c.CurrentCycle.Errors, err.Error())
		c.CycleHistory = append(c.CycleHistory, *c.CurrentCycle)
		_ = c.saveCycleHistory()
		return c.CurrentCycle, err
	}

	// Mark as completed
	c.CurrentCycle.Phase = CyclePhaseCompleted
	c.CurrentCycle.CompletedAt = time.Now()

	// Calculate success rate
	if c.CurrentCycle.ImprovementsPlanned > 0 {
		c.CurrentCycle.SuccessRate = float64(c.CurrentCycle.ImprovementsValidated) / float64(c.CurrentCycle.ImprovementsPlanned)
	}

	c.CycleHistory = append(c.CycleHistory, *c.CurrentCycle)
	_ = c.saveCycleHistory()

	return c.CurrentCycle, nil
}

// runAnalysisPhase runs the analysis phase
func (c *ImprovementCycle) runAnalysisPhase() error {
	c.CurrentCycle.Phase = CyclePhaseAnalysis

	// Collect performance data
	performanceData, err := c.collectPerformanceData()
	if err != nil {
		return err
	}

	// Identify patterns
	patterns, err := c.identifyPatterns(performanceData)
	if err != nil {
		return err
	}

	c.CurrentCycle.PatternsIdentified = len(patterns)

	// Check success criteria
	if len(patterns) < c.phaseCriteria[CyclePhaseAnalysis].MinPatterns {
		return fmt.Errorf("insufficient patterns identified: %d", len(patterns))
	}

	return nil
}

// runPlanningPhase runs the planning phase
func (c *ImprovementCycle) runPlanningPhase() error {
	c.CurrentCycle.Phase = CyclePhasePlanning

	// Generate improvement plan
	plan, err := c.generateImprovementPlan()
	if err != nil {
		return err
	}

	c.CurrentCycle.ImprovementsPlanned = len(plan.Improvements)

	return nil
}

// runImplementationPhase runs the implementation phase
func (c *ImprovementCycle) runImplementationPhase() error {
	c.CurrentCycle.Phase = CyclePhaseImplementation

	// Implement improvements
	implemented, err := c.implementImprovements()
	if err != nil {
		return err
	}

	c.CurrentCycle.ImprovementsImplemented = implemented

	return nil
}

// runValidationPhase runs the validation phase
func (c *ImprovementCycle) runValidationPhase() error {
	c.CurrentCycle.Phase = CyclePhaseValidation

	// Validate improvements
	validated, err := c.validateImprovements()
	if err != nil {
		return err
	}

	c.CurrentCycle.ImprovementsValidated = validated

	return nil
}

// runDeploymentPhase runs the deployment phase
func (c *ImprovementCycle) runDeploymentPhase() error {
	c.CurrentCycle.Phase = CyclePhaseDeployment

	// Deploy improvements
	return c.deployImprovements()
}

// collectPerformanceData collects performance data from all agents
func (c *ImprovementCycle) collectPerformanceData() (map[string]interface{}, error) {
	data := map[string]interface{}{
		"timestamp":      time.Now().Format(time.RFC3339),
		"agents":         make(map[string]interface{}),
		"system_metrics": make(map[string]interface{}),
	}

	// Collect from each agent
	for agentType, agent := range c.Agents {
		performance := agent.(*BaseAgent).GetPerformanceSummary()
		data["agents"].(map[string]interface{})[agentType] = performance
	}

	return data, nil
}

// identifyPatterns identifies patterns in performance data
func (c *ImprovementCycle) identifyPatterns(performanceData map[string]interface{}) ([]map[string]interface{}, error) {
	// Pattern identification will be implemented with LLM service
	return []map[string]interface{}{}, nil
}

// generateImprovementPlan generates improvement plan
func (c *ImprovementCycle) generateImprovementPlan() (*ImprovementPlan, error) {
	return &ImprovementPlan{
		PlanID:    fmt.Sprintf("plan_%s", time.Now().Format("20060102_150405")),
		CreatedAt: time.Now(),
	}, nil
}

// implementImprovements implements improvements from the plan
func (c *ImprovementCycle) implementImprovements() (int, error) {
	// Implementation logic will be added
	return 0, nil
}

// validateImprovements validates implemented improvements
func (c *ImprovementCycle) validateImprovements() (int, error) {
	// Validation logic will be added
	return 0, nil
}

// deployImprovements deploys validated improvements
func (c *ImprovementCycle) deployImprovements() error {
	// Deployment logic will be added
	return nil
}

// saveCycleHistory saves cycle history to disk
func (c *ImprovementCycle) saveCycleHistory() error {
	historyPath := filepath.Join("agents", "memory", "cycle_history.json")
	if err := os.MkdirAll(filepath.Dir(historyPath), 0755); err != nil {
		return err
	}

	data, err := json.MarshalIndent(c.CycleHistory, "", "  ")
	if err != nil {
		return err
	}

	return os.WriteFile(historyPath, data, 0644)
}

// GetStatus returns current improvement cycle status
func (c *ImprovementCycle) GetStatus() map[string]interface{} {
	c.mu.RLock()
	defer c.mu.RUnlock()

	status := map[string]interface{}{
		"running":            c.running,
		"total_cycles":       len(c.CycleHistory),
		"agents_initialized": len(c.Agents),
	}

	if c.CurrentCycle != nil {
		status["current_cycle"] = c.CurrentCycle
	}

	successful := 0
	for _, cycle := range c.CycleHistory {
		if cycle.Phase == CyclePhaseCompleted {
			successful++
		}
	}
	status["successful_cycles"] = successful

	return status
}

// Shutdown shuts down the improvement cycle
func (c *ImprovementCycle) Shutdown() error {
	c.mu.Lock()
	defer c.mu.Unlock()

	// Shutdown all agents
	for _, agent := range c.Agents {
		if baseAgent, ok := agent.(*BaseAgent); ok {
			_ = baseAgent.Shutdown()
		}
	}

	c.running = false
	return nil
}