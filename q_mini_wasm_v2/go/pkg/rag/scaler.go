package rag

import (
	"fmt"
	"math"
	"strings"
)

// TokenScalerConfig holds auto-scaling configuration
type TokenScalerConfig struct {
	MinTokens        int     `json:"min_tokens"`         // Minimum tokens to return
	MaxTokens        int     `json:"max_tokens"`         // Maximum tokens to return
	DefaultTokens    int     `json:"default_tokens"`     // Default token budget
	ScalingFactor    float64 `json:"scaling_factor"`     // Scaling aggressiveness
	ComplexityWeight float64 `json:"complexity_weight"`  // Weight for query complexity
	ContextWeight    float64 `json:"context_weight"`     // Weight for context size
}

// TokenScaler handles automatic token budget scaling
type TokenScaler struct {
	config TokenScalerConfig
}

// NewTokenScaler creates a new token scaler
func NewTokenScaler(config TokenScalerConfig) *TokenScaler {
	if config.MinTokens == 0 {
		config.MinTokens = 256
	}
	if config.MaxTokens == 0 {
		config.MaxTokens = 8192
	}
	if config.DefaultTokens == 0 {
		config.DefaultTokens = 2048
	}
	if config.ScalingFactor == 0 {
		config.ScalingFactor = 1.5
	}
	if config.ComplexityWeight == 0 {
		config.ComplexityWeight = 0.6
	}
	if config.ContextWeight == 0 {
		config.ContextWeight = 0.4
	}

	return &TokenScaler{config: config}
}

// ScaleRequest represents a request for token scaling
type ScaleRequest struct {
	Query           string   // User query
	FilePath        string   // Current file being edited
	MaxTokens       int      // User-specified max tokens (0 = auto)
	ContextType     string   // Type of context needed
	ExistingContext int      // Tokens already in context
}

// ScaleResult represents the scaling result
type ScaleResult struct {
	RecommendedTokens int     // Recommended token budget
	ScalingFactor     float64 // Applied scaling factor
	Reasoning         string  // Human-readable reasoning
	ComplexityScore   float64 // Query complexity score (0-1)
}

// Scale calculates the optimal token budget for a query
func (ts *TokenScaler) Scale(req ScaleRequest) ScaleResult {
	// If user specified max tokens, respect it (within bounds)
	if req.MaxTokens > 0 {
		tokens := clamp(req.MaxTokens, ts.config.MinTokens, ts.config.MaxTokens)
		return ScaleResult{
			RecommendedTokens: tokens,
			ScalingFactor:     1.0,
			Reasoning:         "User-specified token limit",
			ComplexityScore:   0.5,
		}
	}

	// Analyze query complexity
	complexity := ts.analyzeComplexity(req.Query)
	
	// Analyze context needs
	contextNeed := ts.analyzeContextNeed(req)

	// Calculate scaling
	scaling := ts.calculateScaling(complexity, contextNeed)
	
	// Apply scaling to default tokens
	recommended := int(float64(ts.config.DefaultTokens) * scaling)
	recommended = clamp(recommended, ts.config.MinTokens, ts.config.MaxTokens)

	// Adjust for existing context
	if req.ExistingContext > 0 {
		remaining := recommended - req.ExistingContext
		if remaining < ts.config.MinTokens/2 {
			// Not enough room, increase budget
			recommended = req.ExistingContext + ts.config.MinTokens
			recommended = clamp(recommended, ts.config.MinTokens, ts.config.MaxTokens)
		}
	}

	reasoning := ts.generateReasoning(complexity, contextNeed, scaling, recommended)

	return ScaleResult{
		RecommendedTokens: recommended,
		ScalingFactor:     scaling,
		Reasoning:         reasoning,
		ComplexityScore:   complexity,
	}
}

// analyzeComplexity scores query complexity from 0 to 1
func (ts *TokenScaler) analyzeComplexity(query string) float64 {
	score := 0.0
	query = strings.ToLower(query)

	// Length factor
	lengthFactor := math.Min(float64(len(query))/500.0, 1.0)
	score += lengthFactor * 0.2

	// Keyword complexity
	complexKeywords := []string{
		"explain", "how", "why", "compare", "difference",
		"implement", "design", "architecture", "algorithm",
		"optimize", "debug", "error", "issue", "problem",
	}

	simpleKeywords := []string{
		"what is", "where", "when", "list", "show",
		"get", "find", "name", "type",
	}

	for _, kw := range complexKeywords {
		if strings.Contains(query, kw) {
			score += 0.15
		}
	}

	for _, kw := range simpleKeywords {
		if strings.Contains(query, kw) {
			score -= 0.05
		}
	}

	// Code indicators
	if strings.Contains(query, "```") || strings.Contains(query, "func ") ||
		strings.Contains(query, "class ") || strings.Contains(query, "import ") {
		score += 0.2
	}

	// Question marks (multiple questions = more complex)
	questionMarks := strings.Count(query, "?")
	score += math.Min(float64(questionMarks)*0.1, 0.3)

	return clampf(score, 0.0, 1.0)
}

// analyzeContextNeed scores context requirements from 0 to 1
func (ts *TokenScaler) analyzeContextNeed(req ScaleRequest) float64 {
	score := 0.5 // Default moderate need

	// Context type adjustments
	switch req.ContextType {
	case "code":
		score = 0.7 // Code needs more context
	case "documentation":
		score = 0.5
	case "research":
		score = 0.9 // Research papers need lots of context
	case "api":
		score = 0.6
	default:
		score = 0.5
	}

	// File path indicators
	if req.FilePath != "" {
		ext := strings.ToLower(req.FilePath[strings.LastIndex(req.FilePath, "."):])
		switch ext {
		case ".cpp", ".hpp", ".c", ".h":
			score += 0.1 // C++ is complex
		case ".go":
			score += 0.05
		case ".md":
			score -= 0.1 // Docs need less context
		}
	}

	return clampf(score, 0.0, 1.0)
}

// calculateScaling computes the final scaling factor
func (ts *TokenScaler) calculateScaling(complexity, contextNeed float64) float64 {
	weighted := complexity*ts.config.ComplexityWeight + contextNeed*ts.config.ContextWeight
	
	// Apply scaling factor with bounds
	scaling := 1.0 + (weighted-0.5)*ts.config.ScalingFactor
	
	// Clamp to reasonable range
	return clampf(scaling, 0.5, 3.0)
}

// generateReasoning creates a human-readable explanation
func (ts *TokenScaler) generateReasoning(complexity, contextNeed, scaling float64, tokens int) string {
	var parts []string

	if complexity > 0.7 {
		parts = append(parts, "high query complexity")
	} else if complexity < 0.3 {
		parts = append(parts, "simple query")
	}

	if contextNeed > 0.7 {
		parts = append(parts, "extensive context needed")
	} else if contextNeed < 0.3 {
		parts = append(parts, "minimal context needed")
	}

	if scaling > 1.5 {
		parts = append(parts, "scaled up for detail")
	} else if scaling < 0.8 {
		parts = append(parts, "scaled down for efficiency")
	}

	if len(parts) == 0 {
		return fmt.Sprintf("Standard token budget: %d tokens", tokens)
	}

	return fmt.Sprintf("%d tokens (%s)", tokens, strings.Join(parts, ", "))
}

// clamp constrains a value between min and max
func clamp(value, min, max int) int {
	if value < min {
		return min
	}
	if value > max {
		return max
	}
	return value
}

func clampf(value, min, max float64) float64 {
	if value < min {
		return min
	}
	if value > max {
		return max
	}
	return value
}