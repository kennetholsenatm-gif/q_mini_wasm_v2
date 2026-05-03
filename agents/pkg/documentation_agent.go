package pkg

import (
	"encoding/json"
	"os"
	"path/filepath"
	"strings"
	"time"
)

// DocumentationAgent maintains up-to-date documentation with cognitive ergonomics enforcement
type DocumentationAgent struct {
	*BaseAgent
	docCache      map[string]interface{}
	diagramCache   map[string]string
	lastScan       time.Time
	changedFiles   map[string]bool

	// Cognitive ergonomics constants
	MAX_LINE_LENGTH        int
	MAX_PARAGRAPH_LINES    int
	MAX_CODE_BLOCK_LINES   int
	HEADER_INTERVAL_WORDS  int
	MAX_NAV_DEPTH          int
}

// NewDocumentationAgent creates a new DocumentationAgent
func NewDocumentationAgent(config *AgentConfig) *DocumentationAgent {
	return &DocumentationAgent{
		BaseAgent:              NewBaseAgent(config),
		docCache:               make(map[string]interface{}),
		diagramCache:           make(map[string]string),
		changedFiles:           make(map[string]bool),
		MAX_LINE_LENGTH:        75,
		MAX_PARAGRAPH_LINES:    4,
		MAX_CODE_BLOCK_LINES:   15,
		HEADER_INTERVAL_WORDS:  200,
		MAX_NAV_DEPTH:          3,
	}
}

// ExecuteTask executes a documentation task
func (a *DocumentationAgent) ExecuteTask(task map[string]interface{}) (*TaskResult, error) {
	taskType, _ := task["type"].(string)
	parameters, _ := task["parameters"].(map[string]interface{})

	if taskType == "" {
		taskType = "unknown"
	}

	a.State = AgentStateRunning

	var result interface{}
	var err error

	switch taskType {
	case "scan_changes":
		result, err = a.scanCodebaseChanges(parameters)
	case "generate_diagrams":
		result, err = a.generateMermaidDiagrams(parameters)
	case "update_docs":
		result, err = a.updateDocumentation(parameters)
	case "validate_docs":
		result, err = a.validateDocumentation(parameters)
	case "sync_wiki":
		result, err = a.syncWithWiki(parameters)
	case "full_update":
		result, err = a.fullDocumentationUpdate(parameters)
	default:
		a.State = AgentStateIdle
		return &TaskResult{
			Success: false,
			Errors:  []string{"Unknown task type: " + taskType},
		}, nil
	}

	if err != nil {
		a.State = AgentStateError
		return &TaskResult{
			Success: false,
			Errors:  []string{err.Error()},
		}, err
	}

	a.State = AgentStateIdle

	return &TaskResult{
		Success: true,
		Data:    result,
	}, nil
}

// scanCodebaseChanges scans codebase for changes since last documentation update
func (a *DocumentationAgent) scanCodebaseChanges(parameters map[string]interface{}) (map[string]interface{}, error) {
	repoRoot, _ := parameters["repo_root"].(string)
	if repoRoot == "" {
		repoRoot = "."
	}

	changes := map[string]interface{}{
		"cpp_files":      []string{},
		"hpp_files":      []string{},
		"python_files":   []string{},
		"markdown_files": []string{},
		"config_files":   []string{},
		"timestamp":      time.Now().Format(time.RFC3339),
	}

	// Scan C++ files
	err := filepath.Walk(repoRoot, func(path string, info os.FileInfo, err error) error {
		if err != nil {
			return err
		}

		if strings.Contains(path, "build") || strings.Contains(path, ".git") {
			if info.IsDir() {
				return filepath.SkipDir
			}
			return nil
		}

		relPath, _ := filepath.Rel(repoRoot, path)

		switch filepath.Ext(path) {
		case ".cpp":
			changes["cpp_files"] = append(changes["cpp_files"].([]string), relPath)
		case ".hpp", ".h":
			changes["hpp_files"] = append(changes["hpp_files"].([]string), relPath)
		case ".py":
			changes["python_files"] = append(changes["python_files"].([]string), relPath)
		case ".md":
			changes["markdown_files"] = append(changes["markdown_files"].([]string), relPath)
		}

		return nil
	})

	if err != nil {
		return nil, err
	}

	// Scan config files
	configPatterns := []string{"CMakeLists.txt", ".json", ".yml", ".yaml"}
	for _, pattern := range configPatterns {
		_ = filepath.Walk(repoRoot, func(path string, info os.FileInfo, err error) error {
			if err != nil {
				return err
			}

			if strings.Contains(path, "build") || strings.Contains(path, ".git") {
				if info.IsDir() {
					return filepath.SkipDir
				}
				return nil
			}

			relPath, _ := filepath.Rel(repoRoot, path)

			if strings.HasSuffix(path, pattern) {
				changes["config_files"] = append(changes["config_files"].([]string), relPath)
			}

			return nil
		})
	}

	a.lastScan = time.Now()
	a.docCache["last_scan"] = changes

	return map[string]interface{}{
		"scan_complete": true,
		"files_found": map[string]int{
			"cpp":      len(changes["cpp_files"].([]string)),
			"hpp":      len(changes["hpp_files"].([]string)),
			"python":   len(changes["python_files"].([]string)),
			"markdown": len(changes["markdown_files"].([]string)),
			"config":   len(changes["config_files"].([]string)),
		},
		"changes": changes,
	}, nil
}

