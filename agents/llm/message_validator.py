"""
LLM Message Validator - Fixes OpenRouter streaming errors

This module validates LLM messages to ensure they meet provider requirements.
The main issue: assistant messages must have either 'content' or 'tool_calls'.
"""

import re
from typing import Any, Dict, List, Optional, Tuple
from dataclasses import dataclass

import structlog

logger = structlog.get_logger()


@dataclass
class ValidationResult:
    """Result of message validation."""
    is_valid: bool
    errors: List[str]
    fixed_messages: Optional[List[Dict[str, Any]]] = None


class LLMMessageValidator:
    """Validates LLM messages for various API providers."""
    
    def __init__(self, provider: str = "openrouter"):
        self.provider = provider.lower()
    
    def validate_messages(self, messages: List[Dict[str, Any]]) -> ValidationResult:
        """Validate messages for LLM API compatibility."""
        errors = []
        
        for i, msg in enumerate(messages):
            role = msg.get("role", "").lower()
            content = msg.get("content")
            tool_calls = msg.get("tool_calls")
            
            # Assistant message validation
            if role == "assistant":
                # Check if content is None (not provided) AND tool_calls is None/empty
                # Empty string content is valid (it's a value, just empty)
                if content is None and not tool_calls:
                    error_msg = f"Message {i}: Assistant message must provide content or tool_calls"
                    errors.append(error_msg)
                    logger.warning("Invalid assistant message found", 
                                 message_index=i,
                                 error=error_msg)
        
        is_valid = len(errors) == 0
        
        return ValidationResult(
            is_valid=is_valid,
            errors=errors,
            fixed_messages=self.fix_messages(messages) if not is_valid else None
        )
    
    def fix_messages(self, messages: List[Dict[str, Any]]) -> List[Dict[str, Any]]:
        """Automatically fix messages to meet provider requirements."""
        fixed_messages = []
        
        for i, msg in enumerate(messages):
            fixed_msg = msg.copy()
            role = msg.get("role", "").lower()
            content = msg.get("content")
            tool_calls = msg.get("tool_calls")
            
            # Fix assistant messages
            if role == "assistant":
                if not content and not tool_calls:
                    fixed_msg["content"] = ""
                    logger.info("Fixed assistant message by adding empty content",
                               message_index=i)
            
            fixed_messages.append(fixed_msg)
        
        return fixed_messages
    
    def extract_error_info(self, error_message: str) -> Dict[str, Any]:
        """Extract structured information from LLM API error messages."""
        error_info = {
            "original_error": error_message,
            "error_type": "unknown",
            "message_index": None,
            "provider": self.provider,
            "is_fixable": False
        }
        
        # Parse the specific error from the user's report
        if "messages[" in error_message and "] assistant must provide content or tool_calls" in error_message:
            match = re.search(r"messages\[(\d+)\]", error_message)
            if match:
                error_info["message_index"] = int(match.group(1))
                error_info["error_type"] = "missing_content_or_tool_calls"
                error_info["is_fixable"] = True
        
        return error_info


def validate_and_fix_messages(
    messages: List[Dict[str, Any]], 
    provider: str = "openrouter"
) -> Tuple[bool, List[Dict[str, Any]], List[str]]:
    """Convenience function to validate and fix messages."""
    validator = LLMMessageValidator(provider=provider)
    result = validator.validate_messages(messages)
    
    if not result.is_valid:
        fixed_messages = validator.fix_messages(messages)
        final_result = validator.validate_messages(fixed_messages)
        return final_result.is_valid, fixed_messages, final_result.errors
    
    return True, messages, []


def create_safe_assistant_message(
    content: Optional[str] = None,
    tool_calls: Optional[List[Dict[str, Any]]] = None
) -> Dict[str, Any]:
    """Create a safe assistant message that meets API requirements."""
    message = {"role": "assistant"}
    
    if tool_calls:
        message["tool_calls"] = tool_calls
    else:
        message["content"] = content if content is not None else ""
    
    return message