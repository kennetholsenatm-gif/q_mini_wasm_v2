package agents

import (
	"crypto/sha256"
	"encoding/json"
	"fmt"
	"math"
	"os"
	"path/filepath"
	"sync"
	"time"
)

// ShadowMode represents the operating mode for shadow testing
type ShadowMode string

const (
	ShadowModeDisabled ShadowMode = "disabled"
	ShadowModeShadow   ShadowMode = "shadow"  // Run only, no comparison
	ShadowModeCompare  ShadowMode = "compare" // Run + compare outputs
	ShadowModeCanary   ShadowMode = "canary"  // 5% traffic routing
	ShadowModeFull     ShadowMode = "full"    // 100% traffic
)

// DifferenceType classifies the type of mismatch
type DifferenceType string

const (
	DiffMissingKey    DifferenceType = "missing_key"
	DiffValueMismatch DifferenceType = "value_mismatch"
	DiffExtraKey      DifferenceType = "extra_key"
	DiffTypeMismatch  DifferenceType = "type_mismatch"
	DiffNilValue      DifferenceType = "nil_value"
)

// ClassifiedDifference holds analyzed difference information
type ClassifiedDifference struct {
	Type            DifferenceType `json:"type"`
	Key             string         `json:"key"`
	Expected        interface{}    `json:"expected"`
	Actual          interface{}    `json:"actual"`
	RootCause       string         `json:"root_cause,omitempty"`
	ConfidenceFixed int64          `json:"confidence"` // Fixed-point: 1000 = 1.0
}

// ShadowTestResult stores comparison results between implementations
type ShadowTestResult struct {
	TestID           string                 `json:"test_id"`
	Timestamp        time.Time              `json:"timestamp"`
	AgentType        string                 `json:"agent_type"`
	InputHash        string                 `json:"input_hash"`
	PythonOutput     map[string]interface{} `json:"python_output,omitempty"`
	GoOutput         map[string]interface{} `json:"go_output,omitempty"`
	ParityScoreFixed int64                  `json:"parity_score"` // Fixed-point: 1000 = 1.0
	ExecutionTime    time.Duration          `json:"execution_time"`
	MatchInt         int8                   `json:"match"` // 0/1 instead of bool
	Differences      []string               `json:"differences,omitempty"`
	ClassifiedDiffs  []ClassifiedDifference `json:"classified_diffs,omitempty"`
	Errors           []string               `json:"errors,omitempty"`
	FixedInt         int8                   `json:"fixed"` // 0/1 instead of bool
	FixApplied       string                 `json:"fix_applied,omitempty"`
}

// LearningPattern stores identified patterns in differences
type LearningPattern struct {
	PatternID       string         `json:"pattern_id"`
	DifferenceType  DifferenceType `json:"difference_type"`
	Occurrences     int            `json:"occurrences"`
	FirstSeen       time.Time      `json:"first_seen"`
	LastSeen        time.Time      `json:"last_seen"`
	RootCause       string         `json:"root_cause"`
	ConfidenceFixed int64          `json:"confidence"` // Fixed-point: 1000 = 1.0
	FixedInt        int8           `json:"fixed"`      // 0/1 instead of bool
}

// ShadowAgentWrapper runs Go agents in shadow mode alongside production Python agents
type ShadowAgentWrapper struct {
	Mode          ShadowMode
	CanaryPercent int
	Results       []ShadowTestResult
	ResultsLimit  int
	mu            sync.Mutex
	logFile       *os.File

	// Self Learning System
	LearningEnabledInt   int8 // 0/1 instead of bool
	Patterns             map[string]LearningPattern
	AutoFixEnabledInt    int8  // 0/1 instead of bool
	ParityThresholdFixed int64 // Fixed-point: 1000 = 1.0
	MinSuccessWindow     int
}

