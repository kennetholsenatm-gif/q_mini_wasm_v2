# LLM API Error Fix - OpenRouter Streaming Error

## Problem Summary

The cleanup agent (`KanbanReviewFixAgent`) was not fixing the OpenRouter streaming error because it was designed to handle Kanban board issues, not LLM API errors.

### Error Message
```
Failed to create stream: inference request failed: failed to invoke model 'xiaomi/mimo-v2-pro' with streaming from OpenRouter: request failed with status 400: {"error":{"message":"Provider returned error","code":400,"metadata":{"raw":"{\"error\":{\"code\":\"400\",\"message\":\"Param Incorrect\",\"param\":\"messages[32] assistant must provide content or tool_calls\",\"type\":\"\"}}","provider_name":"Xiaomi","is_byok":false}},"user_id":"org_2ue3sRj4x3tXiJ1Dy2aaiheiHnm"}
```

### Root Cause
The error occurs when an assistant message at index 32 doesn't have either `content` or `tool_calls`. OpenRouter's API requires all assistant messages to have at least one of these fields.

## Solution

### 1. Created LLM Message Validator (`agents/llm/message_validator.py`)

A new module that:
- Validates LLM messages before sending to API providers
- Specifically handles the "assistant must provide content or tool_calls" error
- Automatically fixes messages by adding empty content when needed
- Parses error messages to extract structured information

**Key Features:**
- `LLMMessageValidator` class for message validation
- `validate_and_fix_messages()` convenience function
- `create_safe_assistant_message()` helper function
- Error message parsing and classification

### 2. Enhanced KanbanReviewFixAgent (`agents/kanban_review_fix_agent.py`)

Added new methods to handle LLM API errors:

**`handle_llm_api_error(error_message: str)`**
- Parses LLM API error messages
- Creates fix cards for LLM-related issues
- Provides specific solutions for common errors

**`validate_llm_messages(messages: List[Dict[str, Any]])`**
- Validates messages before sending to LLM APIs
- Automatically fixes problematic messages
- Returns validation status and fixed messages

**Updated `suggest_improvements()`**
- Now includes LLM error handling recommendations

### 3. Updated Module Exports (`agents/llm/__init__.py`)

Added exports for the new message validator components.

## Usage

### Validating Messages Before API Calls

```python
from agents.llm import validate_and_fix_messages

messages = [
    {"role": "user", "content": "Hello"},
    {"role": "assistant"},  # Missing content
]

is_valid, fixed_messages, errors = validate_and_fix_messages(messages, provider="openrouter")
# fixed_messages[1] now has: {"role": "assistant", "content": ""}
```

### Creating Safe Assistant Messages

```python
from agents.llm import create_safe_assistant_message

# Safe message with content
msg1 = create_safe_assistant_message(content="Hello!")

# Safe message with tool calls
msg2 = create_safe_assistant_message(tool_calls=[{"name": "test", "args": {}}])

# Safe message with None content (becomes empty string)
msg3 = create_safe_assistant_message(content=None)
```

### Handling LLM Errors in Agent

```python
from agents import KanbanReviewFixAgent, AgentConfig

config = AgentConfig(name="MyAgent", description="...")
agent = KanbanReviewFixAgent(config)

# Handle an LLM API error
result = await agent.handle_llm_api_error(error_message)
print(f"Action taken: {result.action_taken}")
print(f"Success: {result.success}")
```

## Testing

Run the test suite:
```bash
python agents/test_llm_message_validator.py
```

All tests pass, verifying:
- OpenRouter error detection and fixing
- Message validation and correction
- Safe assistant message creation
- Error message parsing

## Benefits

1. **Prevents API Errors**: Messages are validated before sending to LLM APIs
2. **Automatic Fixing**: Problematic messages are automatically corrected
3. **Better Error Handling**: LLM-specific errors are now handled by the agent
4. **Extensible**: Easy to add support for more LLM providers and error types

## Files Modified/Created

1. **Created**: `agents/llm/message_validator.py` - New message validation module
2. **Modified**: `agents/kanban_review_fix_agent.py` - Added LLM error handling methods
3. **Modified**: `agents/llm/__init__.py` - Updated exports
4. **Created**: `agents/test_llm_message_validator.py` - Test suite
5. **Created**: `LLM_ERROR_FIX_SUMMARY.md` - This documentation

## Future Improvements

- Add support for more LLM providers (Anthropic, Cohere, etc.)
- Implement retry logic with exponential backoff
- Add metrics tracking for LLM API errors
- Create a dedicated LLM error monitoring agent