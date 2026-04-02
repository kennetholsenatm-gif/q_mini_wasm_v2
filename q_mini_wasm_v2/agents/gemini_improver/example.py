#!/usr/bin/env python3
"""
Example usage of Gemini Agent Improver

Demonstrates how to use the agent for analyzing and improving MCP servers.
"""

import os
import sys
from pathlib import Path

# Add current directory to path
sys.path.insert(0, str(Path(__file__).parent))

def example_basic_analysis():
    """Example: Basic MCP server analysis."""
    print("Example 1: Basic MCP Server Analysis")
    print("-" * 40)
    
    from agent import run_analysis
    
    # Check for API key
    if not os.getenv("GEMINI_API_KEY"):
        print("⚠️  GEMINI_API_KEY not set. Using simulated analysis.")
        print("Set GEMINI_API_KEY environment variable for real analysis.")
        return
    
    # Analyze the self-learning MCP server
    print("Analyzing qminiwasm-self-learning MCP server...")
    results = run_analysis("qminiwasm-self-learning", "all")
    
    print("\nResults:")
    for i, result in enumerate(results, 1):
        print(f"Step {i}: {result['message'][:100]}...")
        if result['tool_calls']:
            print(f"  Tool calls: {len(result['tool_calls'])}")
    
    print("\n✅ Basic analysis example completed")


def example_tool_improvement():
    """Example: Tool improvement suggestions."""
    print("\nExample 2: Tool Improvement Suggestions")
    print("-" * 40)
    
    from agent import suggest_tool_improvement
    
    # Get improvement suggestions for a tool
    print("Getting improvement suggestions for 'analyze_patterns' tool...")
    suggestions = suggest_tool_improvement.invoke({
        "tool_name": "analyze_patterns",
        "improvement_type": "all"
    })
    
    print("\nSuggestions:")
    for suggestion in suggestions.get("suggestions", []):
        print(f"  - {suggestion['type']}: {suggestion['suggestion']}")
    
    print("\n✅ Tool improvement example completed")


def example_schema_evolution():
    """Example: Schema evolution."""
    print("\nExample 3: Schema Evolution")
    print("-" * 40)
    
    from agent import evolve_schema
    
    # Get schema evolution suggestions
    config_path = Path(__file__).parent.parent.parent.parent / ".mcp-servers" / "qminiwasm-self-learning.json"
    
    if config_path.exists():
        print(f"Evolving schema: {config_path}")
        evolution = evolve_schema.invoke({
            "schema_path": str(config_path),
            "evolution_goal": "optimize"
        })
        
        print("\nEvolution suggestions:")
        for suggestion in evolution.get("suggestions", []):
            print(f"  - {suggestion['action']}: {suggestion['description']}")
    else:
        print(f"⚠️  Config file not found: {config_path}")
    
    print("\n✅ Schema evolution example completed")


def example_integration():
    """Example: Integration with self-learning MCP."""
    print("\nExample 4: Integration with Self-Learning MCP")
    print("-" * 40)
    
    from integrate import SelfLearningIntegrator
    
    integrator = SelfLearningIntegrator()
    
    # Generate improvement report
    print("Generating improvement report...")
    report = integrator.generate_improvement_report()
    
    print("\nReport Summary:")
    summary = report.get("summary", {})
    print(f"  Total servers: {summary.get('total_servers', 0)}")
    print(f"  Analyzed servers: {summary.get('analyzed_servers', 0)}")
    print(f"  Total suggestions: {summary.get('total_suggestions', 0)}")
    print(f"  High priority suggestions: {summary.get('high_priority_suggestions', 0)}")
    
    print("\n✅ Integration example completed")


def main():
    """Run all examples."""
    print("=" * 60)
    print("Gemini Agent Improver - Examples")
    print("=" * 60)
    
    examples = [
        example_basic_analysis,
        example_tool_improvement,
        example_schema_evolution,
        example_integration
    ]
    
    for example in examples:
        try:
            example()
        except Exception as e:
            print(f"❌ Example failed: {e}")
    
    print("\n" + "=" * 60)
    print("All examples completed!")
    print("=" * 60)
    
    print("\n📚 Next Steps:")
    print("1. Set GEMINI_API_KEY environment variable for real analysis")
    print("2. Run: python agent.py qminiwasm-self-learning all")
    print("3. Run: python integrate.py all")
    print("4. Check output directory for results")


if __name__ == "__main__":
    main()
