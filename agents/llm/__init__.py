"""
LLM Module for Auto-Improvement System

This module provides integration with various LLM services,
with a focus on Gemini API via llama-index.
"""

from .gemini_service import GeminiService, GeminiConfig
from .message_validator import (
    LLMMessageValidator,
    ValidationResult,
    validate_and_fix_messages,
    create_safe_assistant_message
)

__all__ = [
    "GeminiService",
    "GeminiConfig",
    "LLMMessageValidator",
    "ValidationResult",
    "validate_and_fix_messages",
    "create_safe_assistant_message",
]