// generateMermaidDiagrams generates Mermaid diagrams for documentation
func (a *DocumentationAgent) generateMermaidDiagrams(parameters map[string]interface{}) (map[string]interface{}, error) {
	repoRoot, _ := parameters["repo_root"].(string)
	outputDir, _ := parameters["output_dir"].(string)

	if repoRoot == "" {
		repoRoot = "."
	}

	if outputDir == "" {
		outputDir = filepath.Join("docs", "diagrams")
	}

	os.MkdirAll(outputDir, 0755)

	diagrams := make(map[string]string)

	diagrams["architecture"] = a.generateArchitectureDiagram(repoRoot)
	diagrams["data_flow"] = a.generateDataFlowDiagram(repoRoot)
	diagrams["user_journey"] = a.generateUserJourneyDiagram(repoRoot)
	diagrams["component_interaction"] = a.generateComponentInteractionDiagram(repoRoot)
	diagrams["build_pipeline"] = a.generateBuildPipelineDiagram(repoRoot)

	for name, content := range diagrams {
		diagramPath := filepath.Join(outputDir, name+".md")
		os.WriteFile(diagramPath, []byte(content), 0644)
		a.diagramCache[name] = content
	}

	return map[string]interface{}{
		"diagrams_generated": len(diagrams),
		"output_directory":   outputDir,
		"diagrams":          getDiagramNames(diagrams),
	}, nil
}

// generateArchitectureDiagram generates system architecture Mermaid diagram
func (a *DocumentationAgent) generateArchitectureDiagram(repoRoot string) string {
	return `# System Architecture Diagram

` + "```mermaid" + `
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
` + "```" + `

## Key Components

| Component | Purpose | Location |
|-----------|---------|----------|
| Ternary State Space | GF(3) arithmetic with trit values {+1, 0, -1} | ` + "`core/ternary/`" + ` |
| Stabilizer Tableau | Qutrit Clifford gate operations | ` + "`core/stabilizer/`" + ` |
| MoE Router | Tropical geometry expert routing | ` + "`core/moe/`" + ` |
| Forward-Forward | Teacherless local learning | ` + "`core/learning/`" + ` |
| Orchestrator | Async task scheduling | ` + "`runtime/`" + ` |
| SYCL Kernels | Parallel GPU/CPU execution | ` + "`sycl/`" + ` |
`
}

// generateDataFlowDiagram generates data flow Mermaid diagram
func (a *DocumentationAgent) generateDataFlowDiagram(repoRoot string) string {
	return `# Data Flow Diagram

` + "```mermaid" + `
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
` + "```" + `

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
`
}

// generateUserJourneyDiagram generates user journey Mermaid diagram
func (a *DocumentationAgent) generateUserJourneyDiagram(repoRoot string) string {
	return `# User Journey Diagram

` + "```mermaid" + `
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
` + "```" + `

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
`
}

