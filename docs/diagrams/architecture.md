# System Architecture Diagram

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