// NewShadowAgentWrapper creates a new shadow agent wrapper with safety defaults
func NewShadowAgentWrapper() (*ShadowAgentWrapper, error) {
	logPath := filepath.Join("agents", "logs", "shadow_testing.log")

	if err := os.MkdirAll(filepath.Dir(logPath), 0755); err != nil {
		return nil, err
	}

	f, err := os.OpenFile(logPath, os.O_APPEND|os.O_CREATE|os.O_WRONLY, 0644)
	if err != nil {
		return nil, err
	}

	return &ShadowAgentWrapper{
		Mode:                 ShadowModeShadow,
		CanaryPercent:        5,
		Results:              make([]ShadowTestResult, 0, 1000),
		ResultsLimit:         1000,
		logFile:              f,
		LearningEnabledInt:   1, // true = 1
		Patterns:             make(map[string]LearningPattern),
		AutoFixEnabledInt:    0,   // false = 0
		ParityThresholdFixed: 999, // 0.999 in fixed-point (1000 = 1.0)
		MinSuccessWindow:     100,
	}, nil
}

// RunShadowTest executes Go agent in parallel with production input - NO SIDE EFFECTS
func (w *ShadowAgentWrapper) RunShadowTest(agentType string, input interface{}, pythonOutput interface{}) (*ShadowTestResult, error) {
	w.mu.Lock()
	defer w.mu.Unlock()

	testID := fmt.Sprintf("shadow_%s", time.Now().Format("20060102_150405.000000"))
	inputHash := calculateHash(input)

	result := &ShadowTestResult{
		TestID:      testID,
		Timestamp:   time.Now(),
		AgentType:   agentType,
		InputHash:   inputHash,
		MatchInt:    0, // false = 0
		Differences: make([]string, 0),
		Errors:      make([]string, 0),
	}

	startTime := time.Now()

	// Execute Go agent in complete isolation
	goOutput, err := w.executeGoAgent(agentType, input)
	result.ExecutionTime = time.Since(startTime)

	if err != nil {
		result.Errors = append(result.Errors, err.Error())
	} else {
		result.GoOutput = goOutput
	}

	// Compare if in compare mode
	if w.Mode >= ShadowModeCompare && pythonOutput != nil {
		pythonMap, ok := pythonOutput.(map[string]interface{})
		if ok {
			result.PythonOutput = pythonMap
			result.ParityScoreFixed, result.MatchInt, result.Differences = compareOutputsFixed(pythonMap, goOutput)
		}
	}

	// Log result (NEVER modify production state)
	w.logResult(result)

	// Store in memory for metrics
	w.Results = append(w.Results, *result)
	if len(w.Results) > w.ResultsLimit {
		w.Results = w.Results[1:]
	}

	// Run learning system on results
	if w.LearningEnabledInt == 1 {
		w.analyzeAndLearn(result)
	}

	// SAFETY FIRST: Never return Go output to production system
	return result, nil
}

// analyzeAndLearn runs self-learning analysis on test results
func (w *ShadowAgentWrapper) analyzeAndLearn(result *ShadowTestResult) {
	if result.MatchInt == 1 {
		return
	}

	// Classify differences and detect patterns
	for _, diff := range result.ClassifiedDiffs {
		patternID := fmt.Sprintf("%s_%s", result.AgentType, diff.Key)

		pattern, exists := w.Patterns[patternID]
		if !exists {
			pattern = LearningPattern{
				PatternID:       patternID,
				DifferenceType:  diff.Type,
				Occurrences:     0,
				FirstSeen:       time.Now(),
				RootCause:       diff.RootCause,
				ConfidenceFixed: diff.ConfidenceFixed,
			}
		}

		pattern.Occurrences++
		pattern.LastSeen = time.Now()
		w.Patterns[patternID] = pattern
	}
}

// GetTrend returns parity trend over last N results (fixed-point version)
func (w *ShadowAgentWrapper) GetTrendFixed(window int) int64 {
	w.mu.Lock()
	defer w.mu.Unlock()

	if len(w.Results) < window {
		window = len(w.Results)
	}

	if window == 0 {
		return 0
	}

	matches := 0
	for i := len(w.Results) - window; i < len(w.Results); i++ {
		if w.Results[i].MatchInt == 1 {
			matches++
		}
	}

	// Return as fixed-point: matches/window * 1000
	return int64(matches * 1000 / window)
}

