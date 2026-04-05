# ADR-001: Ternary Over Binary

## Status

Accepted

## Context

Binary neural networks (1-bit) lose significant expressivity.
Floating-point (32-bit) is energy-prohibitive for edge deployment.

## Decision

Use **ternary state space** `{+1, 0, -1}` as the fundamental
computation unit.

## Rationale

| Metric | Binary | Ternary | FP32 |
|---|---|---|---|
| Values | 2 | 3 | 2^32 |
| Bits/trit | 1 | 1.58 | 32 |
| Energy/op | ~0.1 pJ | <1 pJ | ~3.7 pJ |
| Expressivity | Low | High | Very High |

**Ternary achieves the best energy-expressivity tradeoff**
for edge AI inference.

## Consequences

- All arithmetic must be GF(3) (mod 3)
- Hardware needs ternary ALU or software emulation
- 5-trit packing achieves 99.06% entropy efficiency