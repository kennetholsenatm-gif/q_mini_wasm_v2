# Mixture of Experts Routing

## Tropical Geometry

Uses **max-plus semiring** algebra:

| Operation | Tropical Form | Standard Equivalent |
|---|---|---|
| Addition | `max(a, b)` | `a + b` |
| Multiplication | `a + b` | `a × b` |

## Top-K Routing

Selects K active experts from N total based on input affinity.

**Hypersimplex capacity**: C(N,K) distinct routing regions.

| Config | Experts | Active | Capacity |
|---|---|---|---|
| Small | 4 | 2 | 6 regions |
| Medium | 8 | 2 | 28 regions |
| Large | 16 | 4 | 1820 regions |

## Load Balancing

Auxiliary loss ensures uniform expert utilization:

```
L_balance = N × Σ(f_i × P_i)
```

Where `f_i` = fraction of tokens routed to expert i,
`P_i` = average routing probability for expert i.

## Sparsity = Expressivity

Sparse routing achieves exponential combinatorial depth
without proportional compute cost.
</content>
<write_to_file>
<path>docs/architecture/forward-forward.md</path>
<content>
# Forward-Forward Learning

## No Backpropagation

Each layer learns **independently** using local goodness metrics.
No gradient chain required.

## Goodness Metric

**Tropical inner product**: `goodness = max(x_i × w_i)`

| Sample Type | Goal |
|---|---|
| Positive | Maximize goodness |
| Negative | Minimize goodness |

## Weight Updates

**Hebbian rule** (gradient-free):

```
w_new = w_old + lr × (goodness_pos - goodness_neg) × input
```

## Parallelism

Layers train independently — fully parallelizable
across SYCL work groups.

## Advantages

- No vanishing gradients
- Local learning (no global coordination)
- Hardware-friendly (integer operations)
- Natural fit for ternary state space
</content>
<write_to_file>
<path>docs/architecture/sycl-acceleration.md</path>
<content>
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
cmake -DUSE_SYCL=ON ..
cmake --build .
```

Requires Intel oneAPI or DPC++ toolchain.

## See Also

- [Stabilizer Tableau](stabilizer-tableau.md) — operations being parallelized
- [Build Guide](../guides/sycl-setup.md) — SYCL environment setup