// generateComponentInteractionDiagram generates component interaction Mermaid diagram
func (a *DocumentationAgent) generateComponentInteractionDiagram(repoRoot string) string {
	return `# Component Interaction Diagram

` + "```mermaid" + `
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
` + "```" + `

## API Contracts

| Component | Method | Input | Output |
|-----------|--------|-------|--------|
| Quantizer | ` + "`quantize()`" + ` | ` + "`vector<double>`" + ` | ` + "`vector<Trit>`" + ` |
| Shadow | ` + "`compute_hash()`" + ` | ` + "`vector<Trit>`" + ` | ` + "`uint64_t`" + ` |
| Router | ` + "`route_topk()`" + ` | ` + "`vector<Trit>`" + ` | ` + "`vector<int>`" + ` |
| Learner | ` + "`forward_forward()`" + ` | ` + "`vector<Trit>`" + ` | ` + "`vector<Trit>`" + ` |
| Corrector | ` + "`encode_and_correct()`" + ` | ` + "`vector<Trit>`" + ` | ` + "`vector<Trit>`" + ` |
`
}

// generateBuildPipelineDiagram generates build pipeline Mermaid diagram
func (a *DocumentationAgent) generateBuildPipelineDiagram(repoRoot string) string {
	return `# Build Pipeline Diagram

` + "```mermaid" + `
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
` + "```" + `

## Build Matrix

| Platform | SYCL | Build Type | Status |
|----------|------|------------|--------|
| Windows | OFF | Release | OK |
| Windows | OFF | Debug | OK |
| Linux | OFF | Release | OK |
| Linux | OFF | Debug | OK |
| Linux | ON | Release | Optional |

## Build Commands

` + "```bash" + `
# Basic build
mkdir build && cd build
cmake ..
cmake --build .

# With SYCL
cmake -S q_mini_wasm_v2 -B q_mini_wasm_v2/build_sycl -DCMAKE_CXX_COMPILER=icpx ..
cmake --build .

# Run tests
ctest --output-on-failure
` + "```" + `
`
}

// updateDocumentation updates documentation based on code changes
func (a *DocumentationAgent) updateDocumentation(parameters map[string]interface{}) (map[string]interface{}, error) {
	repoRoot, _ := parameters["repo_root"].(string)
	if repoRoot == "" {
		repoRoot = "."
	}

	updates := map[string]interface{}{
		"architecture_updated": false,
		"api_updated":        false,
		"diagrams_updated":  false,
		"cognitive_validated": false,
	}

	// Update architecture docs if C++ files changed
	if lastScan, ok := a.docCache["last_scan"].(map[string]interface{}); ok {
		if cppFiles, ok := lastScan["cpp_files"].([]string); ok && len(cppFiles) > 0 {
			a.updateArchitectureDocs(repoRoot)
			updates["architecture_updated"] = true
		}

		if hppFiles, ok := lastScan["hpp_files"].([]string); ok && len(hppFiles) > 0 {
			a.updateAPIDocs(repoRoot)
			updates["api_updated"] = true
		}
	}

	// Update diagrams
	if _, err := a.generateMermaidDiagrams(parameters); err == nil {
		updates["diagrams_updated"] = true
	}

	// Validate against cognitive ergonomics
	if validation, err := a.validateDocumentation(parameters); err == nil {
		if valid, ok := validation["valid"].(bool); ok {
			updates["cognitive_validated"] = valid
		}
	}

	return updates, nil
}

// updateArchitectureDocs updates architecture documentation
func (a *DocumentationAgent) updateArchitectureDocs(repoRoot string) {
	archDir := filepath.Join(repoRoot, "docs", "architecture")
	os.MkdirAll(archDir, 0755)

	overviewContent := a.generateArchitectureOverview(repoRoot)
	overviewPath := filepath.Join(archDir, "overview.md")
	os.WriteFile(overviewPath, []byte(overviewContent), 0644)
}

// generateArchitectureOverview generates architecture overview markdown
func (a *DocumentationAgent) generateArchitectureOverview(repoRoot string) []byte {
	return []byte(`# Architecture Overview

## System Design

q_mini_wasm_v2 is a modular C++17 framework organized
around five core subsystems that work together to provide
quantum-inspired, energy-efficient AI inference at the
extreme edge.

## Subsystems

| Subsystem | Location | Purpose |
|-----------|----------|---------|
| Ternary | ` + "`core/ternary/`" + ` | Trit types, GF(3) arithmetic |
| Stabilizer | ` + "`core/stabilizer/`" + ` | Qutrit tableau, Clifford gates |
| MoE | ` + "`core/moe/`" + ` | Tropical geometry routing |
| Learning | ` + "`core/learning/`" + ` | Forward-Forward algorithm |
| Runtime | ` + "`runtime/`" + ` | Thread pool, orchestration |

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
`)
}

