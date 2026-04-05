package agents

import (
	"regexp"
	"strconv"
	"strings"
)

// ValidationResult contains result of message validation
type ValidationResult struct {
	IsValid        bool                   `json:"is_valid"`
	Errors         []string               `json:"errors"`
	FixedMessages  []map[string]interface{} `json:"fixed_messages,omitempty"`
}

// LLMMessageValidator validates LLM messages for various API providers
type LLMMessageValidator struct {
	Provider string
}

// NewLLMMessageValidator creates a new LLMMessageValidator
func NewLLMMessageValidator(provider string) *LLMMessageValidator {
	if provider == "" {
		provider = "openrouter"
	}
	return &LLMMessageValidator{
		Provider: strings.ToLower(provider),
	}
}

// ValidateMessages validates messages for LLM API compatibility
func (v *LLMMessageValidator) ValidateMessages(messages []map[string]interface{}) *ValidationResult {
	var errors []string

	for i, msg := range messages {
		role, _ := msg["role"].(string)
		role = strings.ToLower(role)
		content, contentExists := msg["content"]
		toolCalls, toolCallsExists := msg["tool_calls"]

		// Assistant message validation
		if role == "assistant" {
			// Check if content is nil AND tool_calls is missing/empty
			contentNil := !contentExists || content == nil
			toolCallsEmpty := !toolCallsExists || toolCalls == nil

			if contentNil && toolCallsEmpty {
				errorMsg := "Message " + strconv.Itoa(i) + ": Assistant message must provide content or tool_calls"
				errors = append(errors, errorMsg)
			}
		}
	}

	result := &ValidationResult{
		IsValid: len(errors) == 0,
		Errors:  errors,
	}

	if !result.IsValid {
		result.FixedMessages = v.FixMessages(messages)
	}

	return result
}

// FixMessages automatically fixes messages to meet provider requirements
func (v *LLMMessageValidator) FixMessages(messages []map[string]interface{}) []map[string]interface{} {
	fixed := make([]map[string]interface{}, len(messages))

	for i, msg := range messages {
		fixedMsg := make(map[string]interface{})
		for k, val := range msg {
			fixedMsg[k] = val
		}

		role, _ := fixedMsg["role"].(string)
		role = strings.ToLower(role)
		content, contentExists := fixedMsg["content"]
		toolCalls, toolCallsExists := fixedMsg["tool_calls"]

		if role == "assistant" {
			contentNil := !contentExists || content == nil
			toolCallsEmpty := !toolCallsExists || toolCalls == nil

			if contentNil && toolCallsEmpty {
				fixedMsg["content"] = ""
			}
		}

		fixed[i] = fixedMsg
	}

	return fixed
}

// ExtractErrorInfo extracts structured information from LLM API error messages
func (v *LLMMessageValidator) ExtractErrorInfo(errorMessage string) map[string]interface{} {
	errorInfo := map[string]interface{}{
		"original_error": errorMessage,
		"error_type":     "unknown",
		"message_index":  nil,
		"provider":       v.Provider,
		"is_fixable":     false,
	}

	// Parse missing content error
	if strings.Contains(errorMessage, "messages[") &&
		strings.Contains(errorMessage, "] assistant must provide content or tool_calls") {

		re := regexp.MustCompile(`messages\[(\d+)\]`)
		match := re.FindStringSubmatch(errorMessage)
		if len(match) > 1 {
			idx, err := strconv.Atoi(match[1])
			if err == nil {
				errorInfo["message_index"] = idx
				errorInfo["error_type"] = "missing_content_or_tool_calls"
				errorInfo["is_fixable"] = true
			}
		}
	}

	return errorInfo
}

// ValidateAndFixMessages convenience function to validate and fix messages
func ValidateAndFixMessages(messages []map[string]interface{}, provider string) (bool, []map[string]interface{}, []string) {
	validator := NewLLMMessageValidator(provider)
	result := validator.ValidateMessages(messages)

	if !result.IsValid {
		fixed := validator.FixMessages(messages)
		finalResult := validator.ValidateMessages(fixed)
		return finalResult.IsValid, fixed, finalResult.Errors
	}

	return true, messages, []string{}
}

// CreateSafeAssistantMessage creates a safe assistant message that meets API requirements
func CreateSafeAssistantMessage(content interface{}, toolCalls []map[string]interface{}) map[string]interface{} {
	message := map[string]interface{}{
		"role": "assistant",
	}

	if len(toolCalls) > 0 {
		message["tool_calls"] = toolCalls
	} else {
		if content == nil {
			message["content"] = ""
		} else {
			message["content"] = content
		}
	}

	return message
}