// ShouldBlockCanary automatically blocks canary routing if parity drops below threshold
func (w *ShadowAgentWrapper) ShouldBlockCanary() bool {
	trend := w.GetTrendFixed(w.MinSuccessWindow)
	return trend < w.ParityThresholdFixed
}

// CalculateTropicalInnerProduct implements Forward-Forward baseline goodness metric
// Tropical Geometry max-plus semiring operation
func CalculateTropicalInnerProduct(a, b []float64) float64 {
	if len(a) != len(b) {
		return 0.0
	}

	maxVal := math.Inf(-1)
	for i := range a {
		current := a[i] + b[i]
		if current > maxVal {
			maxVal = current
		}
	}

	return maxVal
}

// GoodnessDelta calculates FF style difference between positive and negative reality
func GoodnessDelta(positiveScore, negativeScore float64) float64 {
	return positiveScore - negativeScore
}

// executeGoAgent runs the Go agent in complete isolation
func (w *ShadowAgentWrapper) executeGoAgent(agentType string, input interface{}) (map[string]interface{}, error) {
	// This is completely isolated from production systems
	// No database writes, no file modifications, no external API calls that modify state

	config := AgentConfig{
		Name:          fmt.Sprintf("Shadow%sAgent", agentType),
		MaxIterations: 3,
		RateLimitRPM:  5,
	}

	_ = NewBaseAgent(&config, nil)

	// Apply cognitive ergonomics processing
	// Cognitive handling will be implemented in Phase 2
	_ = input

	// Placeholder implementation
	return map[string]interface{}{
		"status":    "shadow_executed",
		"agent":     agentType,
		"timestamp": time.Now().Format(time.RFC3339),
	}, nil
}

// logResult safely logs test results without affecting production
func (w *ShadowAgentWrapper) logResult(result *ShadowTestResult) {
	data, err := json.Marshal(result)
	if err != nil {
		return
	}

	// Write only to shadow log file
	fmt.Fprintf(w.logFile, "%s\n", string(data))
}

// ShouldRouteToGo implements canary safety gate
func (w *ShadowAgentWrapper) ShouldRouteToGo() bool {
	if w.Mode == ShadowModeFull {
		return true
	}

	if w.Mode == ShadowModeCanary {
		// Pseudo-random canary routing - deterministic based on time window
		return int(time.Now().UnixNano()%100) < w.CanaryPercent
	}

	// Always route to Python in shadow/compare modes
	return false
}

// GetSuccessRate returns current parity success rate
func (w *ShadowAgentWrapper) GetSuccessRate() float64 {
	w.mu.Lock()
	defer w.mu.Unlock()

	if len(w.Results) == 0 {
		return 0.0
	}

	matches := 0
	for _, r := range w.Results {
		if r.Match {
			matches++
		}
	}

	return float64(matches) / float64(len(w.Results))
}

// Close cleans up resources
func (w *ShadowAgentWrapper) Close() error {
	return w.logFile.Close()
}

// calculateHash creates a safe hash of input data
func calculateHash(input interface{}) string {
	data, _ := json.Marshal(input)
	hash := sha256.Sum256(data)
	return fmt.Sprintf("%x", hash[:8])
}

// compareOutputs compares Python and Go outputs for functional parity
func compareOutputs(python, goOutput map[string]interface{}) (float64, bool, []string) {
	differences := make([]string, 0)
	match := true

	if goOutput == nil {
		return 0.0, false, []string{"Go output is nil"}
	}

	totalKeys := len(python)
	matchingKeys := 0

	for k, v1 := range python {
		v2, exists := goOutput[k]
		if !exists {
			differences = append(differences, fmt.Sprintf("Missing key: %s", k))
			match = false
			continue
		}

		if fmt.Sprintf("%v", v1) != fmt.Sprintf("%v", v2) {
			differences = append(differences, fmt.Sprintf("Value mismatch for %s", k))
			match = false
		} else {
			matchingKeys++
		}
	}

	score := 0.0
	if totalKeys > 0 {
		score = float64(matchingKeys) / float64(totalKeys)
	}

	return score, match && len(goOutput) == len(python), differences
}
