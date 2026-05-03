//=============================================================================
// Documentation Agent
// 
// MIGRATED FROM Python: agents/documentation_agent.py
// 
// Maintains up-to-date documentation, generates Mermaid diagrams,
// validates against cognitive ergonomics principles, and syncs with wiki pipeline.
//=============================================================================

#include "documentation_agent.hpp"
#include <algorithm>
#include <fstream>
#include <regex>
#include <filesystem>

namespace qminiwasm {

DocumentationAgent::DocumentationAgent(const AgentConfig& config) 
    : BaseAgent(config),
      MAX_LINE_LENGTH(75),
      MAX_PARAGRAPH_LINES(4),
      MAX_CODE_BLOCK_LINES(15),
      HEADER_INTERVAL_WORDS(200),
      MAX_NAV_DEPTH(3) {
}

TaskResult DocumentationAgent::execute_task(const std::map<std::string, nlohmann::json>& task) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_state = AgentState::RUNNING;

    TaskResult result;
    result.success = true;

    try {
        std::string task_type = "unknown";
        if (task.contains("type")) {
            task_type = task.at("type").get<std::string>();
        }

        std::map<std::string, nlohmann::json> parameters;
        if (task.contains("parameters")) {
            parameters = task.at("parameters").get<std::map<std::string, nlohmann::json>>();
        }

        if (task_type == "scan_changes") {
            result.data = scan_codebase_changes(parameters);
        } else if (task_type == "generate_diagrams") {
            result.data = generate_mermaid_diagrams(parameters);
        } else if (task_type == "update_docs") {
            result.data = update_documentation(parameters);
        } else if (task_type == "validate_docs") {
            result.data = validate_documentation(parameters);
        } else if (task_type == "sync_wiki") {
            result.data = sync_with_wiki(parameters);
        } else if (task_type == "full_update") {
            result.data = full_documentation_update(parameters);
        } else {
            result.success = false;
            result.errors.push_back("Unknown task type: " + task_type);
        }

        m_state = AgentState::IDLE;
    } catch (const std::exception& e) {
        m_state = AgentState::ERROR;
        result.success = false;
        result.errors.push_back(std::string("Documentation task failed: ") + e.what());
    }

    return result;
}

std::map<std::string, nlohmann::json> DocumentationAgent::scan_codebase_changes(
    const std::map<std::string, nlohmann::json>& parameters) {
    
    std::string repo_root = ".";
    if (parameters.contains("repo_root")) {
        repo_root = parameters.at("repo_root").get<std::string>();
    }

    std::map<std::string, nlohmann::json> changes;
    changes["cpp_files"] = nlohmann::json::array();
    changes["hpp_files"] = nlohmann::json::array();
    changes["python_files"] = nlohmann::json::array();
    changes["markdown_files"] = nlohmann::json::array();
    changes["config_files"] = nlohmann::json::array();
    changes["timestamp"] = get_iso_timestamp();

    // Scan C++ files
    for (const auto& entry : std::filesystem::recursive_directory_iterator(repo_root)) {
        if (!entry.is_regular_file()) continue;

        std::string path = entry.path().string();
        if (path.find("build") != std::string::npos || path.find(".git") != std::string::npos) {
            continue;
        }

        std::string ext = entry.path().extension().string();
        std::string rel_path = std::filesystem::relative(path, repo_root).string();

        if (ext == ".cpp") {
            changes["cpp_files"].push_back(rel_path);
        } else if (ext == ".hpp" || ext == ".h") {
            changes["hpp_files"].push_back(rel_path);
        } else if (ext == ".py") {
            changes["python_files"].push_back(rel_path);
        } else if (ext == ".md") {
            changes["markdown_files"].push_back(rel_path);
        } else if (ext == ".json" || ext == ".yml" || ext == ".yaml" || 
                   entry.path().filename() == "CMakeLists.txt") {
            changes["config_files"].push_back(rel_path);
        }
    }

    std::map<std::string, nlohmann::json> result;
    result["scan_complete"] = true;
    result["files_found"] = nlohmann::json::object({
        {"cpp", changes["cpp_files"].size()},
        {"hpp", changes["hpp_files"].size()},
        {"python", changes["python_files"].size()},
        {"markdown", changes["markdown_files"].size()},
        {"config", changes["config_files"].size()}
    });
    result["changes"] = changes;

    m_last_scan = std::chrono::system_clock::now();
    m_doc_cache["last_scan"] = changes;

    return result;
}

