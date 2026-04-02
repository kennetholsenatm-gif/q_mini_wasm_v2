#!/usr/bin/env python3
"""
Basic tests for Auto-Improvement Agent System

This script provides basic validation tests for the agent system.
"""

import asyncio
import json
import sys
from pathlib import Path

# Add parent directory to path for imports
sys.path.insert(0, str(Path(__file__).parent.parent))

from agents.base_agent import BaseAgent, AgentConfig, TaskResult, AgentMemory
from agents.llm.gemini_service import GeminiService, GeminiConfig


async def test_agent_memory():
    """Test agent memory system."""
    print("Testing AgentMemory...")
    
    memory = AgentMemory()
    
    # Test pattern storage
    pattern = {
        "type": "test_pattern",
        "description": "Test pattern for validation",
        "confidence": 0.9
    }
    
    memory.store_pattern(pattern)
    assert len(memory.patterns) == 1
    assert memory.patterns[0]["type"] == "test_pattern"
    
    # Test improvement storage
    improvement = {
        "type": "test_improvement",
        "description": "Test improvement for validation",
        "impact": 0.5
    }
    
    memory.store_improvement(improvement)
    assert len(memory.improvements) == 1
    assert memory.improvements[0]["type"] == "test_improvement"
    
    # Test recent retrieval
    recent_patterns = memory.get_recent_patterns(limit=5)
    assert len(recent_patterns) == 1
    
    print("? AgentMemory tests passed")


async def test_gemini_config():
    """Test Gemini configuration."""
    print("Testing GeminiConfig...")
    
    config = GeminiConfig(
        api_key_env="TEST_KEY",
        model="gemini-3-flash-preview",
        temperature=0.5,
        max_tokens=4096
    )
    
    assert config.api_key_env == "TEST_KEY"
    assert config.model == "gemini-3-flash-preview"
    assert config.temperature == 0.5
    assert config.max_tokens == 4096
    
    print("? GeminiConfig tests passed")


async def test_agent_config():
    """Test agent configuration."""
    print("Testing AgentConfig...")
    
    config = AgentConfig(
        name="TestAgent",
        description="Test agent for validation",
        system_prompt="You are a test agent.",
        tools=["tool1", "tool2"],
        max_iterations=5
    )
    
    assert config.name == "TestAgent"
    assert config.description == "Test agent for validation"
    assert config.system_prompt == "You are a test agent."
    assert config.tools == ["tool1", "tool2"]
    assert config.max_iterations == 5
    
    print("? AgentConfig tests passed")


async def test_task_result():
    """Test task result model."""
    print("Testing TaskResult...")
    
    result = TaskResult(
        success=True,
        data={"key": "value"},
        errors=[],
        metrics={"metric": 0.5}
    )
    
    assert result.success is True
    assert result.data == {"key": "value"}
    assert result.errors == []
    assert result.metrics == {"metric": 0.5}
    
    # Test with errors
    error_result = TaskResult(
        success=False,
        errors=["Test error"]
    )
    
    assert error_result.success is False
    assert "Test error" in error_result.errors
    
    print("? TaskResult tests passed")


async def test_config_loading():
    """Test configuration loading."""
    print("Testing configuration loading...")
    
    # Create test config
    test_config = {
        "project": "test_project",
        "gemini": {
            "model": "gemini-3-flash-preview",
            "rate_limits": {
                "rpm": 15,
                "tpm": 1000000,
                "rpd": 1500
            }
        }
    }
    
    # Write test config
    config_path = Path("test_config.json")
    config_path.write_text(json.dumps(test_config, indent=2))
    
    # Load config
    loaded_config = json.loads(config_path.read_text())
    assert loaded_config["project"] == "test_project"
    assert loaded_config["gemini"]["model"] == "gemini-3-flash-preview"
    
    # Clean up
    config_path.unlink()
    
    print("? Configuration loading tests passed")


async def run_all_tests():
    """Run all tests."""
    print("Running basic validation tests...\n")
    
    try:
        await test_agent_memory()
        await test_gemini_config()
        await test_agent_config()
        await test_task_result()
        await test_config_loading()
        
        print("\n? All tests passed!")
        return True
        
    except AssertionError as e:
        print(f"\n? Test failed: {e}")
        return False
    except Exception as e:
        print(f"\n? Unexpected error: {e}")
        return False


if __name__ == "__main__":
    success = asyncio.run(run_all_tests())
    sys.exit(0 if success else 1)
