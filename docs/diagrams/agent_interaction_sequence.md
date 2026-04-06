# Agent System Interaction Sequence

## Overview

This diagram shows the interaction sequence between agents in the QMINIWASM multi-agent system.

```mermaid
sequenceDiagram
    participant User
    participant RA as Research<br/>Alignment Agent
    participant CA as Code Agent
    participant TA as Test Agent
    participant DA as Documentation<br/>Agent
    participant KA as Kanban Review<br/>Fix Agent
    participant Git as GitHub API
    participant LLM as LLM Provider

    %% Full Analysis Flow
    rect rgb(230, 245, 255)
        Note over User,LLM: Full Analysis & Implementation Flow
        User->>RA: RunFullAlignmentAnalysis(research, code)
        RA->>RA: ParseResearchDocument()
        RA->>RA: AnalyzeCodebase()
        RA->>RA: CheckAlignment()
        RA->>LLM: GenerateAlignedCode()
        LLM-->>RA: Generated templates
        RA-->>User: AlignmentReport
    end

    %% Code Generation Flow
    rect rgb(255, 245, 230)
        Note over User,LLM: Code Generation Flow
        User->>CA: GenerateCode(language, description)
        CA->>CA: Select template
        CA->>CA: Fill requirements
        CA-->>User: GeneratedCodeResult
    end

    %% Test Generation & Execution
    rect rgb(230, 255, 230)
        Note over User,TA: Test Execution Flow
        User->>TA: GenerateTests(sourceFile)
        TA->>TA: Extract functions
        TA->>TA: Generate test cases
        User->>TA: RunTests(testFile, "Python")
        TA->>TA: exec.Command(pytest)
        TA->>TA: Parse output
        TA-->>User: TestResult
    end

    %% Documentation Flow
    rect rgb(255, 230, 255)
        Note over User,DA: Documentation Flow
        User->>DA: GenerateDocs(codePath)
        DA->>DA: Parse comments
        DA->>DA: Extract API
        DA->>DA: Build markdown
        DA-->>User: Documentation
    end

    %% Kanban Review Flow
    rect rgb(255, 255, 230)
        Note over User,Git: Kanban Review Flow
        User->>KA: ReviewKanbanCards()
        KA->>Git: Fetch cards
        Git-->>KA: Card data
        KA->>KA: Identify stuck cards
        KA->>KA: Determine actions
        KA->>Git: Create fix PRs
        KA-->>User: ReviewFixResults
    end

    %% Parallel Processing
    rect rgb(240, 240, 240)
        Note over RA,KA: Parallel Agent Execution
        par Parallel Processing
            RA->>RA: Research alignment
        and
            CA->>CA: Code generation
        and
            TA->>TA: Test execution
        and
            DA->>DA: Doc generation
        and
            KA->>KA: Kanban review
        end
    end
```

## Agent Communication Patterns

```mermaid
graph TB
    subgraph "Orchestration Layer"
        ORCH[Agent Orchestrator]
        MQ[Message Queue]
    end

    subgraph "Core Agents"
        RA[Research Alignment]
        CA[Code Agent]
        TA[Test Agent]
        DA[Documentation]
        KA[Kanban Review]
        IA[Improvement Cycle]
    end

    subgraph "External Services"
        LLM[LLM Provider]
        GIT[GitHub API]
        QDRANT[Qdrant Vector DB]
        WASM[WASM Runtime]
    end

    ORCH --> MQ
    MQ --> RA
    MQ --> CA
    MQ --> TA
    MQ --> DA
    MQ --> KA
    MQ --> IA

    RA --> LLM
    CA --> LLM
    KA --> GIT
    IA --> QDRANT
    TA --> WASM

    RA -.->|Alignment Results| CA
    CA -.->|Code Changes| TA
    TA -.->|Test Results| IA
    IA -.->|Improvements| DA
    DA -.->|Docs| KA

    style ORCH fill:#e1f5ff
    style LLM fill:#fff4e1
    style GIT fill:#ffe1e1
```

## Agent State Machine

```mermaid
stateDiagram-v2
    [*] --> Idle
    Idle --> Processing: Task received
    
    Processing --> Analyzing: Parse input
    Analyzing --> Executing: Analysis complete
    
    Executing --> Waiting: Need external data
    Waiting --> Executing: Data received
    
    Executing --> Validating: Action complete
    Validating --> Executing: Validation failed
    Validating --> Complete: Validation passed
    
    Complete --> Idle: Reset
    Complete --> [*]: Shutdown
    
    Processing --> Error: Exception
    Executing --> Error: Exception
    Waiting --> Error: Timeout
    
    Error --> Idle: Retry
    Error --> [*]: Fatal error
```

## Agent Capabilities Matrix

| Agent | Research | Code Gen | Testing | Docs | Git Ops | LLM Calls |
|-------|----------|----------|---------|------|---------|-----------|
| Research Alignment | ✓✓ | ✓ | ✗ | ✗ | ✗ | ✓✓ |
| Code Agent | ✗ | ✓✓ | ✓ | ✗ | ✗ | ✓ |
| Test Agent | ✗ | ✗ | ✓✓ | ✗ | ✗ | ✗ |
| Documentation | ✗ | ✗ | ✗ | ✓✓ | ✗ | ✓ |
| Kanban Review | ✓ | ✓ | ✗ | ✗ | ✓✓ | ✓ |
| Improvement Cycle | ✓ | ✓ | ✓ | ✓ | ✓ | ✓✓ |

*✓✓ = Primary function, ✓ = Secondary function, ✗ = Not applicable*

## Event Types

```mermaid
graph LR
    subgraph "Agent Events"
        E1[TaskAssigned]
        E2[AnalysisComplete]
        E3[CodeGenerated]
        E4[TestsPassed]
        E5[DocumentationUpdated]
        E6[KanbanCardMoved]
    end

    subgraph "System Events"
        S1[LLMResponse]
        S2[GitHubWebhook]
        S3[VectorDBUpdate]
        S4[WASMExecution]
    end

    E1 --> S1
    E2 --> S3
    E3 --> S4
    E4 --> S2
    E5 --> S3
    E6 --> S2
```

---

*Generated: April 6, 2026*
*Diagram Type: Mermaid Sequence & Flow*
