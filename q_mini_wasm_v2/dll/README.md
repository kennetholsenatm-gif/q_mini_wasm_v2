# q_mini_wasm_v2 DLL API

## Overview

This directory contains the C-compatible API for the q_mini_wasm_v2 quantum-classical hybrid framework.

## Key Improvements

### 1. Enhanced Error Handling
- **Error Code Enum**: Added q_mini_wasm_v2_error_t enum with specific error codes
- **Error Message Function**: q_mini_wasm_v2_error_string(int error_code) returns human-readable error messages

### 2. Handle Validation
- **Handle Validation Function**: q_mini_wasm_v2_is_valid_handle(void* handle) validates handle pointers
- **Improved Handle Management**: Better thread-safe handle management with mutex protection

### 3. Parameter Validation
- All create functions now validate input parameters:
  - Zero values for required parameters return nullptr
  - Invalid parameter combinations are rejected (e.g., active_experts > total_experts)
  - Non-positive learning rates are rejected

### 4. Null Parameter Handling
- All functions gracefully handle null pointers:
  - Destroy functions accept null handles safely
  - Query functions return appropriate default values for null handles
  - No crashes on null pointer access

## Building the DLL

### CMake Configuration

The DLL is built automatically when BUILD_DLL=ON (default):

```bash
mkdir build && cd build
cmake -DBUILD_DLL=ON ..
cmake --build .
```

### Output Files

- **Windows**: q_mini_wasm_v2.dll and q_mini_wasm_v2.lib
- **Linux**: libq_mini_wasm_v2.so
- **macOS**: libq_mini_wasm_v2.dylib

## Usage Examples

### C/C++ Example

```c
#include <q_mini_wasm_v2/dll/q_mini_wasm_v2_api.h>
#include <stdio.h>

int main() {
    // Get version information
    printf("Version: %s\n", q_mini_wasm_v2_version());
    
    // Create a tableau
    void* tableau = tableau_create(4);
    if (tableau == NULL) {
        printf("Failed to create tableau\n");
        return 1;
    }
    
    // Apply gates
    int result = tableau_apply_hadamard(tableau, 0);
    if (result != Q_MINI_WASM_V2_OK) {
        printf("Error: %s\n", q_mini_wasm_v2_error_string(result));
    }
    
    // Validate handle
    if (q_mini_wasm_v2_is_valid_handle(tableau)) {
        printf("Tableau is valid\n");
    }
    
    // Clean up
    tableau_destroy(tableau);
    return 0;
}
```

### Go Example

```go
package main

// #cgo CXXFLAGS: -std=c++17 -I/../
// #cgo LDFLAGS: -L/../build -lq_mini_wasm_v2 -lstdc++
// #include "../dll/q_mini_wasm_v2_api.hpp"
import "C"
import "fmt"

func main() {
    // Get version
    version := C.GoString(C.q_mini_wasm_v2_version())
    fmt.Printf("Version: %s\n", version)
    
    // Create tableau
    tableau := C.tableau_create(4)
    if tableau == nil {
        fmt.Println("Failed to create tableau")
        return
    }
    defer C.tableau_destroy(tableau)
    
    // Apply Hadamard gate
    result := C.tableau_apply_hadamard(tableau, 0)
    if result != C.Q_MINI_WASM_V2_OK {
        errorMsg := C.GoString(C.q_mini_wasm_v2_error_string(result))
        fmt.Printf("Error: %s\n", errorMsg)
    }
}
```

## Testing

### Unit Tests

```bash
# Build and run DLL API tests
cmake -DBUILD_TESTS=ON ..
cmake --build .
ctest -R dll_api_test
```

### Integration Tests

```bash
# Run integration tests
ctest -R dll_test
```

## API Reference

### Utility Functions

- q_mini_wasm_v2_version(): Get library version string
- q_mini_wasm_v2_build_info(): Get build information
- q_mini_wasm_v2_error_string(int error_code): Get error message
- q_mini_wasm_v2_is_valid_handle(void* handle): Validate handle

### Trit Operations

- trit_add(int8_t a, int8_t b): Add two trits over GF(3)
- trit_multiply(int8_t a, int8_t b): Multiply two trits over GF(3)
- trit_pack_5(const int8_t trits[5]): Pack 5 trits into byte
- trit_unpack_5(uint8_t byte, int8_t trits[5]): Unpack byte to 5 trits

### Stabilizer Tableau

- tableau_create(size_t num_qutrits): Create tableau
- tableau_destroy(void* handle): Destroy tableau
- tableau_apply_hadamard(void* handle, size_t qutrit): Apply Hadamard gate
- tableau_apply_phase(void* handle, size_t qutrit): Apply Phase gate
- tableau_apply_csum(void* handle, size_t control, size_t target): Apply CSUM gate
- tableau_measure_all(void* handle, int8_t* outcomes, size_t max_outcomes): Measure all qutrits
- tableau_is_valid(void* handle): Check validity
- tableau_num_qutrits(void* handle): Get qutrit count

### MoE Router

- moe_router_create(size_t total_experts, size_t active_experts, size_t routing_qutrits): Create router
- moe_router_destroy(void* handle): Destroy router
- moe_router_route_topk(...): Route to Top-K experts
- moe_router_capacity(void* handle): Get hypersimplex capacity

### Forward-Forward Learner

- ff_learner_create(size_t num_layers, size_t neurons_per_layer, double learning_rate): Create learner
- ff_learner_destroy(void* handle): Destroy learner
- ff_learner_forward(...): Forward pass
- ff_learner_goodness(...): Compute goodness metric

### Runtime Orchestrator

- orchestrator_create(size_t num_threads): Create orchestrator
- orchestrator_destroy(void* handle): Destroy orchestrator
- orchestrator_wait_all(void* handle): Wait for all tasks
- orchestrator_has_pending(void* handle): Check pending tasks

## Thread Safety

- All functions are thread-safe
- Handle management uses mutex protection
- Multiple threads can safely call API functions concurrently

## Error Handling Best Practices

1. **Always check return values**: All functions return error codes
2. **Validate handles**: Use q_mini_wasm_v2_is_valid_handle() for debugging
3. **Check for null**: All create functions can return nullptr on failure
4. **Use error strings**: Get human-readable messages with q_mini_wasm_v2_error_string()

## Future Enhancements

- [ ] Batch operations for improved performance
- [ ] Callback support for async operations
- [ ] Logging/debugging support
- [ ] Memory pool for handle allocation
- [ ] Handle enumeration for debugging

