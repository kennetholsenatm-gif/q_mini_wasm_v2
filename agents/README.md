# Auto-Improvement Agent System

This module provides an auto-improvement cycle for agents using Gemini API with llama-index. The system continuously analyzes, plans, implements, and validates improvements to agent performance.

## Architecture

```
agents/
+-- __init__.py              # Package initialization
+-- base_agent.py            # Base agent class
+-- research_agent.py        # Pattern analysis agent
+-- analysis_agent.py        # Performance evaluation agent
+-- code_agent.py            # Code generation agent
+-- test_agent.py            # Testing agent
+-- documentation_agent.py   # Documentation agent
+-- improvement_cycle.py     # Cycle orchestrator
+-- cli.py                   # Command-line interface
+-- config.json              # Configuration file
+-- requirements.txt         # Python dependencies
+-- llm/
    +-- __init__.py          # LLM module init
    +-- gemini_service.py    # Gemini API integration
+-- improvement/             # Improvement algorithms
+-- tools/                   # Tool integrations
+-- memory/                  # Persistent storage
```

## Key Features

- **Multi-Agent Architecture**: Specialized agents for different tasks
- **Gemini Integration**: Uses Gemini API (free tier) via llama-index
- **Rate Limiting**: Built-in rate limiting for free tier (15 RPM, 1M TPM, 1500 RPD)
- **Continuous Improvement**: Automated improvement cycles
- **Memory System**: Persistent storage of patterns and improvements
- **MCP Integration**: Works with existing MCP servers

## Setup

### 1. Install Dependencies

```bash
cd agents
pip install -r requirements.txt
```

### 2. Set API Key

```bash
export GEMINI_API_KEY="your-api-key-here"
```

### 3. Generate Configuration

```bash
python -m agents.cli generate-config
```

## Usage

### Run Single Cycle

```bash
python -m agents.cli run-cycle
```

### Run Continuous Improvement

```bash
python -m agents.cli continuous
```

### Check Status

```bash
python -m agents.cli status
```

### Test LLM Integration

```bash
python -m agents.cli test-llm -p "What are the key principles of quantum computing?"
```

## Improvement Cycle Phases

1. **Analysis**: Identify patterns and bottlenecks
2. **Planning**: Create improvement plan
3. **Implementation**: Generate and apply improvements
4. **Validation**: Test improvements
5. **Deployment**: Deploy validated improvements

## Configuration

The config.json file contains:

- **Gemini Settings**: Model, temperature, rate limits
- **Agent Configs**: System prompts, tools, iterations
- **Cycle Settings**: Phase durations, success criteria
- **Monitoring**: Metrics, alert thresholds
- **Security**: API key rotation, input validation

## Rate Limiting

The system respects Gemini free tier limits:

- **RPM**: 15 requests per minute
- **TPM**: 1,000,000 tokens per minute
- **RPD**: 1,500 requests per day

## Memory System

Each agent maintains:

- **Short-term memory**: Current task context
- **Long-term memory**: Persistent patterns and improvements
- **Pattern storage**: Identified patterns with confidence scores
- **Improvement storage**: Applied improvements with impact metrics

 

### Documentation Agent

The Documentation Agent automatically generates and maintains documentation:

- **Mermaid Diagrams**: Architecture, data flow, user journey diagrams
- **Cognitive Ergonomics**: Validates against project standards
- **Wiki Sync**: Automatically syncs to GitHub Wiki
- **CI/CD Integration**: Runs with every CI/CD rotation

#### Generated Diagrams

1. System Architecture
2. Data Flow
3. User Journey
4. Component Interaction
5. Build Pipeline

## Integration with MCP Servers

The system integrates with existing MCP servers:

- qminiwasm-self-learning: Pattern detection
- qminiwasm-doc-intelligence: Documentation analysis
- qminiwasm-test-orchestrator: Test coordination

## Development

### Running Tests

```bash
python agents/test_basic.py
```

### Adding New Agents

1. Create agent class inheriting from BaseAgent
2. Implement required methods:
   - execute_task()
   - analyze_performance()
   - suggest_improvements()
3. Register in config.json

## Future Enhancements

- [ ] Full implementation of specialized agents
- [ ] Integration with project's C++/Go codebase
- [ ] Web UI for monitoring and control
- [ ] Advanced pattern recognition algorithms
- [ ] Multi-model support (beyond Gemini)

## References

- [Gemini API Documentation](https://ai.google.dev/gemini-api/docs)
- [LlamaIndex Documentation](https://docs.llamaindex.ai/)
- [q_mini_wasm_v2 Project](../README.md)

