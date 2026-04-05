package pkg

import (
	"strings"
	"unicode/utf8"
)

// CognitiveErgonomicsLinter implements the Cognitive Ergonomics Model Protocol
type CognitiveErgonomicsLinter struct {
}

// ErgonomicsViolation represents a detected ergonomic violation
type ErgonomicsViolation struct {
	Principle       string   `json:"principle"`
	Severity        string   `json:"severity"`
	Description     string   `json:"description"`
	Location        string   `json:"location"`
	Recommendation  string   `json:"recommendation"`
	CognitivePenalty float64 `json:"cognitive_penalty"`
}

// CognitiveLoadAnalysis result
type CognitiveLoadAnalysis struct {
	IntrinsicLoad  float64 `json:"intrinsic_load"`
	ExtraneousLoad float64 `json:"extraneous_load"`
	GermaneLoad    float64 `json:"germane_load"`
	TotalLoad      float64 `json:"total_load"`
}

// NewCognitiveErgonomicsLinter creates a new linter
func NewCognitiveErgonomicsLinter() *CognitiveErgonomicsLinter {
	return &CognitiveErgonomicsLinter{}
}

// AnalyzeDocument analyzes markdown documentation for cognitive ergonomics violations
func (l *CognitiveErgonomicsLinter) AnalyzeDocument(content string) []ErgonomicsViolation {
	violations := make([]ErgonomicsViolation, 0)

	// Check Miller's Law (7±2 items per chunk)
	lineCount := 0
	paragraphStart := 0
	lines := strings.Split(content, "\n")

	for i, line := range lines {
		if strings.TrimSpace(line) == "" {
			if lineCount > 9 {
				violations = append(violations, ErgonomicsViolation{
					Principle:       "Miller's Law / Chunking",
					Severity:        "high",
					Description:     "Unchunked wall of text detected",
					Location:        string(paragraphStart),
					Recommendation:  "Break paragraph into 5-9 line chunks with whitespace separators",
					CognitivePenalty: 0.85,
				})
			}
			lineCount = 0
			paragraphStart = i + 1
		} else {
			lineCount++
		}
	}

	// Check line length (optimal 50-75 characters)
	for i, line := range lines {
		length := utf8.RuneCountInString(line)
		if length > 100 {
			violations = append(violations, ErgonomicsViolation{
				Principle:       "Reading Ergonomics",
				Severity:        "medium",
				Description:     "Excessively long line reduces reading speed",
				Location:        string(i),
				Recommendation:  "Wrap lines to 50-75 characters for optimal saccade movement",
				CognitivePenalty: 0.4,
			})
		}
	}

	// Check for actionable error messages
	if strings.Contains(content, "Error 500") || strings.Contains(content, "Contact administrator") {
		violations = append(violations, ErgonomicsViolation{
			Principle:       "Flow State Preservation",
			Severity:        "critical",
			Description:     "Dead-end error message detected",
			Location:        "entire document",
			Recommendation:  "Replace with specific diagnostic information and recovery steps",
			CognitivePenalty: 1.0,
		})
	}

	return violations
}

// AnalyzeInterface analyzes UI structure for cognitive ergonomics
func (l *CognitiveErgonomicsLinter) AnalyzeInterface(elements int, options int) *CognitiveLoadAnalysis {
	analysis := &CognitiveLoadAnalysis{}

	// Miller's Law calculation
	if options > 9 {
		analysis.ExtraneousLoad += 0.3 * (float64(options) - 9)
	}

	// Hick-Hyman Law penalty
	analysis.ExtraneousLoad += 0.05 * float64(options)

	// Total load calculation
	analysis.TotalLoad = analysis.IntrinsicLoad + analysis.ExtraneousLoad + analysis.GermaneLoad

	return analysis
}