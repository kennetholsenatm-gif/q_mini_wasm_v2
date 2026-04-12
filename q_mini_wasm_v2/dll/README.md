# q_mini_wasm_v2 DLL API

C-compatible API for quantum-classical hybrid framework.

## Overview

This DLL provides C bindings for:

- Ternary logic operations (GF(3))
- Stabilizer tableau manipulation
- MoE router control
- Forward-Forward learning

All functions are thread-safe with mutex-protected handles.

## Building

```bash
mkdir build && cd build
cmake -DBUILD_DLL=ON ..
cmake --build .
```

Output:

| Platform | Files |
|:---------|:------|
| Windows | q_mini_wasm_v2.dll, .lib |
| Linux | libq_mini_wasm_v2.so |
| macOS | libq_mini_wasm_v2.dylib |


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

### Core Functions

| Function | Purpose |
|:---------|:--------|
| `q_mini_wasm_v2_version()` | Get version string |
| `q_mini_wasm_v2_error_string(code)` | Get error message |
| `q_mini_wasm_v2_is_valid_handle(h)` | Validate handle |

### Trit Operations

| Function | Purpose |
|:---------|:--------|
| `trit_add(a, b)` | Add over GF(3) |
| `trit_multiply(a, b)` | Multiply over GF(3) |
| `trit_pack_5(trits)` | Pack 5 trits to byte |
| `trit_unpack_5(byte, trits)` | Unpack byte to trits |

### Tableau Operations

| Function | Purpose |
|:---------|:--------|
| `tableau_create(n)` | Create tableau |
| `tableau_destroy(h)` | Destroy tableau |
| `tableau_apply_hadamard(h, i)` | Apply H gate |
| `tableau_apply_phase(h, i)` | Apply S gate |
| `tableau_apply_csum(h, c, t)` | Apply CSUM gate |
| `tableau_measure_all(h, out, n)` | Measure all |

### MoE Router

| Function | Purpose |
|:---------|:--------|
| `moe_router_create(total, active, dim)` | Create router |
| `moe_router_destroy(h)` | Destroy router |
| `moe_router_route_topk(...)` | Route to experts |

### Learner

| Function | Purpose |
|:---------|:--------|
| `ff_learner_create(layers, neurons, lr)` | Create learner |
| `ff_learner_destroy(h)` | Destroy learner |
| `ff_learner_forward(...)` | Forward pass |

### Orchestrator

| Function | Purpose |
|:---------|:--------|
| `orchestrator_create(threads)` | Create orchestrator |
| `orchestrator_destroy(h)` | Destroy orchestrator |
| `orchestrator_wait_all(h)` | Wait for tasks |

## Thread Safety

All functions are thread-safe. Handle management uses mutex protection.

## Error Handling

Best practices:

1. Check return values from all functions
2. Validate handles with `q_mini_wasm_v2_is_valid_handle()`
3. Create functions may return nullptr on failure
4. Use `q_mini_wasm_v2_error_string()` for messages

## Additional Features

| Feature | Functions |
|:--------|:----------|
| Logging | `enable_logging(level)`, `disable_logging()` |
| Handles | `get_handle_count()`, `enumerate_handles()` |
| Batch Ops | `batch_apply_hadamard()`, `batch_route_topk()` |

## Future Work

- Async callback support
- Memory pool allocation