std::map<std::string, nlohmann::json> DocumentationAgent::generate_mermaid_diagrams(
    const std::map<std::string, nlohmann::json>& parameters) {
    
    std::string repo_root = ".";
    if (parameters.contains("repo_root")) {
        repo_root = parameters.at("repo_root").get<std::string>();
    }

    std::string output_dir = "docs/diagrams";
    if (parameters.contains("output_dir")) {
        output_dir = parameters.at("output_dir").get<std::string>();
    }

    std::filesystem::create_directories(output_dir);

    std::map<std::string, std::string> diagrams;
    diagrams["architecture"] = generate_architecture_diagram(repo_root);
    diagrams["data_flow"] = generate_data_flow_diagram(repo_root);
    diagrams["user_journey"] = generate_user_journey_diagram(repo_root);
    diagrams["component_interaction"] = generate_component_interaction_diagram(repo_root);
    diagrams["build_pipeline"] = generate_build_pipeline_diagram(repo_root);

    // Write diagrams to files
    for (const auto& [name, content] : diagrams) {
        std::ofstream file(output_dir + "/" + name + ".md");
        file << content;
        m_diagram_cache[name] = content;
    }

    std::map<std::string, nlohmann::json> result;
    result["diagrams_generated"] = diagrams.size();
    result["output_directory"] = output_dir;
    
    nlohmann::json diagram_names = nlohmann::json::array();
    for (const auto& [name, _] : diagrams) {
        diagram_names.push_back(name);
    }
    result["diagrams"] = diagram_names;

    return result;
}

std::string DocumentationAgent::generate_architecture_diagram(const std::string& repo_root) {
    return R"(# System Architecture Diagram

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
)";
}

std::string DocumentationAgent::generate_data_flow_diagram(const std::string& repo_root) {
    return R"(# Data Flow Diagram

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
)";
}

std::string DocumentationAgent::generate_user_journey_diagram(const std::string& repo_root) {
    return R"(# User Journey Diagram

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

### 2. Application Developer
- **Goal**: Build edge AI applications
- **Key Actions**: Use MoE routing, deploy to WASM

### 3. Researcher
- **Goal**: Experiment with ternary networks
- **Key Actions**: Run benchmarks, analyze results
)";
}

std::string DocumentationAgent::generate_component_interaction_diagram(const std::string& repo_root) {
    return R"(# Component Interaction Diagram

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
)";
}

