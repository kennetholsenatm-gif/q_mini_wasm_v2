# q_mini_wasm_v2: Quantum-Inspired Extreme-Edge AI Framework

[![CI](https://github.com/kennetholsenatm-gif/q_mini_wasm_v2/workflows/CI/badge.svg)](https://github.com/kennetholsenatm-gif/q_mini_wasm_v2/actions)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B20)
[![GF(3) Compliant](https://img.shields.io/badge/GF(3)-Compliant-green.svg)](docs/README.md)

A quantum-inspired, highly energy-efficient AI inference engine operating entirely in a ternary GF(3) state space, exploiting the Gottesman-Knill theorem for efficient classical simulability.

## Quick Start

Download and run the pre-built executable:

```bash
git clone https://github.com/kennetholsenatm-gif/q_mini_wasm_v2.git
cd q_mini_wasm_v2
./qminiwasm.exe
```

Then open http://localhost:7345 in your browser.

That's it. No build step required for Windows users.

## What This Is

q_mini_wasm_v2 is a quantum-inspired AI framework that uses ternary (3-state) computing instead of binary. It implements:

- **Ternary GF(3) Arithmetic**: All operations use -1, 0, +1 states (trits) instead of 0, 1 bits
- **Mixture-of-Experts Routing**: Sparse expert selection using tropical geometry
- **Forward-Forward Learning**: Local, gradient-free learning without backpropagation
- **Quantum Stabilizer Formalism**: Efficient classical simulation of quantum operations

The system achieves <0.5 pJ/op energy efficiency for routing and 57% energy reduction over traditional approaches.

## Features

- **Ternary Computing**: GF(3) arithmetic with 99.06% entropy efficiency
- **Mixture-of-Experts**: Up to 8192 experts (243 production-tested), top-K routing
- **Quantum-Inspired**: Gottesman-Knill theorem for efficient simulation
- **Graph-Native MoE**: O(E) complexity expert routing via QGNN
- **Forward-Forward Learning**: Local gradient-free training with tropical goodness
- **Autonomous Training**: Continuous mode with 15 knowledge engine APIs
- **Betti-Guided Topology**: Dynamic optimization via algebraic topology
- **Web-Based UI**: Built-in WUI at localhost:7345 for training and inference

## Performance

| System | Latency (us) | Energy (pJ/op) | Speedup | Memory Reduction |
|---------|-------------|---------------|---------|------------------|
| Array-Based (32 experts) | 45 | 1.2 | 1.0x | Baseline |
| Graph-Native (32 experts) | 28 | 0.7 | 1.6x | 90% |
| Graph-Native (243 experts) | 65 | 0.9 | 2.8x | 95% |
| Graph-Native (8192 experts target) | <100 | <1.0 | 10x+ | 98% |

## Training Modes

### Epoch-Based (Default)
```bash
./q_mini_wasm_v2_trainer.exe --epochs 100 --moe-experts 243
```

### Continuous/Autonomous (Production)
```bash
./q_mini_wasm_v2_trainer.exe --continuous --moe-experts 243 --enable-web-apis
```

- Runs indefinitely with background training thread
- Pause/resume capability
- Live Betti-guided topology optimization
- Automatic data acquisition from 15 knowledge APIs

## Documentation

See the [docs/](docs/) directory for detailed documentation:

- [API Reference](docs/api/core-reference.md) - Complete API documentation
- [Architecture Overview](docs/architecture/overview.md) - System design
- [QGNN Architecture](docs/architecture/qgnn-architecture.md) - Graph-native quantum neural networks
- [Expert Configuration](docs/guides/expert-configuration.md) - 16/64/243/8192 expert setup
- [Autonomous Training](docs/guides/autonomous-pipeline.md) - Continuous training mode
- [Data Synthesizer](docs/guides/data-synthesizer.md) - 15 knowledge engine APIs
- [Quick Start Guide](docs/guides/quick-start.md) - Detailed setup instructions

## Building from Source

See [docs/guides/building.md](docs/guides/building.md) for build instructions. Requires C++20 compiler and CMake 3.20+.

## License

MIT License - see [LICENSE](LICENSE) file.

## Contact

- **Maintainer**: [Kenneth Olsen](https://github.com/kennetholsenatm-gif)
- **Issues**: [GitHub Issues](https://github.com/kennetholsenatm-gif/q_mini_wasm_v2/issues)
