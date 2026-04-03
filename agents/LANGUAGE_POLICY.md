# Agent Language Policy Update

## Overview

This document outlines the policy update to restrict agent implementations to approved languages only.

## Approved Languages

All agents in the `q_mini_wasm_v2` project must be implemented using **only** the following languages:

| Language | Use Case | Extension |
|----------|----------|-----------|
| **C++** | Core agents, performance-critical components | `.cpp`, `.hpp`, `.h` |
| **Go** | System-level agents, CLI tools | `.go` |
| **Rust** | Safety-critical agents | `.rs` |
| **R** | Data analysis and statistical agents | `.r`, `.R` |
| **DLL (C++)** | Windows-specific agent interfaces | `.dll` |

## Prohibited Languages

- **Python** is **NOT** permitted for new agent implementations
- Existing Python agents must be migrated to approved languages

## Migration Plan

### Phase 1: Go Implementations (Priority)

The following Python agents should be migrated to Go:

1. `base_agent.py` → `base_agent.go`
   - Core agent functionality
   - Memory management
   - Rate limiting

2. `improvement_cycle.py` → `improvement_cycle.go`
   - Orchestration logic
   - Phase management

3. `cli.py` → `go_cli/` (already exists)
   - Command-line interface

### Phase 2: C++ Implementations

4. `documentation_agent.py` → `documentation_agent.cpp`
   - Documentation generation
   - Mermaid diagram generation

5. `analysis_agent.py` → `analysis_agent.cpp`
   - Performance analysis

6. `code_agent.py` → `code_agent.cpp`
   - Code generation

7. `test_agent.py` → `test_agent.cpp`
   - Test orchestration

### Phase 3: Already Migrated

The following agents are already implemented in Go:
- `research_alignment_agent.go` - Research alignment
- `cleanup_agent.go` - Cleanup operations

## GitHub Actions Workflow Updates

The following workflow files need updates to remove Python dependencies:

1. `.github/workflows/agent-automation.yml`
   - Remove Python setup steps
   - Update to use Go/C++ builds

2. `.github/workflows/documentation-agent.yml`
   - Replace Python agent calls with C++ binary calls

3. `.github/workflows/gemini-agent-improvement.yml`
   - Migrate from Python to Go implementation

4. `.github/workflows/kanban-review-fix.yml`
   - Update to use Go CLI

5. `.github/workflows/continuous-improvement.yml`
   - Replace Python scripts with Go/C++ equivalents

## Implementation Status

- [x] Policy documentation created
- [x] Go base agent exists (`research_alignment_agent.go`, `cleanup_agent.go`)
- [ ] Go `base_agent.go` needs completion
- [ ] Go `improvement_cycle.go` needs creation
- [ ] C++ agent implementations needed
- [ ] Workflow files need updating
- [ ] Python files need deprecation notices

## References

- Existing Go agents: `agents/research_alignment_agent.go`, `agents/cleanup_agent.go`
- Go CLI: `go_cli/main.go`
- Build system: `q_mini_wasm_v2/CMakeLists.txt` (C++), `go.mod` (Go)