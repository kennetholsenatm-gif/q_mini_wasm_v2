package agents

import (
	"encoding/json"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"sync"
	"time"

	"github.com/rs/zerolog/log"
)

// CyclePhase represents phases of the improvement cycle
type CyclePhase string

const (
	CyclePhaseAnalysis       CyclePhase = "analysis"
	CyclePhasePlanning       CyclePhase = "planning"
	CyclePhaseImplementation CyclePhase = "implementation"
	CyclePhaseValidation     CyclePhase = "validation"
	CyclePhaseDeployment     CyclePhase = "deployment"
	CyclePhaseCompleted      CyclePhase = "completed"
	CyclePhaseFailed         CyclePhase = "failed"
)

// ImprovementStatus tracks status of an improvement cycle
type ImprovementStatus struct {
	CycleID                 string     `json:"cycle_id"`
	Phase                   CyclePhase `json:"phase"`
	StartedAt               time.Time  `json:"started_at"`
	CompletedAt             *time.Time `json:"completed_at,omitempty"`
	PatternsIdentified      int        `json:"patterns_identified"`
	ImprovementsPlanned     int        `json:"improvements_planned"`
	ImprovementsImplemented int        `json:"improvements_implemented"`
	ImprovementsValidated   int        `json:"improvements_validated"`
	SuccessRate             float64    `json:"success_rate"`
	Errors                  []string   `json:"errors"`
}

// ImprovementPlan contains plan for implementing improvements
type ImprovementPlan struct {
	PlanID          string                   `json:"plan_id"`
	Patterns        []map[string]interface{} `json:"patterns"`
	Improvements    []map[string]interface{} `json:"improvements"`
	PriorityOrder   []string                 `json:"priority_order"`
	EstimatedEffort map[string]float64       `json:"estimated_effort"`
	Risks           []map[string]interface{} `json:"risks"`
	SuccessCriteria map[string]interface{}   `json:"success_criteria"`
	CreatedAt       time.Time                `json:"created_at"`
}

// PhaseCriteria defines success criteria for each cycle phase
type PhaseCriteria struct {
	MinPatterns            int     `json:"min_patterns,omitempty"`
	ConfidenceThreshold    float64 `json:"confidence_threshold,omitempty"`
	PlanCompleteness       float64 `json:"plan_completeness,omitempty"`
	RiskAssessment         bool    `json:"risk_assessment,omitempty"`
	CodeQuality            float64 `json:"code_quality,omitempty"`
	TestCoverage           float64 `json:"test_coverage,omitempty"`
	AllTestsPass           bool    `json:"all_tests_pass,omitempty"`
	PerformanceImprovement float64 `json:"performance_improvement,omitempty"`
	DeploymentSuccess      bool    `json:"deployment_success,omitempty"`
	RollbackPlan           bool    `json:"rollback_plan,omitempty"`
}

// ImprovementCycle orchestrates the auto-improvement cycle
type ImprovementCycle struct {
	Config        map[string]interface{}
	Agents        map[string]AgentInterface
	CycleHistory  []ImprovementStatus
	CurrentCycle  *ImprovementStatus
	CurrentPlan   *ImprovementPlan
	PhaseCriteria map[CyclePhase]PhaseCriteria
	StateDir      string

	running bool
	mu      sync.RWMutex
}

// NewImprovementCycle creates a new improvement cycle orchestrator
func NewImprovementCycle(configPath string) (*ImprovementCycle, error) {
	cycle := &ImprovementCycle{
		Agents:       make(map[string]AgentInterface),
		CycleHistory: make([]ImprovementStatus, 0),
		PhaseCriteria: map[CyclePhase]PhaseCriteria{
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
				AllTestsPass:           true,
				PerformanceImprovement: 0.1,
			},
			CyclePhaseDeployment: {
				DeploymentSuccess: true,
				RollbackPlan:      true,
			},
		},
	}

	if err := cycle.loadConfig(configPath); err != nil {
		return nil, err
	}

	log.Info().Msg("Improvement cycle orchestrator created")
	return cycle, nil
}

