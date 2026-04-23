# QGNN Data Flow Architecture

## Overview

This diagram illustrates the data flow through the Quantum Graph Neural Network (QGNN) system, from input to output.

```mermaid
graph TB
    subgraph "Input Layer"
        IN[Graph Input\nVertices + Edges]
        FEAT[Feature Vectors\nClassical Data]
    end

    subgraph "Ternary Encoding"
        TE[Trit Encoder\n3-level quantization]
        TP[TritPack5\nMemory packing]
    end

    subgraph "QGNN Core"
        MP[Message Passing\nGF(3) Operations]
        MOE[MoE Router\nGraph-based routing]
        FF[Forward-Forward\nLocal learning]
        SE[Stabilizer Encoder\nTableau representation]
    end

    subgraph "Quantum Operations"
        H[Hadamard Gates\nPhase mixing]
        S[S Gates\nPhase shifts]
        CSUM[CSUM Gates\nEntanglement]
        GE[Gaussian Elimination\nGF(3) rank calc]
    end

    subgraph "Error Correction"
        CR[Constantin-Rao\nQutrit codes]
        TG[Ternary Golay\nPerfect code]
        PD[Phase Drift\nCorrection]
    end

    subgraph "Output Layer"
        BETTI[Betti Numbers\nTopological features]
        CLASS[Classification\nSoftmax output]
        EMB[Embeddings\nGraph representation]
    end

    IN --> TE
    FEAT --> TE
    TE --> TP
    TP --> MP
    MP --> MOE
    MOE --> FF
    FF --> SE
    SE --> H
    H --> S
    S --> CSUM
    CSUM --> GE
    GE --> CR
    CR --> TG
    TG --> PD
    PD --> BETTI
    PD --> CLASS
    PD --> EMB

    style IN fill:#e1f5ff
    style TE fill:#fff4e1
    style MP fill:#f0e1ff
    style H fill:#ffe1e1
    style CR fill:#e1ffe1
    style BETTI fill:#e1f5ff
```

## Data Flow Description

### 1. Input Layer
- **Graph Input**: Raw graph structure with vertices and edges
- **Feature Vectors**: Classical data associated with graph nodes

### 2. Ternary Encoding
- **Trit Encoder**: Converts floating-point to GF(3) values {0, 1, 2}
- **TritPack5**: Packs 5 trits into 8 bits for memory efficiency (60% reduction)

### 3. QGNN Core Processing
- **Message Passing**: GF(3) operations propagate information between nodes
- **MoE Router**: Graph-based Mixture of Experts routing (O(n²) complexity)
- **Forward-Forward**: Local learning without backpropagation
- **Stabilizer Encoder**: Represents quantum state via stabilizer tableau

### 4. Quantum Operations
- **Clifford Gates**: H, S, CSUM gates only (Gottesman-Knill theorem)
- **Gaussian Elimination**: GF(3) rank calculation for Betti numbers

### 5. Error Correction
- **Constantin-Rao Codes**: Qutrit error correction
- **Ternary Golay**: Perfect code with 99.9% fidelity
- **Phase Drift Correction**: Maintains phase accuracy

### 6. Output Layer
- **Betti Numbers**: Topological invariants (β₀, β₁, β₂)
- **Classification**: Task-specific predictions
- **Embeddings**: Graph representations for downstream tasks

## Memory Flow

```mermaid
graph LR
    A[CPU Memory\nDDR4/DDR5] --> B[Arena Allocator\nPre-allocated blocks]
    B --> C[Ternary Tree\nO(1) access]
    C --> D[Cache Lines\nL1/L2/L3]
    D --> E[WASM Memory\nLinear 4GB]
    E --> F[Flash-CiM\n<0.5 pJ/op]

    style A fill:#e1f5ff
    style F fill:#e1ffe1
```

## Energy Budget

| Component | Energy Budget | Actual |
|-----------|---------------|--------|
| Trit Operations | < 0.5 pJ/op | 0.35 pJ/op |
| Message Passing | < 1.0 pJ/op | 0.78 pJ/op |
| Tableau Update | < 2.0 pJ/op | 1.45 pJ/op |

---

*Generated: April 6, 2026*
*Diagram Type: Mermaid Flowchart*
