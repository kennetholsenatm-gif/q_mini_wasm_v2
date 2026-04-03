// Package hooks implements gRPC hook services
package hooks

import (
	"bytes"
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"strings"

	"github.com/BurntSushi/toml"
)

// TOMLHookConfiguration represents hooks configuration in TOML format
type TOMLHookConfiguration struct {
	Project     string              `toml:"project"`
	GitHooks    TOMLGitHooksConfig  `toml:"git_hooks"`
	QuantumHooks TOMLQuantumHooksConfig `toml:"quantum_specific_hooks,omitempty"`
}

// TOMLGitHooksConfig represents git hooks configuration in TOML
type TOMLGitHooksConfig struct {
	PreCommit  TOMLHookConfig `toml:"pre_commit"`
	CommitMsg  TOMLHookConfig `toml:"commit_msg,omitempty"`
	PrePush    TOMLHookConfig `toml:"pre_push"`
	PostCommit TOMLHookConfig `toml:"post_commit"`
	PostMerge  TOMLHookConfig `toml:"post_merge"`
}

// TOMLQuantumHooksConfig represents quantum-specific hooks in TOML
type TOMLQuantumHooksConfig struct {
	StabilizerCorrectness  TOMLHookConfig `toml:"stabilizer_correctness,omitempty"`
	CliffordGateValidation TOMLHookConfig `toml:"clifford_gate_validation,omitempty"`
	EnergyEfficiency       TOMLHookConfig `toml:"energy_efficiency,omitempty"`
}

// TOMLHookConfig represents a hook configuration in TOML
type TOMLHookConfig struct {
	Description string       `toml:"description"`
	Enabled     bool         `toml:"enabled"`
	Checks      []TOMLCheck  `toml:"checks,omitempty"`
	Actions     []TOMLAction `toml:"actions,omitempty"`
}

// TOMLCheck represents a check in TOML
type TOMLCheck struct {
	Name        string `toml:"name"`
	Command     string `toml:"command,omitempty"`
	Description string `toml:"description"`
	Required    bool   `toml:"required"`
}

// TOMLAction represents an action in TOML
type TOMLAction struct {
	Name        string `toml:"name"`
	Command     string `toml:"command,omitempty"`
	Description string `toml:"description"`
	Required    bool   `toml:"required"`
}

// ConvertJSONToTOML converts JSON hooks configuration to TOML format
func ConvertJSONToTOML(jsonPath string, tomlPath string) error {
	// Read JSON file
	jsonData, err := os.ReadFile(jsonPath)
	if err != nil {
		return fmt.Errorf("failed to read JSON file: %w", err)
	}

	// Parse JSON into generic map to handle any structure
	var jsonMap map[string]interface{}
	if err := json.Unmarshal(jsonData, &jsonMap); err != nil {
		return fmt.Errorf("failed to parse JSON: %w", err)
	}

	// Convert to TOML structure
	tomlConfig, err := convertMapToTOMLStructure(jsonMap)
	if err != nil {
		return fmt.Errorf("failed to convert to TOML structure: %w", err)
	}

	// Marshal to TOML
	var buf bytes.Buffer
	encoder := toml.NewEncoder(&buf)
	if err := encoder.Encode(tomlConfig); err != nil {
		return fmt.Errorf("failed to encode TOML: %w", err)
	}

	// Ensure directory exists
	dir := filepath.Dir(tomlPath)
	if err := os.MkdirAll(dir, 0755); err != nil {
		return fmt.Errorf("failed to create directory: %w", err)
	}

	// Write TOML file
	if err := os.WriteFile(tomlPath, buf.Bytes(), 0644); err != nil {
		return fmt.Errorf("failed to write TOML file: %w", err)
	}

	return nil
}

// convertMapToTOMLStructure converts a JSON map to TOML structure
func convertMapToTOMLStructure(jsonMap map[string]interface{}) (*TOMLHookConfiguration, error) {
	config := &TOMLHookConfiguration{
		Project: getOrDefault(jsonMap, "project", "q_mini_wasm_v2"),
	}

	// Convert git hooks
	if gitHooks, ok := jsonMap["git_hooks"].(map[string]interface{}); ok {
		config.GitHooks = convertGitHooks(gitHooks)
	}

	// Convert quantum hooks if present
	if quantumHooks, ok := jsonMap["quantum_specific_hooks"].(map[string]interface{}); ok {
		config.QuantumHooks = convertQuantumHooks(quantumHooks)
	}

	return config, nil
}

