# SYCL Hardware Implementation

This document describes the real SYCL implementation that replaces the stubs in the Q-Mini-WASM project.

## Overview

The implementation provides native hardware acceleration for vector and matrix operations using Intel oneAPI SYCL. It includes:

1. **Real SYCL Backend**: Uses dpctl and dpctl.tensor for SYCL operations
2. **Unified Interface**: The original stubs file now automatically uses the real backend when available
3. **Fallback Support**: Maintains stub functionality when SYCL is not available

## Implementation Details

### Directory Structure
```
qminiwasm/hardware/sycl/
├── __init__.py          # Module initialization
├── sycl_hardware.py     # Main SYCL implementation
└── setup.py            # Build configuration
```

### Key Features
- **Vector Engine (XVE)**: Executes logic-heavy routing operations using SYCL
- **Matrix Engine (XMX)**: Performs matrix multiplications using SYCL tensor operations
- **Ternary Weight Packing**: Maintains the 5 trits/byte packing format
- **Memory Paging**: Uses SYCL's USM (Unified Shared Memory) for memory management

## Usage

### Installation
1. Install SYCL dependencies:
   ```bash
   pip install -r requirements/hardware.txt
   ```

2. Build the SYCL extension:
   ```bash
   cd qminiwasm/hardware/sycl
   python setup.py build
   python setup.py install
   ```

### Configuration
Set the `SYCL_BACKEND` environment variable to control behavior:
- `SYCL_BACKEND=sycl` (default): Use real SYCL backend
- `SYCL_BACKEND=stubs`: Force use of stub implementations

### Testing
The implementation automatically falls back to stubs if:
- SYCL backend is not available
- dpctl cannot be imported
- No compatible SYCL device is found

## API Compatibility

The new implementation maintains full API compatibility with the original stubs:
- `execute_vector_engine(kernel, data)`
- `execute_matrix_engine(matrix, weights)`
- `pack_ternary_weights(weights)`
- `unpack_ternary_weights(packed)`
- `driver_memory_paging(memory, size)`

## Error Handling

The implementation includes robust error handling:
- Graceful fallback to stubs when SYCL is unavailable
- Device detection and logging
- USM allocation error handling
- Comprehensive logging for debugging

## Performance Considerations

- Vector operations use SYCL's parallel execution capabilities
- Matrix operations leverage SYCL tensor operations
- Memory operations use USM for efficient host-device data sharing