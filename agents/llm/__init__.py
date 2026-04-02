"""
LLM Module for Auto-Improvement System

This module provides integration with various LLM services,
with a focus on Gemini API via llama-index.
"""

from .gemini_service import GeminiService, GeminiConfig

__all__ = [
    "GeminiService",
    "GeminiConfig",
]