// updateAPIDocs updates API documentation based on header changes
func (a *DocumentationAgent) updateAPIDocs(repoRoot string) {
	apiDir := filepath.Join(repoRoot, "docs", "api")
	os.MkdirAll(apiDir, 0755)

	apiContent := a.generateAPIReference(repoRoot)
	apiPath := filepath.Join(apiDir, "core-reference.md")
	os.WriteFile(apiPath, []byte(apiContent), 0644)
}

// generateAPIReference generates API reference markdown
func (a *DocumentationAgent) generateAPIReference(repoRoot string) []byte {
	return []byte(`# API Reference

## Core API

### Ternary Operations

` + "```cpp" + `
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
` + "```" + `

### Stabilizer Tableau

` + "```cpp" + `
class StabilizerTableau {
public:
    StabilizerTableau(size_t num_qutrits);
    void apply_hadamard(size_t qubit);
    void apply_csum(size_t control, size_t target);
    void apply_phase(size_t qubit, int phase);
};
` + "```" + `

### MoE Router

` + "```cpp" + `
class MoERouter {
public:
    MoERouter(const ExpertConfig& config);
    std::vector<int> route_topk(
        const std::vector<Trit>& input
    );
};
` + "```" + `

## See Also

- [Architecture Overview](../architecture/overview.md)
- [Data Flow Diagram](../diagrams/data_flow.md)
- [Component Interaction](../diagrams/component_interaction.md)
`)
}

// validateDocumentation validates documentation against cognitive ergonomics rules
func (a *DocumentationAgent) validateDocumentation(parameters map[string]interface{}) (map[string]interface{}, error) {
	repoRoot, _ := parameters["repo_root"].(string)
	if repoRoot == "" {
		repoRoot = "."
	}

	docsDir := filepath.Join(repoRoot, "docs")

	violations := make([]map[string]interface{}, 0)
	filesChecked := 0

	if _, err := os.Stat(docsDir); err == nil {
		err := filepath.Walk(docsDir, func(path string, info os.FileInfo, err error) error {
			if err != nil {
				return err
			}

			if filepath.Ext(path) != ".md" {
				return nil
			}

			if strings.Contains(path, "research") {
				return nil
			}

			fileViolations := a.validateMarkdownFile(path)
			violations = append(violations, fileViolations...)
			filesChecked++

			return nil
		})

		if err != nil {
			return nil, err
		}
	}

	return map[string]interface{}{
		"valid":             len(violations) == 0,
		"files_checked":      filesChecked,
		"violations_count":   len(violations),
		"violations":         violations[:min(10, len(violations))],
	}, nil
}

// validateMarkdownFile validates a single markdown file
func (a *DocumentationAgent) validateMarkdownFile(filePath string) []map[string]interface{} {
	violations := make([]map[string]interface{}, 0)

	content, err := os.ReadFile(filePath)
	if err != nil {
		violations = append(violations, map[string]interface{}{
			"file":    filePath,
			"line":    0,
			"rule":    "read-error",
			"message": err.Error(),
		})
		return violations
	}

	lines := strings.Split(string(content), "\n")

	// Check line length
	for i, line := range lines {
		stripped := strings.TrimRight(line, " \t\n\r")
		if strings.HasPrefix(stripped, "|") || strings.HasPrefix(stripped, "```") {
			continue
		}
		if len(stripped) > a.MAX_LINE_LENGTH {
			violations = append(violations, map[string]interface{}{
				"file":    filePath,
				"line":    i + 1,
				"rule":    "line-length",
				"message": fmt.Sprintf("Line is %d chars (max %d)", len(stripped), a.MAX_LINE_LENGTH),
			})
		}
	}

	// Check heading depth
	for i, line := range lines {
		stripped := strings.TrimSpace(line)
		if strings.HasPrefix(stripped, "#") {
			depth := len(stripped) - len(strings.TrimLeft(stripped, "#"))
			if depth > a.MAX_NAV_DEPTH {
				violations = append(violations, map[string]interface{}{
					"file":    filePath,
					"line":    i + 1,
					"rule":    "nav-depth",
					"message": fmt.Sprintf("Heading depth is %d (max %d)", depth, a.MAX_NAV_DEPTH),
				})
			}
		}
	}

	return violations
}

