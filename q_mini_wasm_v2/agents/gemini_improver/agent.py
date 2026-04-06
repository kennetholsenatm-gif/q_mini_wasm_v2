#!/usr/bin/env python3
"""
Gemini Agent Improver - Auto-improves MCP agents using Gemini API + LangGraph

This module implements a ReAct agent that analyzes existing MCP servers
and suggests improvements using Google's Gemini API (free tier).

Based on: https://ai.google.dev/gemini-api/docs/langgraph-example
"""

import os
import json
import sys
from pathlib import Path
from typing import Annotated, Sequence, TypedDict

# Add parent directory to path for imports
sys.path.insert(0, str(Path(__file__).parent.parent.parent))

# Import environment loader
from agents.config import env_config, get_env, require_env

from langchain_core.messages import BaseMessage, ToolMessage
from langchain_core.tools import tool
from langchain_google_genai import ChatGoogleGenerativeAI
from langgraph.graph import StateGraph, END
from langgraph.graph.message import add_messages
from pydantic import BaseModel, Field


class AgentState(TypedDict):
    """The state of the agent."""
    messages: Annotated[Sequence[BaseMessage], add_messages]
    number_of_steps: int


class MCPAnalysisInput(BaseModel):
    """Input schema for MCP server analysis."""
    server_name: str = Field(description="Name of the MCP server to analyze")
    analysis_type: str = Field(
        description="Type of analysis: 'performance', 'usability', 'coverage', 'all'",
        default="all"
    )


class ToolImprovementInput(BaseModel):
    """Input schema for tool improvement suggestions."""
    tool_name: str = Field(description="Name of the tool to improve")
    improvement_type: str = Field(
        description="Type of improvement: 'efficiency', 'reliability', 'usability', 'all'",
        default="all"
    )


class SchemaEvolutionInput(BaseModel):
    """Input schema for schema evolution."""
    schema_path: str = Field(description="Path to the schema file")
    evolution_goal: str = Field(
        description="Goal of evolution: 'add_field', 'optimize', 'validate', 'document'",
        default="optimize"
    )


# Define tools for the agent
@tool("analyze_mcp_server", args_schema=MCPAnalysisInput, return_direct=False)
def analyze_mcp_server(server_name: str, analysis_type: str = "all") -> dict:
    """Analyze an MCP server for potential improvements.
    
    Args:
        server_name: Name of the MCP server to analyze
        analysis_type: Type of analysis to perform
        
    Returns:
        Dictionary with analysis results and suggestions
    """
    mcp_dir = Path(__file__).parent.parent.parent.parent / ".mcp-servers"
    server_file = mcp_dir / f"{server_name}.json"
    
    if not server_file.exists():
        return {"error": f"MCP server {server_name} not found"}
    
    try:
        with open(server_file, 'r') as f:
            server_config = json.load(f)
        
        analysis = {
            "server_name": server_name,
            "analysis_type": analysis_type,
            "tool_count": len(server_config.get("tools", [])),
            "resource_count": len(server_config.get("resources", [])),
            "suggestions": []
        }
        
        # Analyze based on type
        if analysis_type in ["performance", "all"]:
            analysis["suggestions"].append({
                "type": "performance",
                "suggestion": "Consider adding caching for frequently accessed resources",
                "priority": "medium"
            })
        
        if analysis_type in ["usability", "all"]:
            # Check if all tools have descriptions
            tools_without_desc = [
                t["name"] for t in server_config.get("tools", [])
                if not t.get("description")
            ]
            if tools_without_desc:
                analysis["suggestions"].append({
                    "type": "usability",
                    "suggestion": f"Add descriptions to tools: {', '.join(tools_without_desc)}",
                    "priority": "high"
                })
        
        if analysis_type in ["coverage", "all"]:
            # Check for input schema completeness
            for tool in server_config.get("tools", []):
                if not tool.get("input_schema"):
                    analysis["suggestions"].append({
                        "type": "coverage",
                        "suggestion": f"Add input schema to tool: {tool['name']}",
                        "priority": "high"
                    })
        
        return analysis
    except Exception as e:
        return {"error": str(e)}


