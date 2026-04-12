# Gemini Agent Improver

Auto-improves MCP agents using Google's Gemini API (free tier).

## Overview

ReAct agent that analyzes MCP servers and suggests improvements:

- **Gemini API** (free tier) for LLM capabilities
- **LangGraph** for stateful orchestration
- **ReAct pattern** for reasoning and acting

## Setup

### API Key

1. Visit [Google AI Studio](https://aistudio.google.com/)
2. Create API key (free tier)
3. Set environment variable:

```powershell
$env:GEMINI_API_KEY = "your-key-here"
```

### Install

```powershell
pip install -r requirements.txt
```

## Usage

### Basic Analysis

```powershell
python agent.py qminiwasm-self-learning all
```

### Analysis Types

| Type | Focus |
|:-----|:------|
| performance | Speed optimizations |
| usability | UX improvements |
| coverage | Feature completeness |
| all | Comprehensive (default) |

### Commands

```powershell
# Self-learning server
python agent.py qminiwasm-self-learning all

# Usability focus
python agent.py qminiwasm-doc-intelligence usability

# With improvement cycle
python agent.py qminiwasm-self-learning all --cycle
```

## Tools

| Tool | Purpose |
|:-----|:--------|
| analyze_mcp_server | Analyze server config |
| suggest_tool_improvement | Improve specific tool |
| evolve_schema | Evolve schema patterns |

## Limits

| Metric | Limit |
|:-------|:------|
| Requests/min | 60 |
| Tokens/min | 32,000 |
| Requests/day | 1,500 |

## Architecture

```
Gemini API
    ↓
LangGraph Agent
    ↓
ReAct Pattern
    ↓
┌─────────────────┐
│ analyze_mcp     │
│ suggest_tool    │
│ evolve_schema   │
└─────────────────┘
    ↓
Improvements
```

## Future Work

- Automated implementation of suggestions
- User feedback learning
- Multi-agent coordination
- CI/CD integration