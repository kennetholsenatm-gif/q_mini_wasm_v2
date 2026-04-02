# AutoLearn Integration for q_mini_wasm_v2

This document describes the AutoLearn integration for the q_mini_wasm_v2 quantum-classical hybrid framework. AutoLearn enables self-improving AI agents through autonomous skill development and managed memory systems.

## Overview

AutoLearn integration provides:
1. **AutoLearn MCP** - Converts AI agent reasoning traces into deterministic, reusable code
2. **AutoLearn School** - Teacher-student model with managed memory and structured testing
3. **Cross-Agent Spread** - Mechanism to propagate improvements across all AI agents

## Architecture

### Components

1. **AutoLearn MCP Server** (`.mcp-servers/qminiwasm-autolearn.json`)
   - Captures reasoning traces from AI agents
   - Crystallizes successful patterns into deterministic skills
   - Manages skill libraries for each agent

2. **Discrete Metrics** (`config/metrics.json`)
   - 40+ quantifiable metrics across 10 AI agent skills
   - Each metric is testable, measurable, and improvable
   - Examples: `trit_operation_correctness`, `binding_generation_success`, `gpu_utilization_percentage`

3. **Evaluation Framework** (`config/evaluation.json`)
   - AutoLearn School testing system
   - Teacher agent (self_learning) evaluates student agents
   - Weighted evaluation criteria for each agent

4. **Spread Mechanism** (`config/autolearn-spread.json`)
   - Rules for which improvements can spread to which agents
   - Validation requirements for cross-agent improvements
   - Knowledge graph integration

5. **Memory System** (`config/autolearn-memory/`)
   - Skill libraries for each agent
   - Improvement history tracking
   - Metrics history for trend analysis

## AI Agent Skills with AutoLearn

### 1. Quantum Core (`quantum_core`)
- **Metrics**: trit_operation_correctness, gf3_arithmetic_coverage, stabilizer_correctness_score, clifford_gate_accuracy, energy_efficiency_pj
- **Improvement Triggers**: test_failure, performance_regression, coverage_drop, energy_efficiency_degradation
- **Spreadable To**: dll_bridge, go_runtime, sycl_acceleration, runtime_engine, test_orchestration

### 2. DLL Bridge (`dll_bridge`)
- **Metrics**: binding_generation_success, api_coverage_percentage, type_mapping_accuracy, memory_safety_score
- **Improvement Triggers**: binding_generation_failure, type_mapping_error, memory_leak_detected, api_coverage_drop
- **Spreadable To**: go_runtime, wui_design, test_orchestration

### 3. Go Runtime (`go_runtime`)
- **Metrics**: build_success_rate, cgo_binding_accuracy, error_handling_coverage, test_pass_rate
- **Improvement Triggers**: build_failure, test_failure, performance_regression, concurrency_bug
- **Spreadable To**: dll_bridge, rag_service, test_orchestration

### 4. SYCL Acceleration (`sycl_acceleration`)
- **Metrics**: kernel_compilation_success, gpu_utilization_percentage, memory_bandwidth_efficiency, parallel_efficiency_score
- **Improvement Triggers**: kernel_compilation_failure, incorrect_gpu_output, performance_degradation, memory_allocation_error
- **Spreadable To**: runtime_engine, quantum_core

### 5. Runtime Engine (`runtime_engine`)
- **Metrics**: wasm_compilation_success, execution_speedup_vs_native, memory_overhead_ratio, threading_stability
- **Improvement Triggers**: wasm_compilation_failure, execution_error, memory_leak, threading_deadlock
- **Spreadable To**: sycl_acceleration, wui_design

### 6. RAG Service (`rag_service`)
- **Metrics**: retrieval_accuracy, context_optimization_score, index_coverage, query_response_time_ms
- **Improvement Triggers**: retrieval_accuracy_drop, query_timeout, indexing_failure, coverage_gap
- **Spreadable To**: documentation_intelligence, test_orchestration

### 7. Documentation Intelligence (`documentation_intelligence`)
- **Metrics**: api_documentation_coverage, cognitive_ergonomics_score, wiki_sync_success_rate, documentation_freshness_days
- **Improvement Triggers**: documentation_gap_detected, cognitive_ergonomics_violation, wiki_sync_failure, outdated_documentation
- **Spreadable To**: rag_service, wui_design, test_orchestration

### 8. Test Orchestration (`test_orchestration`)
- **Metrics**: test_coverage_percentage, test_pass_rate, multi_lang_coordination_score, regression_detection_rate
- **Improvement Triggers**: coverage_drop, test_failure_spike, regression_missed, coordination_failure
- **Spreadable To**: quantum_core, dll_bridge, go_runtime, sycl_acceleration, runtime_engine, rag_service, documentation_intelligence, wui_design

