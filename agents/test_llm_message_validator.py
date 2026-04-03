#!/usr/bin/env python3
"""
Test LLM Message Validator

This test verifies that the message validator correctly handles
the OpenRouter streaming error.
"""

import sys
from pathlib import Path

# Add parent directory to path for imports
sys.path.insert(0, str(Path(__file__).parent.parent))

from agents.llm.message_validator import (
    LLMMessageValidator,
    validate_and_fix_messages,
    create_safe_assistant_message
)


def test_openrouter_error_fix():
    """Test that the validator fixes the specific OpenRouter error."""
    print("Testing OpenRouter error fix...")
    
    # Create test messages with the problematic assistant message
    messages = [
        {"role": "user", "content": "Hello"},
        {"role": "assistant", "content": "Hi there!"},
        # Simulate many messages...
        *[{"role": "user", "content": f"Message {i}"} for i in range(30)],
        # This is the problematic message at index 32 (after 30 user messages + 2 initial)
        {"role": "assistant"},  # Missing content and tool_calls
    ]
    
    # Validate messages
    validator = LLMMessageValidator(provider="openrouter")
    result = validator.validate_messages(messages)
    
    print(f"  Validation result: {result.is_valid}")
    print(f"  Errors: {result.errors}")
    
    if not result.is_valid:
        # Fix messages
        fixed_messages = validator.fix_messages(messages)
        
        # Verify fix
        final_result = validator.validate_messages(fixed_messages)
        print(f"  Fixed validation result: {final_result.is_valid}")
        
        # Check that the problematic message now has content
        problematic_message = fixed_messages[32]
        print(f"  Fixed message: {problematic_message}")
        
        if not final_result.is_valid:
            print(f"  Final validation errors: {final_result.errors}")
            print(f"  Problematic message after fix: {problematic_message}")
        
        # The validator should have fixed the message
        assert "content" in problematic_message, "Fixed message should have content"
        assert problematic_message["content"] == "", "Fixed message should have empty content"
        
        print("  [PASS] OpenRouter error fix test passed!")
    else:
        print("  [FAIL] Messages were already valid (unexpected)")


def test_convenience_function():
    """Test the convenience function."""
    print("\nTesting convenience function...")
    
    messages = [
        {"role": "user", "content": "Hello"},
        {"role": "assistant"},  # Problematic message
    ]
    
    # The message {"role": "assistant"} has no content key at all
    # This should be invalid because content is None (not provided)
    # But validate_and_fix_messages will fix it and return True
    is_valid, fixed_messages, errors = validate_and_fix_messages(messages, provider="openrouter")
    
    print(f"  Result valid: {is_valid}")
    print(f"  Errors: {errors}")
    print(f"  Fixed messages: {fixed_messages}")
    
    # The function should fix the message and return valid
    assert is_valid, "Fixed messages should be valid"
    assert len(errors) == 0, "Should have no errors after fixing"
    # After fixing, content should be empty string
    assert fixed_messages[1]["content"] == "", "Fixed message should have empty content"
    assert "role" in fixed_messages[1], "Fixed message should have role"
    
    print("  [PASS] Convenience function test passed!")


def test_safe_assistant_message():
    """Test creating safe assistant messages."""
    print("\nTesting safe assistant message creation...")
    
    # Test with None content
    msg1 = create_safe_assistant_message(content=None)
    print(f"  Message with None content: {msg1}")
    assert msg1["content"] == "", "Should have empty content"
    
    # Test with empty string content
    msg2 = create_safe_assistant_message(content="")
    print(f"  Message with empty content: {msg2}")
    assert msg2["content"] == "", "Should have empty content"
    
    # Test with actual content
    msg3 = create_safe_assistant_message(content="Hello!")
    print(f"  Message with content: {msg3}")
    assert msg3["content"] == "Hello!", "Should have provided content"
    
    # Test with tool calls
    msg4 = create_safe_assistant_message(tool_calls=[{"name": "test", "args": {}}])
    print(f"  Message with tool calls: {msg4}")
    assert "tool_calls" in msg4, "Should have tool_calls"
    assert "content" not in msg4, "Should not have content when tool_calls present"
    
    print("  [PASS] Safe assistant message test passed!")


def test_error_parsing():
    """Test parsing of error messages."""
    print("\nTesting error message parsing...")
    
    # The specific error from the user's report
    error_message = '''Failed to create stream: inference request failed: failed to invoke model 'xiaomi/mimo-v2-pro' with streaming from OpenRouter: request failed with status 400: {"error":{"message":"Provider returned error","code":400,"metadata":{"raw":"{\\"error\\":{\\"code\\":\\"400\\",\\"message\\":\\"Param Incorrect\\",\\"param\\":\\"messages[32] assistant must provide content or tool_calls\\",\\"type\\":\\"\\"}}","provider_name":"Xiaomi","is_byok":false}},"user_id":"org_2ue3sRj4x3tXiJ1Dy2aaiheiHnm"}'''
    
    validator = LLMMessageValidator(provider="openrouter")
    error_info = validator.extract_error_info(error_message)
    
    print(f"  Error info: {error_info}")
    
    assert error_info["is_fixable"] == True, "Error should be fixable"
    assert error_info["error_type"] == "missing_content_or_tool_calls", "Should identify error type"
    assert error_info["message_index"] == 32, "Should extract message index"
    
    print("  [PASS] Error parsing test passed!")


if __name__ == "__main__":
    print("Running LLM Message Validator tests...\n")
    
    try:
        test_openrouter_error_fix()
        test_convenience_function()
        test_safe_assistant_message()
        test_error_parsing()
        
        print("\n" + "="*50)
        print("[PASS] All tests passed!")
        print("="*50)
        
    except Exception as e:
        print(f"\n[FAIL] Test failed: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)