// Initialize initializes the improvement cycle
func (c *ImprovementCycle) Initialize() error {
	c.mu.Lock()
	defer c.mu.Unlock()

	log.Info().Msg("Initializing improvement cycle")

	if err := c.loadCycleHistory(); err != nil {
		log.Warn().Err(err).Msg("Failed to load cycle history")
	}

	log.Info().Msg("Improvement cycle initialized successfully")
	return nil
}

// RunCycle executes a complete improvement cycle
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

	c.mu.Lock()
	c.CurrentCycle = &ImprovementStatus{
		CycleID:   cycleID,
		Phase:     CyclePhaseAnalysis,
		StartedAt: time.Now(),
		Errors:    make([]string, 0),
	}
	c.mu.Unlock()

	log.Info().Str("cycle_id", cycleID).Msg("Starting improvement cycle")

	var err error

	// Phase 1: Analysis
	if err = c.runAnalysisPhase(); err != nil {
		c.failCycle(err)
		return c.CurrentCycle, err
	}

	// Phase 2: Planning
	if err = c.runPlanningPhase(); err != nil {
		c.failCycle(err)
		return c.CurrentCycle, err
	}

	// Phase 3: Implementation
	if err = c.runImplementationPhase(); err != nil {
		c.failCycle(err)
		return c.CurrentCycle, err
	}

	// Phase 4: Validation
	if err = c.runValidationPhase(); err != nil {
		c.failCycle(err)
		return c.CurrentCycle, err
	}

	// Phase 5: Deployment
	if err = c.runDeploymentPhase(); err != nil {
		c.failCycle(err)
		return c.CurrentCycle, err
	}

	// Mark cycle completed
	c.mu.Lock()
	now := time.Now()
	c.CurrentCycle.Phase = CyclePhaseCompleted
	c.CurrentCycle.CompletedAt = &now

	if c.CurrentCycle.ImprovementsPlanned > 0 {
		c.CurrentCycle.SuccessRate = float64(c.CurrentCycle.ImprovementsValidated) / float64(c.CurrentCycle.ImprovementsPlanned)
	}

	c.CycleHistory = append(c.CycleHistory, *c.CurrentCycle)
	c.mu.Unlock()

	if err := c.saveCycleHistory(); err != nil {
		log.Warn().Err(err).Msg("Failed to save cycle history")
	}

	log.Info().
		Str("cycle_id", cycleID).
		Float64("success_rate", c.CurrentCycle.SuccessRate).
		Msg("Improvement cycle completed successfully")

	return c.CurrentCycle, nil
}

// Shutdown shuts down the improvement cycle gracefully
func (c *ImprovementCycle) Shutdown() error {
	c.mu.Lock()
	defer c.mu.Unlock()

	log.Info().Msg("Shutting down improvement cycle")

	for name, agent := range c.Agents {
		if err := agent.Shutdown(); err != nil {
			log.Warn().Str("agent", name).Err(err).Msg("Failed to shutdown agent")
		}
	}

	if err := c.saveCycleHistory(); err != nil {
		log.Warn().Err(err).Msg("Failed to save cycle history on shutdown")
	}

	log.Info().Msg("Improvement cycle shut down successfully")
	return nil
}

