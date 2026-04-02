#!/usr/bin/env python3
"""
Test script for Gemini Agent Improver

Tests the agent's ability to analyze MCP servers and suggest improvements.
"""

import os
import sys
import json
from pathlib import Path

# Add current directory to path
sys.path.insert(0, str(Path(__file__).parent))

def test_imports():
    """Test that all required modules can be imported."""
    print("Testing imports...")
    try:
        from agent import (
            AgentState,
            MCPAnalysisInput,
            ToolImprovementInput,
            SchemaEvolutionInput,
            analyze_mcp_server,
            suggest_tool_improvement,
            evolve_schema,
            create_agent,
            run_analysis
        )
        print("✓ All imports successful")
        return True
    except ImportError as e:
        print(f"✗ Import failed: {e}")
        return False


def test_mcp_server_analysis():
    """Test MCP server analysis tool."""
    print("\nTesting MCP server analysis...")
    try:
        from agent import analyze_mcp_server
        
        # Test with existing MCP server
        result = analyze_mcp_server.invoke({
            "server_name": "qminiwasm-self-learning",
            "analysis_type": "all"
        })
        
        if "error" in result:
            print(f"✗ Analysis failed: {result['error']}")
            return False
        
        print(f"✓ Analysis successful")
        print(f"  - Server: {result.get('server_name')}")
        print(f"  - Tool count: {result.get('tool_count')}")
        print(f"  - Suggestions: {len(result.get('suggestions', []))}")
        return True
    except Exception as e:
        print(f"✗ Test failed: {e}")
        return False


def test_tool_improvement():
    """Test tool improvement suggestions."""
    print("\nTesting tool improvement suggestions...")
    try:
        from agent import suggest_tool_improvement
        
        result = suggest_tool_improvement.invoke({
            "tool_name": "analyze_patterns",
            "improvement_type": "all"
        })
        
        if "error" in result:
            print(f"✗ Tool improvement failed: {result['error']}")
            return False
        
        print(f"✓ Tool improvement successful")
        print(f"  - Tool: {result.get('tool_name')}")
        print(f"  - Suggestions: {len(result.get('suggestions', []))}")
        return True
    except Exception as e:
        print(f"✗ Test failed: {e}")
        return False


def test_schema_evolution():
    """Test schema evolution suggestions."""
    print("\nTesting schema evolution...")
    try:
        from agent import evolve_schema
        
        # Test with existing MCP server config
        config_path = Path(__file__).parent.parent.parent.parent / ".mcp-servers" / "qminiwasm-self-learning.json"
        
        if not config_path.exists():
            print(f"✗ Config file not found: {config_path}")
            return False
        
        result = evolve_schema.invoke({
            "schema_path": str(config_path),
            "evolution_goal": "optimize"
        })
        
        if "error" in result:
            print(f"✗ Schema evolution failed: {result['error']}")
            return False
        
        print(f"✓ Schema evolution successful")
        print(f"  - Schema: {result.get('schema_path')}")
        print(f"  - Suggestions: {len(result.get('suggestions', []))}")
        return True
    except Exception as e:
        print(f"✗ Test failed: {e}")
        return False


def test_agent_creation():
    """Test agent graph creation."""
    print("\nTesting agent creation...")
    try:
        from agent import create_agent
        
        graph = create_agent()
        
        if graph is None:
            print("✗ Agent creation failed")
            return False
        
        print("✓ Agent created successfully")
        print(f"  - Graph nodes: {len(graph.nodes)}")
        return True
    except Exception as e:
        print(f"✗ Test failed: {e}")
        return False


def test_configuration():
    """Test configuration file."""
    print("\nTesting configuration...")
    try:
        config_path = Path(__file__).parent / "config.json"
        
        if not config_path.exists():
            print(f"✗ Config file not found: {config_path}")
            return False
        
        with open(config_path, 'r') as f:
            config = json.load(f)
        
        required_fields = ["name", "version", "model", "agent_config", "integration"]
        for field in required_fields:
            if field not in config:
                print(f"✗ Missing required field: {field}")
                return False
        
        print("✓ Configuration valid")
        print(f"  - Name: {config.get('name')}")
        print(f"  - Version: {config.get('version')}")
        print(f"  - Model: {config.get('model', {}).get('name')}")
        return True
    except Exception as e:
        print(f"✗ Test failed: {e}")
        return False


def run_all_tests():
    """Run all tests."""
    print("=" * 60)
    print("Gemini Agent Improver - Test Suite")
    print("=" * 60)
    
    tests = [
        test_imports,
        test_configuration,
        test_mcp_server_analysis,
        test_tool_improvement,
        test_schema_evolution,
        test_agent_creation
    ]
    
    results = []
    for test in tests:
        try:
            result = test()
            results.append(result)
        except Exception as e:
            print(f"✗ Test crashed: {e}")
            results.append(False)
    
    print("\n" + "=" * 60)
    print("Test Results:")
    print("=" * 60)
    
    passed = sum(results)
    total = len(results)
    
    for i, (test, result) in enumerate(zip(tests, results), 1):
        status = "✓ PASS" if result else "✗ FAIL"
        print(f"{i}. {test.__name__}: {status}")
    
    print(f"\nTotal: {passed}/{total} tests passed")
    
    if passed == total:
        print("\n🎉 All tests passed!")
        return 0
    else:
        print(f"\n⚠️  {total - passed} test(s) failed")
        return 1


if __name__ == "__main__":
    sys.exit(run_all_tests())