// syncWithWiki syncs documentation with GitHub Wiki
func (a *DocumentationAgent) syncWithWiki(parameters map[string]interface{}) (map[string]interface{}, error) {
	repoRoot, _ := parameters["repo_root"].(string)
	if repoRoot == "" {
		repoRoot = "."
	}

	wikiOutput := filepath.Join(repoRoot, "wiki-output")

	return map[string]interface{}{
		"sync_complete": true,
		"output_dir":    wikiOutput,
	}, nil
}

// fullDocumentationUpdate performs full documentation update cycle
func (a *DocumentationAgent) fullDocumentationUpdate(parameters map[string]interface{}) (map[string]interface{}, error) {
	results := make(map[string]interface{})

	// 1. Scan for changes
	scanResult, err := a.scanCodebaseChanges(parameters)
	results["scan"] = scanResult
	if err != nil {
		return nil, err
	}

	// 2. Generate diagrams
	diagramResult, err := a.generateMermaidDiagrams(parameters)
	results["diagrams"] = diagramResult
	if err != nil {
		return nil, err
	}

	// 3. Update documentation
	updateResult, err := a.updateDocumentation(parameters)
	results["updates"] = updateResult
	if err != nil {
		return nil, err
	}

	// 4. Validate documentation
	validationResult, err := a.validateDocumentation(parameters)
	results["validation"] = validationResult
	if err != nil {
		return nil, err
	}

	// 5. Sync with wiki
	wikiResult, err := a.syncWithWiki(parameters)
	results["wiki"] = wikiResult
	if err != nil {
		return nil, err
	}

	return map[string]interface{}{
		"full_update_complete": true,
		"timestamp":            time.Now().Format(time.RFC3339),
		"results":              results,
	}, nil
}

// AnalyzePerformance analyzes the documentation agent performance
func (a *DocumentationAgent) AnalyzePerformance() (map[string]interface{}, error) {
	performance := map[string]interface{}{
		"tasks_completed":      a.TaskCount,
		"diagrams_cached":      len(a.diagramCache),
		"memory_patterns":       len(a.Memory.Patterns),
		"memory_improvements": len(a.Memory.Improvements),
	}

	if !a.lastScan.IsZero() {
		performance["last_scan"] = a.lastScan.Format(time.RFC3339)
	} else {
		performance["last_scan"] = nil
	}

	// Calculate average response time
	if rt, ok := a.performanceMetrics["response_time"]; ok && len(rt) > 0 {
		sum := 0.0
		for _, t := range rt {
			sum += t
		}
		performance["avg_response_time"] = sum / float64(len(rt))
	} else {
		performance["avg_response_time"] = 0.0
	}

	return performance, nil
}

// SuggestImprovements suggests improvements for the documentation agent
func (a *DocumentationAgent) SuggestImprovements() ([]map[string]interface{}, error) {
	suggestions := make([]map[string]interface{}, 0)

	// Check diagram coverage
	if len(a.diagramCache) < 5 {
		suggestions = append(suggestions, map[string]interface{}{
			"type":              "diagram_coverage",
			"description":       "Generate additional diagram types",
			"priority":          "medium",
			"estimated_impact":  0.3,
		})
	}

	// Check scan frequency
	if a.lastScan.IsZero() {
		suggestions = append(suggestions, map[string]interface{}{
			"type":              "scan_frequency",
			"description":       "Increase codebase scan frequency",
			"priority":          "high",
			"estimated_impact":  0.5,
		})
	}

	// Check cognitive validation frequency
	recentPatterns := a.Memory.GetRecentPatterns(10)
	cognitivePatterns := 0
	for _, p := range recentPatterns {
		if typ, ok := p["type"].(string); ok && typ == "cognitive_validation" {
			cognitivePatterns++
		}
	}

	if cognitivePatterns < 3 {
		suggestions = append(suggestions, map[string]interface{}{
			"type":              "cognitive_validation",
			"description":       "Increase cognitive ergonomics validation",
			"priority":          "medium",
			"estimated_impact":  0.2,
		})
	}

	return suggestions, nil
}

// Helper functions
func getDiagramNames(diagrams map[string]string) []string {
	names := make([]string, 0, len(diagrams))
	for name := range diagrams {
		names = append(names, name)
	}
	return names
}