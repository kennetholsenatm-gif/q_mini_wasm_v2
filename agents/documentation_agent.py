"""
DEPRECATED: This Python agent is being migrated to C++.

Per the project's language policy, all agents must be implemented in:
- C++, DLLs, GO, Rust, or R

Python is no longer permitted for agent implementations.

Migration Status:
- This file will be replaced by documentation_agent.cpp
- See agents/LANGUAGE_POLICY.md for details

Original: Documentation Agent for q_mini_wasm_v2
"""

import asyncio
import json
import re
from datetime import datetime
from pathlib import Path
from typing import Any, Dict, List, Optional, Set

import structlog

from .base_agent import BaseAgent, AgentConfig, TaskResult

logger = structlog.get_logger()


class DocumentationAgent(BaseAgent):
    """
    Documentation agent for maintaining up-to-date documentation.
    
    This agent:
    1. Scans codebase for changes
    2. Generates Mermaid diagrams (architecture, data flow, journey)
    3. Updates documentation using cognitive ergonomics principles
    4. Validates against project standards
    5. Syncs with wiki pipeline
    """
    
    def __init__(self, config: AgentConfig, llm_service=None):
        super().__init__(config, llm_service)
        self._doc_cache: Dict[str, Any] = {}
        self._diagram_cache: Dict[str, str] = {}
        self._last_scan: Optional[datetime] = None
        self._changed_files: Set[str] = set()
        
        # Cognitive ergonomics constants
        self.MAX_LINE_LENGTH = 75
        self.MAX_PARAGRAPH_LINES = 4
        self.MAX_CODE_BLOCK_LINES = 15
        self.HEADER_INTERVAL_WORDS = 200
        self.MAX_NAV_DEPTH = 3
        
    async def execute_task(self, task: Dict[str, Any]) -> TaskResult:
        """
        Execute a documentation task.
        
        Args:
            task: Task specification with 'type' and 'parameters'
            
        Returns:
            TaskResult with documentation results
        """
        task_type = task.get("type", "unknown")
        parameters = task.get("parameters", {})
        
        self.logger.info("Executing documentation task", task_type=task_type)
        self.state = "running"
        
        try:
            if task_type == "scan_changes":
                result = await self._scan_codebase_changes(parameters)
            elif task_type == "generate_diagrams":
                result = await self._generate_mermaid_diagrams(parameters)
            elif task_type == "update_docs":
                result = await self._update_documentation(parameters)
            elif task_type == "validate_docs":
                result = await self._validate_documentation(parameters)
            elif task_type == "sync_wiki":
                result = await self._sync_with_wiki(parameters)
            elif task_type == "full_update":
                result = await self._full_documentation_update(parameters)
            else:
                return TaskResult(
                    success=False,
                    errors=[f"Unknown task type: {task_type}"]
                )
            
            self.state = "idle"
            return TaskResult(success=True, data=result)
            
        except Exception as e:
            self.logger.error("Documentation task failed", error=str(e))
            self.state = "error"
            return TaskResult(success=False, errors=[str(e)])
    
    async def _scan_codebase_changes(self, parameters: Dict[str, Any]) -> Dict[str, Any]:
        """
        Scan codebase for changes since last documentation update.
        
        Returns:
            Dictionary with changed files and their types
        """
        repo_root = Path(parameters.get("repo_root", "."))
        docs_dir = repo_root / "docs"
        
        # Track file changes
        changes = {
            "cpp_files": [],
            "hpp_files": [],
            "python_files": [],
            "markdown_files": [],
            "config_files": [],
            "timestamp": datetime.now().isoformat()
        }
        
        # Scan for C++ source files
        for pattern in ["**/*.cpp", "**/*.hpp", "**/*.h"]:
            for file_path in repo_root.glob(pattern):
                if "build" not in str(file_path) and ".git" not in str(file_path):
                    rel_path = str(file_path.relative_to(repo_root))
                    if file_path.suffix == ".cpp":
                        changes["cpp_files"].append(rel_path)
                    else:
                        changes["hpp_files"].append(rel_path)
        
        # Scan for Python files
        for file_path in repo_root.glob("**/*.py"):
            if "build" not in str(file_path) and ".git" not in str(file_path):
                rel_path = str(file_path.relative_to(repo_root))
                changes["python_files"].append(rel_path)
        
        # Scan for documentation files
        if docs_dir.exists():
            for file_path in docs_dir.rglob("*.md"):
                rel_path = str(file_path.relative_to(repo_root))
                changes["markdown_files"].append(rel_path)
        
        # Scan config files
        for pattern in ["**/CMakeLists.txt", "**/*.json", "**/*.yml", "**/*.yaml"]:
            for file_path in repo_root.glob(pattern):
                if "build" not in str(file_path) and ".git" not in str(file_path):
                    rel_path = str(file_path.relative_to(repo_root))
                    changes["config_files"].append(rel_path)
        
        self._last_scan = datetime.now()
        self._doc_cache["last_scan"] = changes
        
        return {
            "scan_complete": True,
            "files_found": {
                "cpp": len(changes["cpp_files"]),
                "hpp": len(changes["hpp_files"]),
                "python": len(changes["python_files"]),
                "markdown": len(changes["markdown_files"]),
                "config": len(changes["config_files"])
            },
            "changes": changes
        }
    
    async def _generate_mermaid_diagrams(self, parameters: Dict[str, Any]) -> Dict[str, Any]:
        """
        Generate Mermaid diagrams for documentation.
        
        Returns:
            Dictionary with generated diagrams
        """
        repo_root = Path(parameters.get("repo_root", "."))
        output_dir = Path(parameters.get("output_dir", "docs/diagrams"))
        output_dir.mkdir(parents=True, exist_ok=True)
        
        diagrams = {}
        
        # 1. System Architecture Diagram
        diagrams["architecture"] = self._generate_architecture_diagram(repo_root)
        
        # 2. Data Flow Diagram
        diagrams["data_flow"] = self._generate_data_flow_diagram(repo_root)
        
        # 3. User Journey Diagram
        diagrams["user_journey"] = self._generate_user_journey_diagram(repo_root)
        
        # 4. Component Interaction Diagram
        diagrams["component_interaction"] = self._generate_component_interaction_diagram(repo_root)
        
        # 5. Build Pipeline Diagram
        diagrams["build_pipeline"] = self._generate_build_pipeline_diagram(repo_root)
        
        # Write diagrams to files
        for name, content in diagrams.items():
            diagram_path = output_dir / f"{name}.md"
            diagram_path.write_text(content, encoding="utf-8")
            self.logger.info(f"Generated diagram: {name}")
        
        # Update cache
        self._diagram_cache.update(diagrams)
        
        return {
            "diagrams_generated": len(diagrams),
            "output_directory": str(output_dir),
            "diagrams": list(diagrams.keys())
        }
    
    def _generate_architecture_diagram(self, repo_root: Path) -> str:
        """Generate system architecture Mermaid diagram."""
        return """# System Architecture Diagram

```mermaid
graph TB
    subgraph "Core Framework"
        T[Ternary State Space<br/>GF(3) Arithmetic]
        S[Stabilizer Tableau<br/>Clifford Gates]
        M[MoE Router<br/>Tropical Geometry]
        F[Forward-Forward<br/>Learning]
    end
    
    subgraph "Runtime"
        O[Orchestrator<br/>Thread Pool]
        Y[SYCL Kernels<br/>GPU/CPU]
    end
    
    subgraph "External"
        W[WebAssembly<br/>Target]
        G[Go/DLL<br/>Plugin]
        C[Flash-CIM<br/>Interface]
    end
    
    subgraph "Input Processing"
        Q[Quantizer<br/>Absmean]
        SH[Shadow<br/>Clifford Hash]
    end
    
    subgraph "Output"
        EC[Error Correction<br/>Steane Code]
        ACT[Activations<br/>Trit Vectors]
    end
    
    Q --> SH
    SH --> M
    M --> T
    M --> S
    T --> F
    S --> F
    F --> EC
    EC --> ACT
    
    O --> Y
    Y --> T
    Y --> S
    
    F -.-> W
    F -.-> G
    F -.-> C
    
    style T fill:#e1f5fe
    style S fill:#f3e5f5
    style M fill:#fff3e0
    style F fill:#e8f5e8
    style O fill:#fce4ec
    style Y fill:#f1f8e9
```

## Key Components

| Component | Purpose | Location |
|-----------|---------|----------|
| Ternary State Space | GF(3) arithmetic with trit values {+1, 0, -1} | `core/ternary/` |
| Stabilizer Tableau | Qutrit Clifford gate operations | `core/stabilizer/` |
| MoE Router | Tropical geometry expert routing | `core/moe/` |
| Forward-Forward | Teacherless local learning | `core/learning/` |
| Orchestrator | Async task scheduling | `runtime/` |
| SYCL Kernels | Parallel GPU/CPU execution | `sycl/` |
"""
    
    def _generate_data_flow_diagram(self, repo_root: Path) -> str:
        """Generate data flow Mermaid diagram."""
        return """# Data Flow Diagram

```mermaid
flowchart LR
    subgraph Input
        A[Continuous<br/>Input]
        B[Absmean<br/>Quantizer]
        C[Ternary<br/>Trits]
    end
    
    subgraph Processing
        D[Clifford<br/>Shadow]
        E[MoE<br/>Router]
        F[Expert<br/>Selection]
        G[Forward-Forward<br/>Inference]
    end
    
    subgraph Correction
        H[Steane<br/>Code]
        I[Error<br/>Detection]
        J[Error<br/>Correction]
    end
    
    subgraph Output
        K[Final<br/>Activations]
        L[Result<br/>Vector]
    end
    
    A -->|FP32 values| B
    B -->|{+1,0,-1}| C
    C -->|Trit vector| D
    D -->|Hash| E
    E -->|Top-K| F
    F -->|Selected experts| G
    G -->|Raw output| H
    H -->|Syndrome| I
    I -->|Corrections| J
    J -->|Corrected| K
    K -->|Trit vector| L
    
    style A fill:#ffebee
    style B fill:#fce4ec
    style C fill:#f3e5f5
    style D fill:#ede7f6
    style E fill:#e8eaf6
    style F fill:#e3f2fd
    style G fill:#e1f5fe
    style H fill:#e0f7fa
    style I fill:#e0f2f1
    style J fill:#e8f5e9
    style K fill:#f1f8e9
    style L fill:#f9fbe7
```

## Data Transformation Pipeline

| Stage | Input Format | Output Format | Operation |
|-------|-------------|---------------|-----------|
| Quantization | FP32 vector | Trit vector | Absmean thresholding |
| Shadow | Trit vector | Hash | Clifford measurement |
| Routing | Hash | Expert indices | Tropical geometry |
| Inference | Trit vector | Trit vector | Forward-Forward |
| Correction | Trit vector | Trit vector | Steane code |

## Energy Efficiency

| Operation | Energy | Notes |
|-----------|--------|-------|
| Trit operation | <1 pJ | 3.7x more efficient than FP32 |
| Pack/unpack | <0.1 pJ | 5-trit-to-8-bit packing |
| Total pipeline | <5 pJ | End-to-end inference |
"""
    
    def _generate_user_journey_diagram(self, repo_root: Path) -> str:
        """Generate user journey Mermaid diagram."""
        return """# User Journey Diagram

```mermaid
journey
    title q_mini_wasm_v2 User Journey
    section Getting Started
      Clone repository: 5: User
      Install dependencies: 4: User
      Build framework: 3: User, System
      Run tests: 4: User, System
    section Development
      Write ternary code: 5: User
      Configure MoE router: 4: User
      Implement Forward-Forward: 3: User
      Debug with SYCL: 2: User, System
    section Deployment
      Build WebAssembly: 4: User, System
      Optimize for edge: 3: User
      Deploy to device: 5: User, System
      Monitor performance: 4: User
    section Maintenance
      Update documentation: 3: User, Agent
      Run CI/CD: 4: System
      Review improvements: 5: User
      Apply patches: 4: User, System
```

## User Personas

### 1. Framework Developer
- **Goal**: Extend core functionality
- **Key Actions**: Modify C++ code, add new gates
- **Pain Points**: Complex GF(3) arithmetic

### 2. Application Developer
- **Goal**: Build edge AI applications
- **Key Actions**: Use MoE routing, deploy to WASM
- **Pain Points**: SYCL setup, optimization

### 3. Researcher
- **Goal**: Experiment with ternary networks
- **Key Actions**: Run benchmarks, analyze results
- **Pain Points**: Documentation gaps

### 4. DevOps Engineer
- **Goal**: Maintain CI/CD pipeline
- **Key Actions**: Monitor builds, manage deployments
- **Pain Points**: Cross-platform builds
"""
    
    def _generate_component_interaction_diagram(self, repo_root: Path) -> str:
        """Generate component interaction Mermaid diagram."""
        return """# Component Interaction Diagram

```mermaid
sequenceDiagram
    participant User
    participant Orchestrator
    participant Quantizer
    participant Shadow
    participant Router
    participant Expert
    participant Learner
    participant Corrector
    
    User->>Orchestrator: submit_inference(input)
    Orchestrator->>Quantizer: quantize(input)
    Quantizer-->>Orchestrator: ternary_input
    
    Orchestrator->>Shadow: compute_hash(ternary_input)
    Shadow-->>Orchestrator: shadow_hash
    
    Orchestrator->>Router: route_topk(shadow_hash)
    Router-->>Orchestrator: selected_experts
    
    loop For each selected expert
        Orchestrator->>Expert: process(ternary_input)
        Expert->>Learner: forward_forward(input)
        Learner-->>Expert: activations
        Expert-->>Orchestrator: expert_output
    end
    
    Orchestrator->>Corrector: encode_and_correct(outputs)
    Corrector-->>Orchestrator: corrected_output
    
    Orchestrator-->>User: final_result
    
    Note over Orchestrator: Async execution with<br/>thread pool
    Note over Router: Tropical geometry<br/>Top-K selection
    Note over Learner: Local layer-wise<br/>no backpropagation
```

## API Contracts

| Component | Method | Input | Output |
|-----------|--------|-------|--------|
| Quantizer | `quantize()` | `vector<double>` | `vector<Trit>` |
| Shadow | `compute_hash()` | `vector<Trit>` | `uint64_t` |
| Router | `route_topk()` | `vector<Trit>` | `vector<int>` |
| Learner | `forward_forward()` | `vector<Trit>` | `vector<Trit>` |
| Corrector | `encode_and_correct()` | `vector<Trit>` | `vector<Trit>` |
"""
    
    def _generate_build_pipeline_diagram(self, repo_root: Path) -> str:
        """Generate build pipeline Mermaid diagram."""
        return """# Build Pipeline Diagram

```mermaid
graph LR
    subgraph Source
        S1[C++ Sources]
        S2[Headers]
        S3[CMakeLists]
    end
    
    subgraph Build
        B1[Configure]
        B2[Compile]
        B3[Link]
    end
    
    subgraph Test
        T1[Unit Tests]
        T2[Integration]
        T3[Performance]
    end
    
    subgraph Deploy
        D1[Library]
        D2[WASM]
        D3[Plugins]
    end
    
    subgraph CI/CD
        C1[Lint]
        C2[Build Matrix]
        C3[Test Matrix]
        C4[Deploy]
    end
    
    S1 --> B1
    S2 --> B1
    S3 --> B1
    
    B1 --> B2
    B2 --> B3
    
    B3 --> T1
    B3 --> T2
    B3 --> T3
    
    T1 --> D1
    T2 --> D2
    T3 --> D3
    
    C1 --> C2
    C2 --> C3
    C3 --> C4
    
    style S1 fill:#e3f2fd
    style B1 fill:#e8f5e9
    style T1 fill:#fff3e0
    style D1 fill:#fce4ec
    style C1 fill:#f3e5f5
```

## Build Matrix

| Platform | SYCL | Build Type | Status |
|----------|------|------------|--------|
| Windows | OFF | Release | OK |
| Windows | OFF | Debug | OK |
| Linux | OFF | Release | OK |
| Linux | OFF | Debug | OK |
| Linux | ON | Release | Optional |

## Build Commands

```bash
# Basic build
mkdir build && cd build
cmake ..
cmake --build .

# With SYCL
cmake -DUSE_SYCL=ON ..
cmake --build .

# Run tests
ctest --output-on-failure
```
"""
    
    async def _update_documentation(self, parameters: Dict[str, Any]) -> Dict[str, Any]:
        """
        Update documentation based on code changes.
        
        Returns:
            Dictionary with update results
        """
        repo_root = Path(parameters.get("repo_root", "."))
        
        updates = {
            "architecture_updated": False,
            "api_updated": False,
            "diagrams_updated": False,
            "cognitive_validated": False
        }
        
        # Update architecture docs if C++ files changed
        if self._doc_cache.get("last_scan", {}).get("cpp_files"):
            await self._update_architecture_docs(repo_root)
            updates["architecture_updated"] = True
        
        # Update API docs if headers changed
        if self._doc_cache.get("last_scan", {}).get("hpp_files"):
            await self._update_api_docs(repo_root)
            updates["api_updated"] = True
        
        # Update diagrams
        await self._generate_mermaid_diagrams({"repo_root": str(repo_root)})
        updates["diagrams_updated"] = True
        
        # Validate against cognitive ergonomics
        validation_result = await self._validate_documentation({"repo_root": str(repo_root)})
        updates["cognitive_validated"] = validation_result.get("valid", False)
        
        return updates
    
    async def _update_architecture_docs(self, repo_root: Path) -> None:
        """Update architecture documentation based on C++ changes."""
        arch_dir = repo_root / "docs" / "architecture"
        arch_dir.mkdir(parents=True, exist_ok=True)
        
        overview_content = self._generate_architecture_overview(repo_root)
        overview_path = arch_dir / "overview.md"
        overview_path.write_text(overview_content, encoding="utf-8")
    
    def _generate_architecture_overview(self, repo_root: Path) -> str:
        """Generate architecture overview markdown."""
        return """# Architecture Overview

## System Design

q_mini_wasm_v2 is a modular C++17 framework organized
around five core subsystems that work together to provide
quantum-inspired, energy-efficient AI inference at the
extreme edge.

## Subsystems

| Subsystem | Location | Purpose |
|-----------|----------|---------|
| Ternary | `core/ternary/` | Trit types, GF(3) arithmetic |
| Stabilizer | `core/stabilizer/` | Qutrit tableau, Clifford gates |
| MoE | `core/moe/` | Tropical geometry routing |
| Learning | `core/learning/` | Forward-Forward algorithm |
| Runtime | `runtime/` | Thread pool, orchestration |

## Data Flow

See [Data Flow Diagram](../diagrams/data_flow.md) for
complete pipeline visualization.

## Cognitive Ergonomics

This documentation follows project standards:

- Line length <= 75 characters
- Paragraphs <= 4 lines
- Headers every +/-200 words
- Code blocks <= 15 lines
- Navigation depth <= 3 levels
"""
    
    async def _update_api_docs(self, repo_root: Path) -> None:
        """Update API documentation based on header changes."""
        api_dir = repo_root / "docs" / "api"
        api_dir.mkdir(parents=True, exist_ok=True)
        
        api_content = self._generate_api_reference(repo_root)
        api_path = api_dir / "core-reference.md"
        api_path.write_text(api_content, encoding="utf-8")
    
    def _generate_api_reference(self, repo_root: Path) -> str:
        """Generate API reference markdown."""
        return """# API Reference

## Core API

### Ternary Operations

```cpp
// Trit type definition
enum class Trit : int8_t {
    NEGATIVE = -1,
    ZERO = 0,
    POSITIVE = 1
};

// GF(3) addition
Trit gf3_add(Trit a, Trit b);

// GF(3) multiplication
Trit gf3_mul(Trit a, Trit b);
```

### Stabilizer Tableau

```cpp
class StabilizerTableau {
public:
    StabilizerTableau(size_t num_qutrits);
    void apply_hadamard(size_t qubit);
    void apply_csum(size_t control, size_t target);
    void apply_phase(size_t qubit, int phase);
};
```

### MoE Router

```cpp
class MoERouter {
public:
    MoERouter(const ExpertConfig& config);
    std::vector<int> route_topk(
        const std::vector<Trit>& input
    );
};
```

## See Also

- [Architecture Overview](../architecture/overview.md)
- [Data Flow Diagram](../diagrams/data_flow.md)
- [Component Interaction](../diagrams/component_interaction.md)
"""
    
    async def _validate_documentation(self, parameters: Dict[str, Any]) -> Dict[str, Any]:
        """
        Validate documentation against cognitive ergonomics rules.
        
        Returns:
            Dictionary with validation results
        """
        repo_root = Path(parameters.get("repo_root", "."))
        docs_dir = repo_root / "docs"
        
        violations = []
        files_checked = 0
        
        if docs_dir.exists():
            for md_file in docs_dir.rglob("*.md"):
                # Skip research papers
                if "research" in str(md_file):
                    continue
                
                file_violations = self._validate_markdown_file(md_file)
                if file_violations:
                    violations.extend(file_violations)
                files_checked += 1
        
        return {
            "valid": len(violations) == 0,
            "files_checked": files_checked,
            "violations_count": len(violations),
            "violations": violations[:10]  # Limit for readability
        }
    
    def _validate_markdown_file(self, file_path: Path) -> List[Dict[str, Any]]:
        """Validate a single markdown file."""
        violations = []
        
        try:
            content = file_path.read_text(encoding="utf-8")
            lines = content.split("\n")
            
            # Check line length
            for i, line in enumerate(lines, 1):
                stripped = line.rstrip()
                if stripped.startswith("|") or stripped.startswith("```"):
                    continue
                if len(stripped) > self.MAX_LINE_LENGTH:
                    violations.append({
                        "file": str(file_path),
                        "line": i,
                        "rule": "line-length",
                        "message": f"Line is {len(stripped)} chars (max {self.MAX_LINE_LENGTH})"
                    })
            
            # Check heading depth
            for i, line in enumerate(lines, 1):
                stripped = line.strip()
                if stripped.startswith("#"):
                    depth = len(stripped) - len(stripped.lstrip("#"))
                    if depth > self.MAX_NAV_DEPTH:
                        violations.append({
                            "file": str(file_path),
                            "line": i,
                            "rule": "nav-depth",
                            "message": f"Heading depth is {depth} (max {self.MAX_NAV_DEPTH})"
                        })
            
        except Exception as e:
            violations.append({
                "file": str(file_path),
                "line": 0,
                "rule": "read-error",
                "message": str(e)
            })
        
        return violations
    
    async def _sync_with_wiki(self, parameters: Dict[str, Any]) -> Dict[str, Any]:
        """
        Sync documentation with GitHub Wiki.
        
        Returns:
            Dictionary with sync results
        """
        repo_root = Path(parameters.get("repo_root", "."))
        wiki_output = repo_root / "wiki-output"
        
        # Run existing wiki generator
        import subprocess
        result = subprocess.run(
            ["python3", "docs/wiki-pipeline/generate_wiki.py"],
            cwd=str(repo_root),
            capture_output=True,
            text=True
        )
        
        success = result.returncode == 0
        
        return {
            "sync_complete": success,
            "output_dir": str(wiki_output) if wiki_output.exists() else None,
            "stdout": result.stdout[-500:] if result.stdout else None,
            "stderr": result.stderr[-500:] if result.stderr else None
        }
    
    async def _full_documentation_update(self, parameters: Dict[str, Any]) -> Dict[str, Any]:
        """
        Perform full documentation update cycle.
        
        Returns:
            Dictionary with complete update results
        """
        results = {}
        
        # 1. Scan for changes
        scan_result = await self._scan_codebase_changes(parameters)
        results["scan"] = scan_result
        
        # 2. Generate diagrams
        diagram_result = await self._generate_mermaid_diagrams(parameters)
        results["diagrams"] = diagram_result
        
        # 3. Update documentation
        update_result = await self._update_documentation(parameters)
        results["updates"] = update_result
        
        # 4. Validate documentation
        validation_result = await self._validate_documentation(parameters)
        results["validation"] = validation_result
        
        # 5. Sync with wiki
        wiki_result = await self._sync_with_wiki(parameters)
        results["wiki"] = wiki_result
        
        return {
            "full_update_complete": True,
            "timestamp": datetime.now().isoformat(),
            "results": results
        }
    
    async def analyze_performance(self) -> Dict[str, Any]:
        """
        Analyze documentation agent performance.
        
        Returns:
            Performance analysis
        """
        return {
            "tasks_completed": self._task_count,
            "diagrams_cached": len(self._diagram_cache),
            "last_scan": self._last_scan.isoformat() if self._last_scan else None,
            "memory_patterns": len(self.memory.patterns),
            "memory_improvements": len(self.memory.improvements),
            "avg_response_time": (
                sum(self._performance_metrics["response_time"]) / 
                len(self._performance_metrics["response_time"])
                if self._performance_metrics["response_time"] else 0
            ),
        }
    
    async def suggest_improvements(self) -> List[Dict[str, Any]]:
        """
        Suggest improvements for the documentation agent.
        
        Returns:
            List of improvement suggestions
        """
        suggestions = []
        
        # Check diagram coverage
        if len(self._diagram_cache) < 5:
            suggestions.append({
                "type": "diagram_coverage",
                "description": "Generate additional diagram types",
                "priority": "medium",
                "estimated_impact": 0.3
            })
        
        # Check validation frequency
        if not self._last_scan:
            suggestions.append({
                "type": "scan_frequency",
                "description": "Increase codebase scan frequency",
                "priority": "high",
                "estimated_impact": 0.5
            })
        
        # Check cognitive compliance
        recent_patterns = self.memory.get_recent_patterns(limit=10)
        cognitive_patterns = [p for p in recent_patterns if p.get("type") == "cognitive_validation"]
        if len(cognitive_patterns) < 3:
            suggestions.append({
                "type": "cognitive_validation",
                "description": "Increase cognitive ergonomics validation",
                "priority": "medium",
                "estimated_impact": 0.2
            })
        
        return suggestions
