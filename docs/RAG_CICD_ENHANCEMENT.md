# RAG CI/CD Agent Management Enhancement

## Overview
Enhanced the RAG system to manage CI/CD agents, ensuring pipelines keep moving and CI actually does CI.

## New Components

### 1. CI/CD Agent Manager (`scripts/cicd_manager.py`)
- **Agent Registration**: Register CI/CD agents for pipeline execution
- **Job Assignment**: Assign jobs to available agents
- **Stuck Agent Detection**: Detect agents that haven't sent heartbeat
- **Auto-Recovery**: Automatically recover stuck agents by reassigning jobs
- **Status Reporting**: Get overall pipeline health status

### 2. CI/CD Data Models (`scripts/cicd_models.py`)
- **AgentStatus**: IDLE, RUNNING, STUCK, FAILED, RECOVERING
- **PipelineStage**: CHECKOUT, BUILD, TEST, ANALYSIS, DEPLOY
- **AgentInfo**: Agent metadata and state
- **PipelineJob**: Job tracking and retry management

### 3. CI/CD Context Provider (`scripts/cicd_context_provider.py`)
- Provides pipeline context for RAG queries
- Analyzes GitHub Actions workflows
- Generates recommendations based on current state

### 4. MCP Server Configuration (`.mcp-servers/qminiwasm-cicd-manager.json`)
- New MCP server for CI/CD management
- Tools: status, register, recover, assign_job, complete_job

### 5. GitHub Actions Workflow (`.github/workflows/rag-cicd-orchestration.yml`)
- RAG-powered CI/CD orchestration
- Agent health monitoring
- Automatic stuck agent recovery

## Key Features

### Agent Health Monitoring
- Tracks agent heartbeats
- Detects stuck agents (no heartbeat for 10+ minutes)
- Automatic recovery by reassigning jobs

### Job Management
- Assigns jobs to idle agents
- Tracks job status and retry count
- Automatic retry on different agent (up to 3 retries)

### Pipeline Orchestration
- Integrates with existing RAG service
- Provides context for RAG queries
- Monitors pipeline health

## Usage

### Check Agent Status
```bash
python scripts/cicd_manager.py status
```

### Register an Agent
```bash
python scripts/cicd_manager.py register --agent-name "my-agent"
```

### Recover Stuck Agents
```bash
python scripts/cicd_manager.py recover
```

## Integration with Existing System

### RAG Service Tools Added
- `cicd_agent_status` - Get CI/CD agent status
- `cicd_register_agent` - Register new agent
- `cicd_recover_agents` - Recover stuck agents
- `cicd_assign_job` - Assign job to agent
- `cicd_complete_job` - Complete a job

### Kanban Config Updated
- Added `qminiwasm-cicd-manager` to MCP servers
- Added CI/CD manager configuration
- Added `agent_health` to pipeline gates

## Benefits

1. **Pipeline Resilience**: Stuck agents are automatically recovered
2. **Resource Optimization**: Jobs are assigned to available agents
3. **Visibility**: Real-time agent and job status
4. **Integration**: Works with existing RAG and MCP infrastructure
5. **Automation**: CI pipelines self-heal when agents get stuck

## Next Steps

1. Implement full MCP server integration in Go gateway
2. Add metrics collection for agent performance
3. Implement agent resource-based job assignment
4. Add Slack/Discord notifications for stuck agents
5. Create dashboard for pipeline health visualization
