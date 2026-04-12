# Flash-CIM

Flash Compute-in-Memory with ternary data.

## Overview

Proof-of-concept for in-storage computation using flash devices.
Part of q_mini_wasm_v2 quantum-classical hybrid framework.

## Key Concepts

### Compute-in-Memory (CIM)

Avoids "memory wall" by computing in storage:

| Operation | Energy |
|:----------|:-------|
| Data transfer | ~10 pJ/cell |
| CIM compute | ~0.1 pJ/op |
| **Savings** | **~99%** |

### Ternary Storage

Uses q_mini_wasm_v2 1.58-bit states `{+1, 0, -1}`.
MLC flash stores 4 states per cell.

## Cell States

| State | Value | Electrons | Ternary |
|:------|:------|:----------|:--------|
| ERASED | 0 | None | N/A |
| LOW | 1 | Few | -1 |
| MEDIUM | 2 | Some | 0 |
| HIGH | 3 | Many | +1 |
| RESERVED | 4 | N/A | Error detect |

## Usage

```cpp
#include "flash_cim.hpp"
using namespace q_mini_wasm_v2::core::flash_cim;

// Create and init
auto config = create_default_d_drive_config();
auto ctrl = create_flash_cim_controller(config);
ctrl->initialize();

// Write data
std::vector<Trit> data = {Trit::POSITIVE, Trit::NEGATIVE};
ctrl->write_trits(data, 0);

// CIM operations
ctrl->cim_ternary_add(a, b, out);
ctrl->cim_vector_matrix_multiply(in, w, out);

// Energy tracking
double pj = ctrl->get_total_energy_pj();
```

## Building

```bash
mkdir build && cd build
cmake ..
cmake --build .
```

## Configuration

| Parameter | Default | Description |
|:----------|:--------|:------------|
| storage_path | D:\flash_cim | Storage path |
| block_size | 4096 | Block size (bytes) |
| page_size | 512 | Page size (bytes) |
| cells_per_page | 256 | Cells per page |
| enable_ecc | true | Error correction |
| wear_leveling | true | Wear leveling |
| max_cycles | 10000 | Max program cycles |

## Performance

| Operation | Energy | Latency |
|:----------|:-------|:--------|
| Read/cell | ~1 pJ | 1 cycle |
| Write/cell | ~10 pJ | 1 cycle |
| Erase/block | ~1000 pJ | 10 cycles |
| CIM/op | ~0.1 pJ | 1 cycle |

## Limitations

Proof of concept only:
- Simulated flash (not real hardware)
- Memory-only storage (not persisted)
- Simplified wear leveling

## Future Work

- D:\ drive persistence
- Hardware acceleration
- Advanced ECC
- SYCL integration

## References

1. "Compute-in-Memory for Energy-Efficient AI" - IEEE Micro 2023
2. "Ternary Neural Networks with Flash-CIM" - Internal Report

## License

Part of the q_mini_wasm_v2 research framework.