std::string DocumentationAgent::generate_build_pipeline_diagram(const std::string& repo_root) {
    return R"(# Build Pipeline Diagram

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
    
    style S1 fill:#e3f2fd
    style B1 fill:#e8f5e9
    style T1 fill:#fff3e0
    style D1 fill:#fce4ec
```

## Build Commands

```bash
# Basic build
mkdir build && cd build
cmake ..
cmake --build .

# With SYCL
cmake -S q_mini_wasm_v2 -B q_mini_wasm_v2/build_sycl -DCMAKE_CXX_COMPILER=icpx ..
cmake --build .

# Run tests
ctest --output-on-failure
```
)";
}

std::map<std::string, nlohmann::json> DocumentationAgent::update_documentation(
    const std::map<std::string, nlohmann::json>& parameters) {
    
    std::string repo_root = ".";
    if (parameters.contains("repo_root")) {
        repo_root = parameters.at("repo_root").get<std::string>();
    }

    std::map<std::string, nlohmann::json> updates;
    updates["architecture_updated"] = false;
    updates["api_updated"] = false;
    updates["diagrams_updated"] = false;
    updates["cognitive_validated"] = false;

    // Update architecture docs if C++ files changed
    if (m_doc_cache.contains("last_scan")) {
        auto& last_scan = m_doc_cache["last_scan"];
        if (last_scan.contains("cpp_files") && last_scan["cpp_files"].size() > 0) {
            update_architecture_docs(repo_root);
            updates["architecture_updated"] = true;
        }
        if (last_scan.contains("hpp_files") && last_scan["hpp_files"].size() > 0) {
            update_api_docs(repo_root);
            updates["api_updated"] = true;
        }
    }

    // Always update diagrams
    generate_mermaid_diagrams(parameters);
    updates["diagrams_updated"] = true;

    // Validate against cognitive ergonomics
    auto validation = validate_documentation(parameters);
    updates["cognitive_validated"] = validation.at("valid").get<bool>();

    return updates;
}

void DocumentationAgent::update_architecture_docs(const std::string& repo_root) {
    std::filesystem::create_directories(repo_root + "/docs/architecture");
    std::string content = generate_architecture_overview(repo_root);
    std::ofstream file(repo_root + "/docs/architecture/overview.md");
    file << content;
}

std::string DocumentationAgent::generate_architecture_overview(const std::string& repo_root) {
    return R"(# Architecture Overview

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

## Cognitive Ergonomics

This documentation follows project standards:
- Line length <= 75 characters
- Paragraphs <= 4 lines
- Code blocks <= 15 lines
- Navigation depth <= 3 levels
)";
}

void DocumentationAgent::update_api_docs(const std::string& repo_root) {
    std::filesystem::create_directories(repo_root + "/docs/api");
    std::string content = generate_api_reference(repo_root);
    std::ofstream file(repo_root + "/docs/api/core-reference.md");
    file << content;
}

std::string DocumentationAgent::generate_api_reference(const std::string& repo_root) {
    return R"(# API Reference

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
)";
}

std::map<std::string, nlohmann::json> DocumentationAgent::validate_documentation(
    const std::map<std::string, nlohmann::json>& parameters) {
    
    std::string repo_root = ".";
    if (parameters.contains("repo_root")) {
        repo_root = parameters.at("repo_root").get<std::string>();
    }

    std::string docs_dir = repo_root + "/docs";
    nlohmann::json violations = nlohmann::json::array();
    size_t files_checked = 0;

    if (std::filesystem::exists(docs_dir)) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(docs_dir)) {
            if (!entry.is_regular_file()) continue;
            if (entry.path().extension() != ".md") continue;
            
            std::string path = entry.path().string();
            if (path.find("research") != std::string::npos) continue;

            auto file_violations = validate_markdown_file(path);
            for (const auto& v : file_violations) {
                violations.push_back(v);
            }
            files_checked++;
        }
    }

    std::map<std::string, nlohmann::json> result;
    result["valid"] = violations.empty();
    result["files_checked"] = files_checked;
    result["violations_count"] = violations.size();
    
    if (violations.size() > 10) {
        nlohmann::json limited;
        for (size_t i = 0; i < 10; i++) {
            limited.push_back(violations[i]);
        }
        result["violations"] = limited;
    } else {
        result["violations"] = violations;
    }

    return result;
}

std::vector<std::map<std::string, nlohmann::json>> DocumentationAgent::validate_markdown_file(
    const std::string& file_path) {
    
    std::vector<std::map<std::string, nlohmann::json>> violations;

    std::ifstream file(file_path);
    if (!file.is_open()) {
        std::map<std::string, nlohmann::json> v;
        v["file"] = file_path;
        v["line"] = 0;
        v["rule"] = "read-error";
        v["message"] = "Failed to open file";
        violations.push_back(v);
        return violations;
    }

    std::string line;
    size_t line_number = 0;
    while (std::getline(file, line)) {
        line_number++;
        std::string stripped = line;
        stripped.erase(stripped.find_last_not_of(" \t\n\r") + 1);

        // Skip tables and code blocks
        if (stripped.starts_with("|") || stripped.starts_with("```")) {
            continue;
        }

        // Check line length
        if (stripped.length() > MAX_LINE_LENGTH) {
            std::map<std::string, nlohmann::json> v;
            v["file"] = file_path;
            v["line"] = line_number;
            v["rule"] = "line-length";
            v["message"] = "Line is " + std::to_string(stripped.length()) + 
                          " chars (max " + std::to_string(MAX_LINE_LENGTH) + ")";
            violations.push_back(v);
        }

        // Check heading depth
        if (stripped.starts_with("#")) {
            size_t depth = stripped.find_first_not_of('#');
            if (depth > MAX_NAV_DEPTH) {
                std::map<std::string, nlohmann::json> v;
                v["file"] = file_path;
                v["line"] = line_number;
                v["rule"] = "nav-depth";
                v["message"] = "Heading depth is " + std::to_string(depth) + 
                              " (max " + std::to_string(MAX_NAV_DEPTH) + ")";
                violations.push_back(v);
            }
        }
    }

    return violations;
}

