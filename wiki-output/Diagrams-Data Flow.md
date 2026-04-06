# Data Flow Diagram

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