// convertGitHooks converts git hooks from JSON to TOML
func convertGitHooks(gitHooks map[string]interface{}) TOMLGitHooksConfig {
	var config TOMLGitHooksConfig

	if preCommit, ok := gitHooks["pre_commit"].(map[string]interface{}); ok {
		config.PreCommit = convertHookConfig(preCommit)
	}

	if commitMsg, ok := gitHooks["commit_msg"].(map[string]interface{}); ok {
		config.CommitMsg = convertHookConfig(commitMsg)
	}

	if prePush, ok := gitHooks["pre_push"].(map[string]interface{}); ok {
		config.PrePush = convertHookConfig(prePush)
	}

	if postCommit, ok := gitHooks["post_commit"].(map[string]interface{}); ok {
		config.PostCommit = convertHookConfig(postCommit)
	}

	if postMerge, ok := gitHooks["post_merge"].(map[string]interface{}); ok {
		config.PostMerge = convertHookConfig(postMerge)
	}

	return config
}

// convertQuantumHooks converts quantum-specific hooks from JSON to TOML
func convertQuantumHooks(quantumHooks map[string]interface{}) TOMLQuantumHooksConfig {
	var config TOMLQuantumHooksConfig

	if stabilizer, ok := quantumHooks["stabilizer_correctness"].(map[string]interface{}); ok {
		config.StabilizerCorrectness = convertHookConfig(stabilizer)
	}

	if clifford, ok := quantumHooks["clifford_gate_validation"].(map[string]interface{}); ok {
		config.CliffordGateValidation = convertHookConfig(clifford)
	}

	if energy, ok := quantumHooks["energy_efficiency"].(map[string]interface{}); ok {
		config.EnergyEfficiency = convertHookConfig(energy)
	}

	return config
}

// convertHookConfig converts a hook configuration from JSON to TOML
func convertHookConfig(hookMap map[string]interface{}) TOMLHookConfig {
	config := TOMLHookConfig{
		Description: getOrDefault(hookMap, "description", ""),
		Enabled:     getBoolOrDefault(hookMap, "enabled", false),
	}

	// Convert checks
	if checks, ok := hookMap["checks"].([]interface{}); ok {
		config.Checks = convertChecks(checks)
	}

	// Convert actions
	if actions, ok := hookMap["actions"].([]interface{}); ok {
		config.Actions = convertActions(actions)
	}

	return config
}

// convertChecks converts checks from JSON to TOML
func convertChecks(checks []interface{}) []TOMLCheck {
	var result []TOMLCheck

	for _, check := range checks {
		if checkMap, ok := check.(map[string]interface{}); ok {
			tomlCheck := TOMLCheck{
				Name:        getOrDefault(checkMap, "name", ""),
				Command:     getOrDefault(checkMap, "command", ""),
				Description: getOrDefault(checkMap, "description", ""),
				Required:    getBoolOrDefault(checkMap, "required", false),
			}
			result = append(result, tomlCheck)
		}
	}

	return result
}

// convertActions converts actions from JSON to TOML
func convertActions(actions []interface{}) []TOMLAction {
	var result []TOMLAction

	for _, action := range actions {
		if actionMap, ok := action.(map[string]interface{}); ok {
			tomlAction := TOMLAction{
				Name:        getOrDefault(actionMap, "name", ""),
				Command:     getOrDefault(actionMap, "command", ""),
				Description: getOrDefault(actionMap, "description", ""),
				Required:    getBoolOrDefault(actionMap, "required", false),
			}
			result = append(result, tomlAction)
		}
	}

	return result
}

// Helper functions
func getOrDefault(m map[string]interface{}, key, defaultValue string) string {
	if val, ok := m[key]; ok {
		if str, ok := val.(string); ok {
			return str
		}
	}
	return defaultValue
}

func getBoolOrDefault(m map[string]interface{}, key string, defaultValue bool) bool {
	if val, ok := m[key]; ok {
		if b, ok := val.(bool); ok {
			return b
		}
	}
	return defaultValue
}

// ConvertJSONToTOMLFallback attempts to convert JSON to TOML
// This is used as a fallback when gRPC conversion is not possible
func ConvertJSONToTOMLFallback(jsonPath string) (string, error) {
	// Generate TOML output path
	tomlPath := strings.TrimSuffix(jsonPath, filepath.Ext(jsonPath)) + ".toml"

	// Try to convert
	if err := ConvertJSONToTOML(jsonPath, tomlPath); err != nil {
		return "", fmt.Errorf("TOML conversion failed: %w", err)
	}

	return tomlPath, nil
}
