package agents

import (
	"strings"

	"github.com/pelletier/go-toml/v2"
)

// CognitiveErgonomicsViolation represents a violation of cognitive ergonomics principles
type CognitiveErgonomicsViolation struct {
	Principle    string   `json:"principle"`
	Severity     string   `json:"severity"`
	Path         string   `json:"path"`
	Description  string   `json:"description"`
	Recommendation string `json:"recommendation"`
}

// CognitiveErgonomicsLinter implements the OmniGraph Cognitive Blueprint linting
type CognitiveErgonomicsLinter struct {
	Violations []CognitiveErgonomicsViolation
}

// NewCognitiveErgonomicsLinter creates a new linter instance
func NewCognitiveErgonomicsLinter() *CognitiveErgonomicsLinter {
	return &CognitiveErgonomicsLinter{
		Violations: make([]CognitiveErgonomicsViolation, 0),
	}
}

// LintToml analyzes TOML content against cognitive ergonomics principles
func (l *CognitiveErgonomicsLinter) LintToml(tomlContent string) []CognitiveErgonomicsViolation {
	l.Violations = make([]CognitiveErgonomicsViolation, 0)

	var config map[string]interface{}
	err := toml.Unmarshal([]byte(tomlContent), &config)
	if err != nil {
		l.addViolation(
			"Valid TOML Parsing",
			"critical",
			"root",
			"Failed to parse TOML content",
			"Fix TOML syntax errors before ergonomic analysis",
		)
		return l.Violations
	}

	l.checkMillersLaw(config, "root")
	l.checkChunkingPrinciple(config, "root")
	l.checkVisualHierarchy(config, "root")
	l.checkProgressiveDisclosure(config, "root")

	return l.Violations
}

// checkMillersLaw verifies that no section contains more than 7±2 top level items
func (l *CognitiveErgonomicsLinter) checkMillersLaw(obj map[string]interface{}, path string) {
	itemCount := len(obj)

	if itemCount > 9 {
		l.addViolation(
			"Miller's Law (7±2 Working Memory Limit)",
			"high",
			path,
			"Section contains more than 9 ungrouped items",
			"Group related items into logical sub-sections (chunks) of 5-9 items maximum",
		)
	}

	// Recurse into child sections
	for k, v := range obj {
		if child, ok := v.(map[string]interface{}); ok {
			l.checkMillersLaw(child, path+"."+k)
		}
	}
}

// checkChunkingPrinciple verifies that related items are properly grouped
func (l *CognitiveErgonomicsLinter) checkChunkingPrinciple(obj map[string]interface{}, path string) {
	keys := make([]string, 0, len(obj))
	for k := range obj {
		keys = append(keys, k)
	}

	// Check for ungrouped flat parameters
	commonPrefixes := make(map[string]int)
	for _, k := range keys {
		parts := strings.SplitN(k, "_", 2)
		if len(parts) == 2 {
			commonPrefixes[parts[0]]++
		}
	}

	for prefix, count := range commonPrefixes {
		if count >= 4 {
			l.addViolation(
				"Structural Chunking Principle",
				"medium",
				path,
				"Multiple parameters share common prefix indicating missing grouping",
				"Create a nested table for '" + prefix + "_*' parameters to form logical chunks",
			)
		}
	}
}

// checkVisualHierarchy verifies that sections are properly ordered by priority
func (l *CognitiveErgonomicsLinter) checkVisualHierarchy(obj map[string]interface{}, path string) {
	// Check for critical settings mixed with trivial settings
	hasCritical := false
	hasTrivial := false

	for k := range obj {
		lower := strings.ToLower(k)
		if strings.Contains(lower, "enabled") || strings.Contains(lower, "critical") || 
		   strings.Contains(lower, "timeout") || strings.Contains(lower, "api_key") {
			hasCritical = true
		}
		if strings.Contains(lower, "color") || strings.Contains(lower, "theme") || 
		   strings.Contains(lower, "debug") || strings.Contains(lower, "verbose") {
			hasTrivial = true
		}
	}

	if hasCritical && hasTrivial {
		l.addViolation(
			"Visual Hierarchy Principle",
			"medium",
			path,
			"Critical operational parameters mixed with cosmetic/debug settings",
			"Separate critical path configuration into separate section before cosmetic options",
		)
	}
}

// checkProgressiveDisclosure verifies that advanced settings are segregated
func (l *CognitiveErgonomicsLinter) checkProgressiveDisclosure(obj map[string]interface{}, path string) {
	advancedCount := 0
	for k := range obj {
		lower := strings.ToLower(k)
		if strings.Contains(lower, "advanced") || strings.Contains(lower, "experimental") || 
		   strings.Contains(lower, "expert") || strings.Contains(lower, "beta") {
			advancedCount++
		}
	}

	if advancedCount >= 3 && path != "root.advanced" {
		l.addViolation(
			"Progressive Disclosure Principle",
			"low",
			path,
			"Multiple advanced options present at root level",
			"Move all advanced/experimental settings into an [advanced] nested table",
		)
	}
}

func (l *CognitiveErgonomicsLinter) addViolation(principle, severity, path, description, recommendation string) {
	l.Violations = append(l.Violations, CognitiveErgonomicsViolation{
		Principle:    principle,
		Severity:     severity,
		Path:         path,
		Description:  description,
		Recommendation: recommendation,
	})
}