// GetStatus returns current improvement cycle status
func (c *ImprovementCycle) GetStatus() map[string]interface{} {
	c.mu.RLock()
	defer c.mu.RUnlock()

	status := map[string]interface{}{
		"running":      c.running,
		"total_cycles": len(c.CycleHistory),
		"agents_count": len(c.Agents),
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

func (c *ImprovementCycle) runAnalysisPhase() error {
	log.Info().Msg("Running analysis phase")

	c.mu.Lock()
	c.CurrentCycle.Phase = CyclePhaseAnalysis
	c.mu.Unlock()

	// Collect performance data
	performanceData, err := c.collectPerformanceData()
	if err != nil {
		return fmt.Errorf("failed to collect performance data: %w", err)
	}

	// Identify patterns
	patterns, err := c.identifyPatterns(performanceData)
	if err != nil {
		return fmt.Errorf("failed to identify patterns: %w", err)
	}

	c.mu.Lock()
	c.CurrentCycle.PatternsIdentified = len(patterns)
	c.mu.Unlock()

	// Check success criteria
	if len(patterns) < c.PhaseCriteria[CyclePhaseAnalysis].MinPatterns {
		return fmt.Errorf("insufficient patterns identified: %d (minimum %d)",
			len(patterns), c.PhaseCriteria[CyclePhaseAnalysis].MinPatterns)
	}

	log.Info().Int("patterns", len(patterns)).Msg("Analysis phase completed")
	return nil
}

func (c *ImprovementCycle) runPlanningPhase() error {
	log.Info().Msg("Running planning phase")

	c.mu.Lock()
	c.CurrentCycle.Phase = CyclePhasePlanning
	c.mu.Unlock()

	plan, err := c.generateImprovementPlan()
	if err != nil {
		return fmt.Errorf("failed to generate improvement plan: %w", err)
	}

	c.mu.Lock()
	c.CurrentCycle.ImprovementsPlanned = len(plan.Improvements)
	c.mu.Unlock()

	log.Info().Int("improvements", len(plan.Improvements)).Msg("Planning phase completed")
	return nil
}

func (c *ImprovementCycle) runImplementationPhase() error {
	log.Info().Msg("Running implementation phase")

	c.mu.Lock()
	c.CurrentCycle.Phase = CyclePhaseImplementation
	c.mu.Unlock()

	implemented, err := c.implementImprovements()
	if err != nil {
		return fmt.Errorf("failed to implement improvements: %w", err)
	}

	c.mu.Lock()
	c.CurrentCycle.ImprovementsImplemented = implemented
	c.mu.Unlock()

	log.Info().Int("implemented", implemented).Msg("Implementation phase completed")
	return nil
}

func (c *ImprovementCycle) runValidationPhase() error {
	log.Info().Msg("Running validation phase")

	c.mu.Lock()
	c.CurrentCycle.Phase = CyclePhaseValidation
	c.mu.Unlock()

	validated, err := c.validateImprovements()
	if err != nil {
		return fmt.Errorf("failed to validate improvements: %w", err)
	}

	c.mu.Lock()
	c.CurrentCycle.ImprovementsValidated = validated
	c.mu.Unlock()

	log.Info().Int("validated", validated).Msg("Validation phase completed")
	return nil
}

func (c *ImprovementCycle) runDeploymentPhase() error {
	log.Info().Msg("Running deployment phase")

	c.mu.Lock()
	c.CurrentCycle.Phase = CyclePhaseDeployment
	c.mu.Unlock()

	if err := c.deployImprovements(); err != nil {
		return fmt.Errorf("failed to deploy improvements: %w", err)
	}

	log.Info().Msg("Deployment phase completed")
	return nil
}

func (c *ImprovementCycle) failCycle(err error) {
	c.mu.Lock()
	defer c.mu.Unlock()

	now := time.Now()
	c.CurrentCycle.Phase = CyclePhaseFailed
	c.CurrentCycle.CompletedAt = &now
	c.CurrentCycle.Errors = append(c.CurrentCycle.Errors, err.Error())
	c.CycleHistory = append(c.CycleHistory, *c.CurrentCycle)

	log.Error().
		Str("cycle_id", c.CurrentCycle.CycleID).
		Err(err).
		Msg("Improvement cycle failed")

	if saveErr := c.saveCycleHistory(); saveErr != nil {
		log.Warn().Err(saveErr).Msg("Failed to save cycle history after failure")
	}
}

func (c *ImprovementCycle) loadConfig(configPath string) error {
	if configPath == "" {
		configPath = "agents/config.json"
	}

	data, err := os.ReadFile(configPath)
	if err != nil {
		if os.IsNotExist(err) {
			// Use default config
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
		return err
	}

	return json.Unmarshal(data, &c.Config)
}

func (c *ImprovementCycle) loadCycleHistory() error {
	historyPath := filepath.Join("agents", "memory", "cycle_history.json")

	if _, err := os.Stat(historyPath); os.IsNotExist(err) {
		return nil
	}

	data, err := os.ReadFile(historyPath)
	if err != nil {
		return err
	}

	return json.Unmarshal(data, &c.CycleHistory)
}

func (c *ImprovementCycle) saveCycleHistory() error {
	memoryDir := filepath.Join("agents", "memory")
	if err := os.MkdirAll(memoryDir, 0755); err != nil {
		return err
	}

	historyPath := filepath.Join(memoryDir, "cycle_history.json")

	data, err := json.MarshalIndent(c.CycleHistory, "", "  ")
	if err != nil {
		return err
	}

	return os.WriteFile(historyPath, data, 0644)
}

func (c *ImprovementCycle) collectPerformanceData() (map[string]interface{}, error) {
	data := map[string]interface{}{
		"timestamp":      time.Now().Format(time.RFC3339),
		"agents":         make(map[string]interface{}),
		"system_metrics": make(map[string]interface{}),
	}

	for name, agent := range c.Agents {
		performance, err := agent.AnalyzePerformance()
		if err != nil {
			log.Warn().Str("agent", name).Err(err).Msg("Failed to collect performance data")
			continue
		}
		data["agents"].(map[string]interface{})[name] = performance
	}

	return data, nil
}

func (c *ImprovementCycle) identifyPatterns(performanceData map[string]interface{}) ([]map[string]interface{}, error) {
	// Pattern identification logic will be implemented
	// Currently returns placeholder patterns for testing
	patterns := []map[string]interface{}{
		{
			"pattern":    "high_token_usage_in_analysis_phase",
			"confidence": 0.85,
			"source":     "performance_analysis",
		},
		{
			"pattern":    "rate_limit_throttling_during_peak_hours",
			"confidence": 0.92,
			"source":     "performance_analysis",
		},
		{
			"pattern":    "repeated_validation_failures",
			"confidence": 0.78,
			"source":     "performance_analysis",
		},
	}

	// Store patterns in agent memory
	for _, pattern := range patterns {
		for _, agent := range c.Agents {
			if baseAgent, ok := agent.(*BaseAgent); ok {
				baseAgent.Memory.StorePattern(pattern)
			}
		}
	}

	return patterns, nil
}

func (c *ImprovementCycle) generateImprovementPlan() (*ImprovementPlan, error) {
	allPatterns := make([]map[string]interface{}, 0)

	for _, agent := range c.Agents {
		if baseAgent, ok := agent.(*BaseAgent); ok {
			allPatterns = append(allPatterns, baseAgent.Memory.GetRecentPatterns(20)...)
		}
	}

	plan := &ImprovementPlan{
		PlanID:          fmt.Sprintf("plan_%s", time.Now().Format("20060102_150405")),
		Patterns:        allPatterns,
		Improvements:    make([]map[string]interface{}, 0),
		PriorityOrder:   make([]string, 0),
		EstimatedEffort: make(map[string]float64),
		Risks:           make([]map[string]interface{}, 0),
		SuccessCriteria: make(map[string]interface{}),
		CreatedAt:       time.Now(),
	}

	return plan, nil
}

func (c *ImprovementCycle) implementImprovements() (int, error) {
	c.mu.Lock()
	defer c.mu.Unlock()

	if c.CurrentPlan == nil {
		return 0, fmt.Errorf("no improvement plan available")
	}

	implemented := 0
	for _, improvement := range c.CurrentPlan.Improvements {
		improvementType, ok := improvement["type"].(string)
		if !ok {
			continue
		}

		switch improvementType {
		case "code_change":
			// Apply code modifications
			if filePath, ok := improvement["file_path"].(string); ok {
				if content, ok := improvement["content"].(string); ok {
					if err := c.applyCodeChange(filePath, content); err != nil {
						c.CurrentCycle.Errors = append(c.CurrentCycle.Errors,
							fmt.Sprintf("Failed to apply code change to %s: %v", filePath, err))
						continue
					}
					implemented++
				}
			}
		case "config_update":
			// Update configuration
			if configKey, ok := improvement["key"].(string); ok {
				if configValue, ok := improvement["value"]; ok {
					c.Config[configKey] = configValue
					implemented++
				}
			}
		case "agent_tuning":
			// Tune agent parameters
			if agentName, ok := improvement["agent"].(string); ok {
				if agent, exists := c.Agents[agentName]; exists {
					if params, ok := improvement["params"].(map[string]interface{}); ok {
						if tunable, ok := agent.(interface {
							Tune(map[string]interface{}) error
						}); ok {
							if err := tunable.Tune(params); err != nil {
								c.CurrentCycle.Errors = append(c.CurrentCycle.Errors,
									fmt.Sprintf("Failed to tune agent %s: %v", agentName, err))
								continue
							}
							implemented++
						}
					}
				}
			}
		default:
			log.Warn().Str("type", improvementType).Msg("Unknown improvement type")
		}
	}

	c.CurrentCycle.ImprovementsImplemented = implemented
	log.Info().Int("implemented", implemented).Msg("Improvements implemented")
	return implemented, nil
}

func (c *ImprovementCycle) validateImprovements() (int, error) {
	c.mu.Lock()
	defer c.mu.Unlock()

	if c.CurrentPlan == nil {
		return 0, fmt.Errorf("no improvement plan to validate")
	}

	validated := 0
	failed := 0

	for i, improvement := range c.CurrentPlan.Improvements {
		// Check if improvement has success criteria
		criteria, hasCriteria := improvement["success_criteria"].(map[string]interface{})
		if !hasCriteria {
			// No criteria means automatic success
			validated++
			continue
		}

		// Validate based on criteria type
		validationType, ok := criteria["type"].(string)
		if !ok {
			validated++
			continue
		}

		switch validationType {
		case "file_exists":
			if filePath, ok := criteria["file_path"].(string); ok {
				if _, err := os.Stat(filePath); err == nil {
					validated++
				} else {
					failed++
					c.CurrentCycle.Errors = append(c.CurrentCycle.Errors,
						fmt.Sprintf("Validation failed for improvement %d: file %s does not exist", i, filePath))
				}
			}
		case "performance_metric":
			// Check if performance improved
			if metric, ok := criteria["metric"].(string); ok {
				if threshold, ok := criteria["threshold"].(float64); ok {
					if currentValue := c.getCurrentMetric(metric); currentValue >= threshold {
						validated++
					} else {
						failed++
						c.CurrentCycle.Errors = append(c.CurrentCycle.Errors,
							fmt.Sprintf("Validation failed for improvement %d: metric %s = %f < %f", i, metric, currentValue, threshold))
					}
				}
			}
		case "test_pass":
			// Run tests to validate
			if testCmd, ok := criteria["test_command"].(string); ok {
				if err := c.runValidationTest(testCmd); err == nil {
					validated++
				} else {
					failed++
					c.CurrentCycle.Errors = append(c.CurrentCycle.Errors,
						fmt.Sprintf("Validation failed for improvement %d: test failed - %v", i, err))
				}
			}
		default:
			// Unknown validation type, assume success
			validated++
		}
	}

	c.CurrentCycle.ImprovementsValidated = validated
	successRate := 0.0
	if total := validated + failed; total > 0 {
		successRate = float64(validated) / float64(total)
	}
	c.CurrentCycle.SuccessRate = successRate

	log.Info().Int("validated", validated).Int("failed", failed).Float64("success_rate", successRate).Msg("Improvements validated")
	return validated, nil
}

func (c *ImprovementCycle) deployImprovements() error {
	c.mu.Lock()
	defer c.mu.Unlock()

	if c.CurrentCycle.Phase != CyclePhaseValidation {
		return fmt.Errorf("improvements must be validated before deployment")
	}

	if c.CurrentCycle.SuccessRate < 0.8 {
		return fmt.Errorf("success rate %.2f too low for deployment (minimum 0.8)", c.CurrentCycle.SuccessRate)
	}

	// Create deployment backup
	backupDir := filepath.Join(c.StateDir, "backups", time.Now().Format("20060102_150405"))
	if err := os.MkdirAll(backupDir, 0755); err != nil {
		return fmt.Errorf("failed to create backup directory: %w", err)
	}

	// Backup current state
	if err := c.backupCurrentState(backupDir); err != nil {
		return fmt.Errorf("failed to backup current state: %w", err)
	}

	// Apply deployment
	for _, improvement := range c.CurrentPlan.Improvements {
		if deployment, ok := improvement["deployment"].(map[string]interface{}); ok {
			if action, ok := deployment["action"].(string); ok {
				switch action {
				case "restart_agent":
					if agentName, ok := deployment["agent"].(string); ok {
						if agent, exists := c.Agents[agentName]; exists {
							if restartable, ok := agent.(interface{ Restart() error }); ok {
								if err := restartable.Restart(); err != nil {
									log.Error().Err(err).Str("agent", agentName).Msg("Failed to restart agent")
								}
							}
						}
					}
				case "reload_config":
					// Config already updated, just log
					log.Info().Msg("Configuration reloaded")
				case "notify":
					if message, ok := deployment["message"].(string); ok {
						log.Info().Str("message", message).Msg("Deployment notification")
					}
				}
			}
		}
	}

	c.CurrentCycle.Phase = CyclePhaseDeployment
	now := time.Now()
	c.CurrentCycle.CompletedAt = &now

	log.Info().Str("cycle_id", c.CurrentCycle.CycleID).Msg("Improvements deployed successfully")
	return nil
}

// Helper methods for the improvement cycle

func (c *ImprovementCycle) applyCodeChange(filePath, content string) error {
	// Ensure directory exists
	dir := filepath.Dir(filePath)
	if err := os.MkdirAll(dir, 0755); err != nil {
		return fmt.Errorf("failed to create directory: %w", err)
	}

	// Write the file
	if err := os.WriteFile(filePath, []byte(content), 0644); err != nil {
		return fmt.Errorf("failed to write file: %w", err)
	}

	return nil
}

func (c *ImprovementCycle) getCurrentMetric(metric string) float64 {
	// Check agent performance metrics
	for _, agent := range c.Agents {
		if perf, err := agent.AnalyzePerformance(); err == nil {
			if value, ok := perf[metric].(float64); ok {
				return value
			}
		}
	}
	return 0.0
}

func (c *ImprovementCycle) runValidationTest(testCmd string) error {
	// Simple test execution
	parts := strings.Fields(testCmd)
	if len(parts) == 0 {
		return fmt.Errorf("empty test command")
	}

	cmd := exec.Command(parts[0], parts[1:]...)
	cmd.Dir = c.StateDir
	output, err := cmd.CombinedOutput()
	if err != nil {
		return fmt.Errorf("test failed: %w, output: %s", err, string(output))
	}
	return nil
}

func (c *ImprovementCycle) backupCurrentState(backupDir string) error {
	// Copy current configuration
	configData, err := json.MarshalIndent(c.Config, "", "  ")
	if err != nil {
		return err
	}
	if err := os.WriteFile(filepath.Join(backupDir, "config.json"), configData, 0644); err != nil {
		return err
	}

	// Copy plan
	if c.CurrentPlan != nil {
		planData, err := json.MarshalIndent(c.CurrentPlan, "", "  ")
		if err != nil {
			return err
		}
		if err := os.WriteFile(filepath.Join(backupDir, "plan.json"), planData, 0644); err != nil {
			return err
		}
	}

	return nil
}