@tool("suggest_tool_improvement", args_schema=ToolImprovementInput, return_direct=False)
def suggest_tool_improvement(tool_name: str, improvement_type: str = "all") -> dict:
    """Suggest improvements for a specific tool.
    
    Args:
        tool_name: Name of the tool to improve
        improvement_type: Type of improvement to suggest
        
    Returns:
        Dictionary with improvement suggestions
    """
    # Analyze tool implementation and suggest concrete improvements
    suggestions = []
    
    # Common tool patterns to check
    if improvement_type in ["all", "efficiency"]:
        suggestions.append({
            "type": "efficiency",
            "suggestion": f"Review {tool_name} for caching opportunities",
            "implementation": "Add @functools.lru_cache decorator for repeated calls with same arguments",
            "rationale": "Reduces redundant computation and improves response time"
        })
        suggestions.append({
            "type": "efficiency",
            "suggestion": f"Consider async implementation for {tool_name}",
            "implementation": "Convert to async/await pattern for I/O-bound operations",
            "rationale": "Improves throughput when handling multiple concurrent requests"
        })
    
    if improvement_type in ["all", "reliability"]:
        suggestions.append({
            "type": "reliability",
            "suggestion": f"Add input validation to {tool_name}",
            "implementation": "Use pydantic models or type hints with validation",
            "rationale": "Prevents errors from invalid inputs and improves error messages"
        })
        suggestions.append({
            "type": "reliability",
            "suggestion": f"Implement retry logic for {tool_name}",
            "implementation": "Add exponential backoff for transient failures",
            "rationale": "Improves robustness against temporary service outages"
        })
    
    if improvement_type in ["all", "usability"]:
        suggestions.append({
            "type": "usability",
            "suggestion": f"Enhance documentation for {tool_name}",
            "implementation": "Add docstrings with examples and type information",
            "rationale": "Makes tool easier to use correctly and discover features"
        })
    
    return {
        "tool_name": tool_name,
        "improvement_type": improvement_type,
        "suggestions": suggestions,
        "analysis_timestamp": time.time() if 'time' in globals() else None
    }


@tool("evolve_schema", args_schema=SchemaEvolutionInput, return_direct=False)
def evolve_schema(schema_path: str, evolution_goal: str = "optimize") -> dict:
    """Evolve a schema based on usage patterns.
    
    Args:
        schema_path: Path to the schema file
        evolution_goal: Goal of the evolution
        
    Returns:
        Dictionary with evolution suggestions
    """
    schema_file = Path(schema_path)
    
    if not schema_file.exists():
        return {"error": f"Schema file {schema_path} not found"}
    
    try:
        with open(schema_file, 'r') as f:
            schema = json.load(f)
        
        evolution = {
            "schema_path": schema_path,
            "evolution_goal": evolution_goal,
            "current_version": schema.get("version", "1.0"),
            "suggestions": []
        }
        
        if evolution_goal == "optimize":
            evolution["suggestions"].append({
                "action": "add_validation",
                "description": "Add JSON Schema validation for all input parameters",
                "priority": "high"
            })
        
        elif evolution_goal == "document":
            evolution["suggestions"].append({
                "action": "add_examples",
                "description": "Add example usage for each tool",
                "priority": "medium"
            })
        
        return evolution
    except Exception as e:
        return {"error": str(e)}


tools = [analyze_mcp_server, suggest_tool_improvement, evolve_schema]


def init_model():
    """Initialize the Gemini model with tools."""
    # Get API key using environment loader
    api_key = env_config.get("GEMINI_API_KEY")
    if not api_key:
        raise ValueError("GEMINI_API_KEY environment variable not set or not found in .env file")
    
    # Create LLM - using Gemini Flash for free tier
    llm = ChatGoogleGenerativeAI(
        model="gemini-2.0-flash",  # Free tier model
        temperature=0.7,
        max_retries=2,
        google_api_key=api_key,
    )
    
    # Bind tools to the model
    model = llm.bind_tools(tools)
    return model