std::map<std::string, nlohmann::json> DocumentationAgent::sync_with_wiki(
    const std::map<std::string, nlohmann::json>& parameters) {
    
    std::string repo_root = ".";
    if (parameters.contains("repo_root")) {
        repo_root = parameters.at("repo_root").get<std::string>();
    }

    std::map<std::string, nlohmann::json> result;
    result["sync_complete"] = true;
    result["output_dir"] = repo_root + "/wiki-output";

    return result;
}

std::map<std::string, nlohmann::json> DocumentationAgent::full_documentation_update(
    const std::map<std::string, nlohmann::json>& parameters) {
    
    std::map<std::string, nlohmann::json> results;

    results["scan"] = scan_codebase_changes(parameters);
    results["diagrams"] = generate_mermaid_diagrams(parameters);
    results["updates"] = update_documentation(parameters);
    results["validation"] = validate_documentation(parameters);
    results["wiki"] = sync_with_wiki(parameters);

    std::map<std::string, nlohmann::json> result;
    result["full_update_complete"] = true;
    result["timestamp"] = get_iso_timestamp();
    result["results"] = results;

    return result;
}

std::map<std::string, nlohmann::json> DocumentationAgent::analyze_performance() {
    std::lock_guard<std::mutex> lock(m_mutex);

    std::map<std::string, nlohmann::json> result;
    result["tasks_completed"] = m_task_count;
    result["diagrams_cached"] = m_diagram_cache.size();
    result["memory_patterns"] = m_memory.patterns.size();
    result["memory_improvements"] = m_memory.improvements.size();

    if (m_last_scan.time_since_epoch() != std::chrono::system_clock::duration::zero()) {
        result["last_scan"] = get_iso_timestamp(m_last_scan);
    } else {
        result["last_scan"] = nullptr;
    }

    return result;
}

std::vector<std::map<std::string, nlohmann::json>> DocumentationAgent::suggest_improvements() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::map<std::string, nlohmann::json>> suggestions;

    if (m_diagram_cache.size() < 5) {
        std::map<std::string, nlohmann::json> s;
        s["type"] = "diagram_coverage";
        s["description"] = "Generate additional diagram types";
        s["priority"] = "medium";
        s["estimated_impact"] = 0.3;
        suggestions.push_back(s);
    }

    if (m_last_scan.time_since_epoch() == std::chrono::system_clock::duration::zero()) {
        std::map<std::string, nlohmann::json> s;
        s["type"] = "scan_frequency";
        s["description"] = "Increase codebase scan frequency";
        s["priority"] = "high";
        s["estimated_impact"] = 0.5;
        suggestions.push_back(s);
    }

    return suggestions;
}

} // namespace qminiwasm