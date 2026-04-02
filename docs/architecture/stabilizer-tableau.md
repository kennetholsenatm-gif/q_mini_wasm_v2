# Stabilizer Tableau

## Overview

Tracks n qutrits using a 2n×2n matrix over GF(3).
Clifford gates update the tableau in **O(n²)** time.

## Clifford Gates

| Gate | Operation | Effect |
|---|---|---|
| **H** (Hadamard) | X ↔ Z swap | Superposition creation |
| **S** (Phase) | Z → ZX | Relative phase |
| **CSUM** (Controlled-SUM) | X_a → X_a, Z_b → Z_b + X_a | Entanglement |

## Gottesman-Knill Theorem

Stabilizer circuits (H, S, CSUM + measurements) can be efficiently
simulated classically — **no exponential overhead**.

This enables quantum-inspired parallelism on classical hardware.

## Tableau Structure

```
tableau[n][2n] : GF(3) matrix
  rows 0..n-1   : X generators
  rows n..2n-1  : Z generators
```

## See Also

- [Ternary State Space](ternary-state-space.md) — underlying GF(3) arithmetic
- [SYCL Acceleration](sycl-acceleration.md) — parallel tableau updates