# Flash-CIM: Flash Compute-in-Memory

## Overview

Flash-CIM (Flash Compute-in-Memory) is a proof-of-concept implementation that demonstrates using flash storage devices (like the D:\ drive) for in-storage computation with ternary data. This module is part of the q_mini_wasm_v2 quantum-classical hybrid framework's exploration of energy-efficient computing paradigms.

## Key Concepts

### 1. Compute-in-Memory (CIM)
Traditional computing architectures suffer from the "memory wall" - the bottleneck of transferring data between memory and processing units. CIM performs computation directly in the storage medium, dramatically reducing energy consumption.

### 2. Ternary Data Storage
Flash-CIM uses the q_mini_wasm_v2 1.58-bit ternary state space `{+1, 0, -1}` for data storage. MLC (Multi-Level Cell) flash can store 4 states per cell, which maps naturally to ternary values with one state reserved for error detection.

### 3. Energy Efficiency
- **Traditional data transfer**: ~10 pJ per cell operation
- **CIM operations**: ~0.1 pJ per operation
- **Energy savings**: ~99% reduction for in-storage compute

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    Flash-CIM Controller                      │
├─────────────────────────────────────────────────────────────┤
│  Ternary Encoding/Decoding  │  Cell State Management        │
│  - Trit ↔ CellState mapping │  - Block status tracking      │
│  - 5-trit to 8-bit packing  │  - Wear leveling              │
│                             │  - Error correction (ECC)     │
├─────────────────────────────────────────────────────────────┤
│                  Compute-in-Memory Operations                │
│  - Vector-Matrix Multiplication                              │
│  - Ternary Addition                                          │
│  - Accumulation                                              │
├─────────────────────────────────────────────────────────────┤
│                    Flash Storage Layer                       │
│  - Block management (1024 blocks simulated)                  │
│  - Page-based access (512 bytes per page)                    │
│  - D:\ drive integration                                     │
└─────────────────────────────────────────────────────────────┘
```

## Flash Cell States

| State    | Value | Electron Level | Ternary Mapping |
|----------|-------|----------------|-----------------|
| ERASED   | 0     | None           | N/A             |
| LOW      | 1     | Few            | -1 (NEGATIVE)   |
| MEDIUM   | 2     | Some           | 0 (ZERO)        |
| HIGH     | 3     | Many           | +1 (POSITIVE)   |
| RESERVED | 4     | N/A            | Error detection |

## Usage

### Basic Usage

```cpp
#include "q_mini_wasm_v2/core/flash_cim/flash_cim.hpp"

using namespace q_mini_wasm_v2::core::flash_cim;

// Create controller with D:\ drive configuration
auto config = create_default_d_drive_config();
auto controller = create_flash_cim_controller(config);

// Initialize
controller->initialize();

// Write ternary data
std::vector<ternary::Trit> data = {
    ternary::Trit::POSITIVE,
    ternary::Trit::NEGATIVE,
    ternary::Trit::ZERO
};
controller->write_trits(data, 0);  // Write to block 0

// Read ternary data
auto read_data = controller->read_trits(0, 3);

// Shutdown
controller->shutdown();
```

### Compute-in-Memory Operations

```cpp
// In-storage ternary addition
controller->cim_ternary_add(block_a, block_b, output_block);

// In-storage vector-matrix multiply
controller->cim_vector_matrix_multiply(input_block, weight_block, output_block);

// In-storage accumulation
std::vector<uint32_t> blocks = {1, 2, 3, 4};
controller->cim_accumulate(blocks, output_block);
```

### Energy Tracking

```cpp
// Get total energy consumed
double energy_pj = controller->get_total_energy_pj();

// Get detailed metrics
auto metrics = controller->get_metrics();
for (const auto& [name, value] : metrics) {
    std::cout << name << ": " << value << std::endl;
}
```

## Building

The Flash-CIM module is included in the main q_mini_wasm_v2 build:

```bash
mkdir build && cd build
cmake ..
cmake --build .
```

To run the demo:

```bash
# Build the demo (add to tests/CMakeLists.txt)
./q_mini_wasm_v2_flash_cim_demo
```

## Configuration

### FlashCIMConfig

| Parameter           | Default      | Description                           |
|---------------------|--------------|---------------------------------------|
| storage_path        | D:\flash_cim | Path to storage device                |
| block_size          | 4096         | Block size in bytes                   |
| page_size           | 512          | Page size in bytes                    |
| cells_per_page      | 256          | Number of flash cells per page        |
| enable_ecc          | true         | Enable error correction               |
| enable_wear_leveling| true         | Enable wear leveling                  |
| max_program_cycles  | 10000        | Maximum program/erase cycles          |

## Performance Characteristics

| Operation              | Energy (pJ) | Latency (cycles) |
|------------------------|-------------|------------------|
| Read (per cell)        | ~1          | 1                |
| Write (per cell)       | ~10         | 1                |
| Erase (per block)      | ~1000       | 10               |
| CIM operation (per op) | ~0.1        | 1                |

## Limitations (Proof of Concept)

1. **Simulated Flash Behavior**: This is a software simulation of flash storage behavior
2. **No Actual Hardware CIM**: Real CIM requires specialized hardware
3. **Simplified Wear Leveling**: Production implementations need more sophisticated algorithms
4. **No Persistent Storage**: Data is stored in memory, not actually written to D:\

## Future Work

- [ ] Actual D:\ drive persistence layer
- [ ] Hardware-accelerated CIM operations
- [ ] Advanced ECC algorithms
- [ ] Multi-plane operations
- [ ] 3D NAND support
- [ ] Integration with SYCL for parallel CIM operations

## References

1. "Compute-in-Memory for Energy-Efficient AI" - IEEE Micro, 2023
2. "Ternary Neural Networks with Flash-CIM" - q_mini_wasm_v2 Research
3. "MLC Flash Storage for Ternary Data" - Internal Technical Report

## License

Part of the q_mini_wasm_v2 research framework.