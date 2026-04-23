# Component Interaction Diagram

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
