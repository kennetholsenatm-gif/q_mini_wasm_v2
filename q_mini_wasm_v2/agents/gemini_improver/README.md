# Gemini Agent Improver

Auto-improves MCP agents using Google's Gemini API (free tier) with LangGraph.

## Overview

This module implements a ReAct agent that analyzes existing MCP servers and suggests improvements. It uses:

- **Gemini API** (free tier) for LLM capabilities
- **LangGraph** for stateful agent orchestration
- **ReAct pattern** for reasoning and acting

## Setup

### 1. Get Gemini API Key

1. Go to [Google AI Studio](https://aistudio.google.com/)
2. Create a new API key (free tier)
3. Set the environment variable:

```powershell
$env:GEMINI_API_KEY = "your-api-key-here"
```

### 2. Install Dependencies

```powershell
pip install -r requirements.txt
```

## Usage

### Basic Analysis

Analyze an MCP server for improvements:

```powershell
python agent.py qminiwasm-self-learning all
```

### Analysis Types

- performance: Focus on performance improvements
- usability: Focus on user experience improvements
- coverage: Focus on feature coverage
- all: Comprehensive analysis (default)

### Example Commands

```powershell
# Analyze self-learning MCP server
python agent.py qminiwasm-self-learning all

# Analyze documentation intelligence MCP server for usability
python agent.py qminiwasm-doc-intelligence usability

# Run complete improvement cycle
python agent.py qminiwasm-self-learning all --cycle
```

## Agent Tools

### 1. analyze_mcp_server
Analyzes an MCP server configuration and suggests improvements.

**Parameters:**
- server_name: Name of the MCP server
- analysis_type: Type of analysis (performance/usability/coverage/all)

### 2. suggest_tool_improvement
Suggests improvements for a specific tool.

**Parameters:**
- tool_name: Name of the tool to improve
- improvement_type: Type of improvement (efficiency/reliability/usability/all)

### 3. evolve_schema
Evolves a schema based on usage patterns.

**Parameters:**
- schema_path: Path to the schema file
- evolution_goal: Goal of evolution (add_field/optimize/validate/document)

## Free Tier Limits

- **Requests per minute:** 60
- **Tokens per minute:** 32,000
- **Requests per day:** 1,500

## Integration with q_mini_wasm_v2

This agent integrates with the existing self-learning MCP server pattern:

1. **Pattern Analysis**: Uses the existing analyze_patterns tool
2. **Tool Suggestion**: Enhances the suggest_tool tool with Gemini intelligence
3. **Schema Evolution**: Improves the evolve_schema tool with better suggestions

## Example Output

```
Running analysis for qminiwasm-self-learning (all)...

=== Analysis Results ===

Step 1:
Message: I'll analyze the MCP server 'qminiwasm-self-learning' for all improvements...
Tool calls: 1

Step 2:
Message: Based on the analysis, I found several areas for improvement...
Tool calls: 2

Step 3:
Message: Here are my recommendations for improving the MCP server...
```

## Architecture

```
Gemini API (Free Tier)
        ↓
    LangGraph Agent
        ↓
    ReAct Pattern
        ↓
┌─────────────────────────────────────┐
│  Tools:                            │
│  1. analyze_mcp_server             │
│  2. suggest_tool_improvement       │
│  3. evolve_schema                  │
└─────────────────────────────────────┘
        ↓
    MCP Server Configs
        ↓
    Improvement Suggestions
```

## Future Enhancements

1. **Automated Implementation**: Automatically implement suggested improvements
2. **Learning from Feedback**: Learn from user feedback to improve suggestions
3. **Multi-Agent Collaboration**: Coordinate multiple agents for complex improvements
4. **Integration with CI/CD**: Integrate with GitHub Actions for automated improvements