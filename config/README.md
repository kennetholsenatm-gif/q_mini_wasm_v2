# q_mini_wasm_v2 Configuration

This directory contains configuration files for the q_mini_wasm_v2 quantum-classical hybrid framework.

## Configuration Files

### rules.json
Coding standards and architectural rules for the project:
- **C++ Standards**: C++17 with specific compiler requirements
- **Quantum-Specific Rules**: GF(3) arithmetic, trit operations, stabilizer formalism
- **Go Standards**: Go 1.26+ with CGO bindings
- **WASM Configuration**: Emscripten toolchain requirements
- **Testing Rules**: Coverage requirements and quantum correctness tests
- **Security Rules**: Memory safety and quantum state isolation

### workflows.json
Development and CI/CD workflow definitions:
- **Feature Development**: Branch naming, pre-commit checks, PR requirements
- **Bug Fix Workflow**: Regression testing and quantum validation
- **Quantum Algorithm Changes**: Research documentation requirements
- **Build Workflows**: C++, Go, and WASM build configurations
- **CI/CD Pipelines**: GitHub Actions integration with build matrices
- **Quantum-Specific Workflows**: Tableau validation and energy efficiency analysis

### hooks.json
Git hooks for automated validation:
- **Pre-commit**: Code formatting, trit validation, floating-point checks
- **Commit Message**: Enforced format with quantum-specific prefixes
- **Pre-push**: Test execution and integration validation
- **Post-commit**: Knowledge graph updates and RAG indexing
- **Post-merge**: Index rebuilding and changelog generation
- **Quantum-Specific Hooks**: Stabilizer correctness and Clifford gate validation

### skills.json
Available skills mapped to MCP servers:
- **quantum_core**: Qutrit stabilizer formalism and GF(3) operations
- **dll_bridge**: Language bindings (Go, Rust, Python, WASM)
- **go_runtime**: Go gateway with CGO bindings
- **documentation_intelligence**: Cognitive ergonomics documentation
- **rag_service**: Retrieval-augmented generation
- **runtime_engine**: WASM orchestrator with async tableau tracking
- **self_learning**: Pattern detection and tool generation
- **sycl_acceleration**: GPU acceleration for quantum operations
- **test_orchestration**: Multi-language test coordination
- **wui_design**: Web UI with cognitive CSS

## Skill Compositions

### Full Quantum Pipeline
```
develop ? bind ? integrate ? test
```
Skills: quantum_core, dll_bridge, go_runtime, test_orchestration

### Documentation Pipeline
```
analyze ? document ? index ? learn
```
Skills: documentation_intelligence, rag_service, self_learning

### Acceleration Pipeline
```
implement ? accelerate ? profile ? optimize
```
Skills: quantum_core, sycl_acceleration, runtime_engine

## Kanban Integration

All skills include Kanban status tracking for pipeline management:
- Quantum Core: `kanban_quantum_status`
- DLL Bridge: `kanban_dll_pipeline`
- Go Runtime: `kanban_go_status`
- RAG Service: `kanban_rag_status`
- Runtime Engine: `kanban_runtime_status`
- SYCL Accelerator: `kanban_sycl_status`
- Test Orchestrator: `kanban_test_gate`
- WUI Designer: `kanban_wui_status`

## Usage

These configuration files are designed to be consumed by:
1. **Cline/Claude**: For AI-assisted development guidance
2. **CI/CD Pipelines**: For automated workflow execution
3. **Git Hooks**: For pre-commit and pre-push validation
4. **MCP Servers**: For skill invocation and tool execution

## Research Foundation

Configuration values are derived from:
- Cognitive Ergonomics Model Protocol
- Qutrit Clifford Entanglement Framework
- QMINIWASM Quantum-Classical Framework Synthesis
- Tropical Geometry MoE Routing Research