def call_tool(state: AgentState):
    """Execute tool calls from the last message."""
    outputs = []
    tools_by_name = {tool.name: tool for tool in tools}
    
    for tool_call in state["messages"][-1].tool_calls:
        tool_result = tools_by_name[tool_call["name"]].invoke(tool_call["args"])
        outputs.append(
            ToolMessage(
                content=json.dumps(tool_result, indent=2),
                name=tool_call["name"],
                tool_call_id=tool_call["id"],
            )
        )
    
    return {"messages": outputs}


def call_model(state: AgentState, config=None):
    """Call the Gemini model with the current state."""
    model = init_model()
    response = model.invoke(state["messages"], config)
    return {"messages": [response]}


def should_continue(state: AgentState):
    """Determine if the agent should continue or end."""
    messages = state["messages"]
    if not messages[-1].tool_calls:
        return "end"
    return "continue"


def create_agent():
    """Create and compile the agent graph."""
    workflow = StateGraph(AgentState)
    
    # Add nodes
    workflow.add_node("llm", call_model)
    workflow.add_node("tools", call_tool)
    
    # Set entry point
    workflow.set_entry_point("llm")
    
    # Add conditional edges
    workflow.add_conditional_edges(
        "llm",
        should_continue,
        {
            "continue": "tools",
            "end": END,
        },
    )
    
    # Add normal edge
    workflow.add_edge("tools", "llm")
    
    # Compile the graph
    graph = workflow.compile()
    return graph


def run_analysis(server_name: str, analysis_type: str = "all"):
    """Run analysis on an MCP server.
    
    Args:
        server_name: Name of the MCP server to analyze
        analysis_type: Type of analysis to perform
        
    Returns:
        Analysis results
    """
    graph = create_agent()
    
    # Create initial message
    inputs = {
        "messages": [
            ("user", f"Analyze the MCP server '{server_name}' for {analysis_type} improvements. "
                     "Use the analyze_mcp_server tool to get detailed analysis and suggestions.")
        ],
        "number_of_steps": 0
    }
    
    # Run the agent
    results = []
    for state in graph.stream(inputs, stream_mode="values"):
        last_message = state["messages"][-1]
        results.append({
            "step": state.get("number_of_steps", 0),
            "message": str(last_message),
            "tool_calls": getattr(last_message, "tool_calls", [])
        })
    
    return results


def run_improvement_cycle(server_name: str):
    """Run a complete improvement cycle for an MCP server.
    
    Args:
        server_name: Name of the MCP server to improve
        
    Returns:
        Improvement suggestions and actions
    """
    graph = create_agent()
    
    # Create initial message for improvement cycle
    inputs = {
        "messages": [
            ("user", f"Run a complete improvement cycle for MCP server '{server_name}'. "
                     "First analyze the server, then suggest specific improvements for each tool, "
                     "and finally propose schema evolutions. Use all available tools.")
        ],
        "number_of_steps": 0
    }
    
    # Run the agent
    results = []
    for state in graph.stream(inputs, stream_mode="values"):
        last_message = state["messages"][-1]
        results.append({
            "step": state.get("number_of_steps", 0),
            "message": str(last_message),
            "tool_calls": getattr(last_message, "tool_calls", [])
        })
    
    return results


if __name__ == "__main__":
    # Example usage
    if len(sys.argv) < 2:
        print("Usage: agent.py <server_name> [analysis_type]")
        print("Example: agent.py qminiwasm-self-learning all")
        sys.exit(1)
    
    server_name = sys.argv[1]
    analysis_type = sys.argv[2] if len(sys.argv) > 2 else "all"
    
    print(f"Running analysis for {server_name} ({analysis_type})...")
    results = run_analysis(server_name, analysis_type)
    
    print("\n=== Analysis Results ===")
    for i, result in enumerate(results, 1):
        print(f"\nStep {i}:")
        print(f"Message: {result['message'][:200]}...")
        if result['tool_calls']:
            print(f"Tool calls: {len(result['tool_calls'])}")
