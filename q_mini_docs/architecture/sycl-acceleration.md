# SYCL Acceleration

## Overview

SYCL provides hardware-agnostic parallelism for
GPU and multi-core CPU acceleration.

## Parallel Operations

| Operation | Parallelism Strategy |
|---|---|
| Tableau updates | Row-level parallelism |
| Modulo-3 arithmetic | Sub-group vectorization |
| MoE routing | Expert-level parallelism |
| Forward-Forward | Layer-level parallelism |

## Kernel Structure

```cpp
queue.submit([&](handler& h) {
    h.parallel_for(range{n}, [=](id<1> i) {
        // Parallel tableau row update
    });
});
```

## Build Configuration

```bash
cmake -S q_mini_wasm_v2 -B q_mini_wasm_v2/build_sycl -DCMAKE_CXX_COMPILER=icpx ..
cmake --build .
```

Requires Intel oneAPI or DPC++ toolchain.

## See Also

- [Stabilizer Tableau](stabilizer-tableau.md) — operations being parallelized
- [Build Guide](../guides/sycl-setup.md) — SYCL environment setup