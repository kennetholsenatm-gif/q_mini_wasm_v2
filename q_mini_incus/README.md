# Q-Mini Incus Native

Ternary-native container runtime for GF(3) computation on Incus/LXC.

## Overview

Q-Mini Incus is a subproject that provides a ternary-native container runtime for the q_mini_wasm_v2 ecosystem. Unlike the WASM version which bridges to web runtimes, this version runs natively in Linux containers with GF(3) arithmetic throughout.

**Key Design Principles:**
- **Ternary-native computation**: All internal operations use GF(3) arithmetic
- **Namespace boundary conversion**: Binary conversion only at Linux namespace edges
- **Dual deployment modes**: Single-container (simple) or multi-container (scalable)

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│                    Host (Binary)                         │
│  ┌─────────────────────────────────────────────────┐   │
│  │           Incus Container                       │   │
│  │  ┌─────────────────────────────────────────┐   │   │
│  │  │    Ternary-Native Namespace            │   │   │
│  │  │  ┌─────────┐ ┌─────────┐ ┌─────────┐   │   │   │
│  │  │  │ qminid  │ │inference│ │ training│   │   │   │
│  │  │  │ (PID 1) │ │service  │ │service  │   │   │   │
│  │  │  └────┬────┘ └────┬────┘ └────┬────┘   │   │   │
│  │  │       └─────────────┴─────────┘        │   │   │
│  │  │              Ternary IPC                   │   │   │
│  │  └─────────────────────────────────────────┘   │   │
│  │           ↑ Namespace Boundary ↑               │   │
│  │     (Ternary↔Binary conversion)               │   │
│  │  ┌─────────────────────────────────────────┐   │   │
│  │  │    Network/Mount Namespace (Binary)      │   │   │
│  │  │         External I/O                     │   │   │
│  │  └─────────────────────────────────────────┘   │   │
│  └─────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────┘
```

## Project Structure

```
q_mini_incus/
├── CMakeLists.txt              # Build configuration
├── containers/                 # Container service implementations
│   ├── inference/             # Inference service
│   ├── training/              # Training service
│   ├── wui/                   # Web UI service
│   └── agent/                # Agent service
├── runtime/                   # Ternary-native runtime
│   ├── qminid/               # Init daemon (PID 1)
│   ├── namespace_bridge/     # Ternary↔Binary conversion
│   └── ipc/                  # Ternary-native IPC
├── deploy/                    # Deployment configurations
│   ├── single-container/       # Unified deployment
│   └── multi-container/       # Scalable deployment
└── core/                      # Symlink to ../q_mini_wasm_v2/core
```

## Quick Start

### Prerequisites

- Incus or LXD installed
- CMake 3.16+
- C++20 compiler (GCC 11+, Clang 14+)

### Building

```bash
# Build Incus native version (not WASM)
cd q_mini_wasm_v2
mkdir build_incus && cd build_incus
cmake ../q_mini_incus \
    -DBUILD_INCUS_INFERENCE=ON \
    -DBUILD_INCUS_TRAINING=ON \
    -DBUILD_INCUS_WUI=ON \
    -DBUILD_INCUS_AGENTS=ON
make -j$(nproc)
```

### Single-Container Deployment

```bash
# Create container
incus launch images:ubuntu/24.04 qmini-unified --profile q_mini_incus/deploy/single-container/incus-profile.yaml

# Copy binaries
incus file push build_incus/qmini-unified qmini-unified/root/

# Start qminid
incus exec qmini-unified -- /root/qmini-unified
```

### Multi-Container Deployment

```bash
# Launch individual containers
incus launch qmini-inference --profile q_mini_incus/deploy/multi-container/incus-profile.yaml
incus launch qmini-training --profile q_mini_incus/deploy/multi-container/incus-profile.yaml
incus launch qmini-wui --profile q_mini_incus/deploy/multi-container/incus-profile.yaml
```

## Design Decisions

### Why Subproject (Not Fork)?

- Shares core GF(3) logic with WASM version
- Single repository for both deployment targets
- Easier maintenance of ternary arithmetic code

### Why Linux Namespaces?

- Kernel-provided isolation
- Clean boundary for binary conversion
- Industry-standard containerization

### Why qminid Instead of Traditional Init?

- No UNIX signals (ternary message passing instead)
- No binary contamination from syslog/systemd
- Purpose-built for ternary-native services

## Differences from WASM Version

| Feature | WASM Version | Incus Native |
|---------|--------------|--------------|
| Runtime | WebAssembly | Native Linux |
| Bridge | WASM↔SYCL bridge | Namespace boundary |
| Init | N/A (browser/JS host) | qminid (ternary-native) |
| IPC | WASM linear memory | Ternary IPC sockets |
| Deployment | Browser/edge | Server/cloud |

## Contributing

See the main project [Contributing Guide](../docs/guides/contributing.md).

## License

Same as q_mini_wasm_v2 (see main project LICENSE).
