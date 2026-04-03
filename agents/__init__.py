"""
q_mini_wasm_v2 Auto-Improvement Agent System

This module provides an auto-improvement cycle for agents using Gemini API
with llama-index. The system continuously analyzes, plans, implements, and
validates improvements to agent performance.

Key Features:
- Multi-agent architecture for specialized improvement tasks
- Integration with Gemini API (free tier) via llama-index
- Continuous monitoring and feedback loops
- Automated testing and validation
- MCP server integration for tool access

Architecture:
- BaseAgent: Core agent functionality
- ResearchAgent: Pattern analysis and information gathering
- AnalysisAgent: Performance evaluation and bottleneck identification
- CodeAgent: Code generation and refactoring
- TestAgent: Testing and validation
- ImprovementCycle: Orchestrates the improvement process

Usage:
    from agents import ImprovementCycle
    
    cycle = ImprovementCycle()
    await cycle.run()
"""

__version__ = "1.0.0"
__author__ = "q_mini_wasm_v2 team"

# Import only available modules
from .base_agent import BaseAgent, AgentConfig, TaskResult, AgentMemory
from .research_agent import ResearchAgent
from .research_alignment_agent import ResearchAlignmentAgent
from .improvement_cycle import ImprovementCycle

# Import LLM module
from .llm import GeminiService, GeminiConfig

__all__ = [
    "BaseAgent",
    "AgentConfig",
    "TaskResult",
    "AgentMemory",
    "ResearchAgent",
    "ResearchAlignmentAgent",
    "ImprovementCycle",
    "GeminiService",
    "GeminiConfig",
]

# Import Kanban Review Fix Agent
from .kanban_review_fix_agent import KanbanReviewFixAgent, ReviewCard, ReviewStatus, CardAction

__all__.extend([
    "KanbanReviewFixAgent",
    "ReviewCard",
    "ReviewStatus",
    "CardAction",
])