### 9. WUI Design (`wui_design`)
- **Metrics**: build_success_rate, cognitive_css_compliance, visualization_accuracy, user_interaction_responsiveness
- **Improvement Triggers**: visualization_error, css_violation, build_failure, performance_regression
- **Spreadable To**: documentation_intelligence

### 10. Self-Learning (`self_learning`)
- **Metrics**: pattern_detection_accuracy, tool_suggestion_relevance, schema_evolution_success, learning_rate_per_cycle
- **Improvement Triggers**: pattern_detection_failure, irrelevant_suggestion, schema_evolution_failure, learning_stagnation
- **Spreadable To**: All agents (teacher agent)

## AutoLearn Workflow

### 1. Evaluation Cycle
```
evaluate -> detect -> improve -> validate -> spread
```

### 2. Improvement Cycle
1. **Evaluation**: Teacher agent evaluates all student agents against metrics
2. **Detection**: Identify improvement opportunities where metrics are below target
3. **Improvement**: Generate improvement plans using AutoLearn MCP
4. **Validation**: Test improvements against evaluation criteria
5. **Spread**: Propagate validated improvements to applicable agents

### 3. Spread Mechanism
1. Improvement validated in source agent
2. Analysis determines which other agents can benefit
3. Improvement adapted for each target agent's context
4. Validation on each target agent
5. Deployment with rollback capability

## Integration Points

### Git Hooks
- **pre-commit**: Validate metrics and skill definitions
- **post-commit**: Capture reasoning traces and update metrics
- **pre-push**: Run agent evaluations and detect improvements

### CI/CD
- **GitHub Actions**: Weekly improvement cycles
- **Continuous evaluation**: On every push to main/develop

### MCP Hooks
- **pre-tool-use**: Capture tool usage for learning
- **post-tool-use**: Evaluate results and crystallize successful patterns

## Usage

### 1. Running Evaluations
```bash
# Evaluate all agents
python scripts/autolearn_evaluate_agents.py

# Evaluate specific agent
python scripts/autolearn_evaluate_agents.py --agent quantum_core
```

### 2. Detecting Improvements
```bash
# Detect improvement opportunities
python scripts/autolearn_detect_improvements.py

# Detect for specific agent
python scripts/autolearn_detect_improvements.py --agent dll_bridge
```

### 3. Spreading Improvements
```bash
# Spread validated improvement
python scripts/autolearn_spread_improvement.py --improvement-id <id>

# Spread to specific agents
python scripts/autolearn_spread_improvement.py --improvement-id <id> --targets go_runtime test_orchestration
```

## Metrics Tracking

Metrics are tracked in `config/metrics-history.json` with:
- Baseline establishment
- Historical snapshots
- Trend analysis per metric per agent

## Skill Libraries

Each agent maintains a skill library in `config/autolearn-memory/skill-libraries.json`:
- Skills crystallized from reasoning traces
- Usage statistics
- Cross-agent shared skills

## Reporting

AutoLearn generates reports in `reports/autolearn/`:
- Daily evaluation summaries
- Improvement logs
- Metric trends
- Regression alerts
- Cross-agent knowledge transfer reports

## Configuration Files

| File | Purpose |
|------|---------|
| `config/autolearn.json` | Main AutoLearn configuration |
| `config/metrics.json` | Discrete metrics for all agents |
| `config/evaluation.json` | AutoLearn School evaluation framework |
| `config/autolearn-spread.json` | Cross-agent spread mechanism |
| `config/autolearn-hooks.json` | Integration hooks |
| `.mcp-servers/qminiwasm-autolearn.json` | AutoLearn MCP server |

## Best Practices

1. **Metric Design**: Ensure metrics are discrete, testable, and measurable
2. **Improvement Validation**: Always validate improvements before spreading
3. **Regression Detection**: Monitor for regressions after improvements
4. **Cross-Agent Learning**: Leverage improvements across related agents
5. **Memory Management**: Regularly clean up old skill library entries

## Future Enhancements

1. **Visual Dashboard**: Real-time metrics visualization
2. **Automated Skill Suggestion**: AI-powered skill recommendations
3. **Performance Benchmarking**: Automated performance comparison
4. **Community Improvements**: Share improvements across projects

## References

- [AutoLearn.dev](https://autolearn.dev)
- [AutoLearn MCP Experiment](https://autolearn.dev#autolearn-mcp)
- [AutoLearn School Experiment](https://autolearn.dev#autolearn-school)
