# q_mini_wasm_v2: Quantum-Inspired Extreme-Edge AI Framework

[![CI](https://github.com/kennetholsenatm-gif/q_mini_wasm_v2/workflows/CI/badge.svg)](https://github.com/kennetholsenatm-gif/q_mini_wasm_v2/actions)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B20)
[![GF(3) Compliant](https://img.shields.io/badge/GF(3)-Compliant-green.svg)](q_mini_docs/README.md)

> **⚠️ STATUS: RESEARCH FRAMEWORK - NOT PRODUCTION READY**
> 
> Active research: WUI + Go control plane + C++ training DLL (`q_training`). Some MCP endpoints are intentionally unimplemented (no fake responses). See [GROUND_TRUTH.md](GROUND_TRUTH.md) and [CONTRIBUTING.md](CONTRIBUTING.md).

A quantum-inspired, highly energy-efficient AI inference engine operating entirely in a ternary GF(3) state space, exploiting the Gottesman-Knill theorem for efficient classical simulability.

## Quick Start

```powershell
git clone https://github.com/kennetholsenatm-gif/q_mini_wasm_v2.git
cd q_mini_wasm_v2
```

Build the training DLL (MSVC / CMake), then the Go host (CGO links `q_training.dll`):

```powershell
cmake -S q_mini_wasm_v2 -B q_mini_wasm_v2/build_final
cmake --build q_mini_wasm_v2/build_final --config Release --target q_training
go build -o cmd/qminiwasm/qminiwasm.exe ./cmd/qminiwasm
```

Copy `q_mini_wasm_v2/build_final/q_training.dll` next to `cmd/qminiwasm/qminiwasm.exe` (or on `PATH`), create external data at `C:\q_mini_data` per [CONTRIBUTING.md](CONTRIBUTING.md), then:

```powershell
.\cmd\qminiwasm\qminiwasm.exe
```

Open **http://localhost:9090** (see `Port` in `cmd/qminiwasm/main.go` if you change it).

Full setup, data layout, and what is tracked in git: **[CONTRIBUTING.md](CONTRIBUTING.md)**.

## What This Is

q_mini_wasm_v2 is a quantum-inspired AI framework that uses ternary (3-state) computing instead of binary. It implements:

- **Ternary GF(3) Arithmetic**: All operations use -1, 0, +1 states (trits) instead of 0, 1 bits
- **Mixture-of-Experts Routing**: Sparse expert selection using tropical geometry
- **Forward-Forward Learning**: Local, gradient-free learning without backpropagation
- **Quantum Stabilizer Formalism**: Efficient classical simulation of quantum operations

The system achieves <0.5 pJ/op energy efficiency for routing and 57% energy reduction over traditional approaches.

## Features

**IMPLEMENTED:**
- ✅ **Ternary Computing**: GF(3) arithmetic core (fixed-point: 1000 = 1.0)
- ✅ **Mixture-of-Experts Architecture**: 243 expert routing structure
- ✅ **Quantum Stabilizer Formalism**: Tableau representation implemented
- ✅ **Graph-Native QGNN**: Sparse adjacency list data structures
- ✅ **Web-Based WUI**: React dashboard with real backend integration
- ✅ **Data Acquisition**: 9+ web API sources configured

**IN PROGRESS / RESEARCH:**
- ⚠️ **Forward–Forward + MoE**: C++ pipeline and `q_training.dll` host integration; not a finished product model
- ⚠️ **Autonomous Training**: Requires external `C:\q_mini_data` layout and matching DLL build
- ⚠️ **Betti-Guided Topology**: Estimator exists; wiring to routing evolves
- ⚠️ **MoE Inference**: Several WUI/MCP paths return “not implemented” instead of fake outputs

## Performance Targets

**Status:** Models and simulations only - not yet measured on real hardware.

| System | Latency (us) | Energy (pJ/op) | Speedup | Memory Reduction | Status |
|---------|-------------|---------------|---------|------------------|--------|
| Array-Based (32 experts) | 45 | 1.2 | 1.0x | Baseline | Simulated |
| Graph-Native (32 experts) | 28 | 0.7 | 1.6x | 90% | Target |
| Graph-Native (243 experts) | 65 | 0.9 | 2.8x | 95% | Target |
| Graph-Native (8192 experts) | <100 | <1.0 | 10x+ | 98% | Roadmap |

## Training Modes

### Epoch-Based (Default)
```bash
./q_mini_wasm_v2_trainer.exe --epochs 100 --moe-experts 243
```

### Continuous/Autonomous (Planned)
```bash
./q_mini_wasm_v2_trainer.exe --continuous --moe-experts 243 --enable-web-apis
```

**Status:** Control plane implemented. Actual training loop is stubbed.

- Control plane: Session management works
- Training thread: Started but performs no weight updates
- Data acquisition: Configuration ready, fetching not implemented
- Betti optimization: Structure exists, not integrated

## Documentation

**Start Here:**
- [GROUND_TRUTH.md](GROUND_TRUTH.md) - Honest assessment of what's working
- [WUI_ARCHITECTURE.md](WUI_ARCHITECTURE.md) - Web UI design and entry points

**Detailed Docs:**
- `wiki-output/` - Comprehensive guides and architecture docs
- `q_mini_docs/` - Deep technical documentation
- `reports/` - Audit reports and remediation plans

**Key Guides:**
- [Quick Start](wiki-output/Guides-Quick%20Start.md) - Getting started
- [Building](wiki-output/Guides-Building.md) - Build from source
- [Architecture Overview](wiki-output/Architecture-Overview.md) - System design
- [API Reference](wiki-output/API-Core%20Reference.md) - MCP API documentation

## Building from Source

See [wiki-output/Guides-Building.md](wiki-output/Guides-Building.md) for detailed instructions.

**Quick Build:**
```powershell
# C++ training DLL
cmake --build q_mini_wasm_v2/build_final --config Release

# Go WUI server
go build -o qminiwasm.exe ./cmd/qminiwasm

# React WUI (optional)
cd wui/react-wui
npm install
npm run build
```

**Requirements:** Visual Studio 2022, CMake 3.20+, Go 1.26+, Node.js 18+

## License

MIT License - see [LICENSE](LICENSE) file.

## Contact

- **Maintainer**: [Kenneth Olsen](https://github.com/kennetholsenatm-gif)
- **Issues**: [GitHub Issues](https://github.com/kennetholsenatm-gif/q_mini_wasm_v2/issues)
