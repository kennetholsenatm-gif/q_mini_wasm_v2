# q_mini_wasm_v2

> **Quantum-Inspired, Ternary AI Inference Engine for Extreme-Edge Computing**

---

## Quick Links

- **[Quick Start](Guides-Quick-Start)** - Get started in 5 minutes
- **[Architecture Overview](Architecture-Overview)** - System design and concepts  
- **[API Reference](API-Core-Reference)** - Complete API documentation
- **[Building](Guides-Building)** - Build from source

---

## What is q_mini_wasm_v2?

A quantum-inspired, highly energy-efficient AI inference engine operating entirely in a **ternary GF(3) state space**, exploiting the **Gottesman-Knill theorem** for efficient classical simulability on edge devices.

### Key Features

| Feature | Description |
|---------|-------------|
| **Ternary Computing** | 1.58-bit precision with 99.06% entropy efficiency |
| **GF(3) Arithmetic** | All operations over Galois Field of order 3 - no floating point |
| **Energy Efficiency** | <0.5 pJ/op for routing operations |
| **O(n²) Complexity** | Stabilizer tableau simulation via Gottesman-Knill |
| **SYCL Acceleration** | GPU/FPGA support via oneAPI |

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────┐
│  QGNN: Quantum Graph Neural Network                     │
│  ├─ GraphTableau: O(n²) stabilizer representation       │
│  ├─ BettiExtractor: Topological data analysis         │
│  └─ MoE Router: 243-expert tropical routing             │
├─────────────────────────────────────────────────────────┤
│  Forward-Forward Learning                               │
│  ├─ Data Synthesizer: Autonomous training pipeline      │
│  └─ Hebbian Updates: Local learning without backprop    │
├─────────────────────────────────────────────────────────┤
│  Runtime                                                │
│  ├─ SYCL Kernels: GPU/FPGA acceleration                 │
│  ├─ Memory Arena: Zero-copy USM                         │
│  └─ Flash-CIM: Compute-in-memory ternary storage        │
└─────────────────────────────────────────────────────────┘
```

---

## Documentation

### Getting Started
- [Quick Start](Guides-Quick-Start) - Installation and first steps
- [Building](Guides-Building) - Compile from source
- [SYCL Setup](Guides-SYCL-Setup) - GPU/FPGA configuration
- [Contributing](Guides-Contributing) - Development guidelines

### Architecture
- [Overview](Architecture-Overview) - System architecture
- [QGNN Architecture](Architecture-QGNN-Architecture) - Graph-native quantum neural networks
- [Stabilizer Tableau](Architecture-Stabilizer-Tableau) - O(n²) quantum simulation
- [MOE Routing](Architecture-MOE-Routing) - Mixture-of-Experts routing
- [SYCL Acceleration](Architecture-SYCL-Acceleration) - Hardware acceleration

### Research
- [Quantum Betti Numbers](Research-Quantum-betti-numbers-integration-analysis) - Topological data analysis
- [Forward-Forward Training](Research-Autonomous-forward-Forward-training-plan) - Autonomous learning
- [QGNN Integration](Research-Repository-analysis-for-qgnn-integration) - Graph quantum neural networks

### Reference
- [API Core Reference](API-Core-Reference) - C++ API documentation
- [Betti Extractor Trace](Betti-Extractor-Trace) - Code trace to SYCL XPU layer

---

## Implementation Status

| Component | Status | Location |
|-----------|--------|----------|
| WUI (Simplified) | Complete | `wui/` - 4 pages, inline CSS/JS |
| BettiExtractor | Complete | `core/qgnn/` - GF(3) TDA |
| Data Synthesizer | Complete | `core/training/` - C++17 |
| GraphTableau | Complete | `core/qgnn/` - O(n²) stabilizer |
| SYCL Kernels | Complete | `core/qgnn/` - XPU acceleration |

---

## Stats

- **38 Wiki Pages** - Architecture, API, guides, research
- **C++17** - Modern C++ with SYCL support
- **GF(3) Purity** - Zero floating-point in core
- **<0.5 pJ/op** - Edge energy target

---

*Last updated: April 2026*
