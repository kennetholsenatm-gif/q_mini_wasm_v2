# Tropical Geometry MoE Routing for Extreme-Edge AI

**Research Reference:** "Stochastic Topologies in Extreme-Edge AI: Bayesian Distillation and Variance-Bounded Markovian Routing on the Hypersimplex" (see `docs/research/QMINIWASM_ Bayesian Markovian Edge AI.md`)

## Overview

Mixture-of-Experts (MoE) routing in extreme-edge AI is fundamentally rooted in **tropical geometry**. The Top-K routing mechanism is algebraically isomorphic to the K-th elementary symmetric tropical polynomial, creating an **Affine Order-K Voronoi Diagram** that partitions input space into the **Normal Fan of a Hypersimplex**.

## The Tropical Semiring

In tropical geometry, standard arithmetic is redefined:

| Operation | Standard | Tropical |
|-----------|----------|----------|
| Addition | a + b | max(a, b) |
| Multiplication | a × b | a + b |

This is denoted as the tropical semiring `T_R = (R ∪ {-∞}, ⊕, ⊙)` where `a ⊕ b = max(a, b)` and `a ⊙ b = a + b`.

## Top-K Routing Isomorphism

Given `N` experts and routing logits `l_1, l_2, ..., l_N`, the router selects the top-K experts by finding the coalition of size K that maximizes:

```
score(S) = Σ_{i ∈ S} l_i
```

This is algebraically equivalent to the K-th elementary symmetric tropical polynomial:

```
σ_K^{trop}(l_1, ..., l_N) = max_{|S|=K} Σ_{i ∈ S} l_i
```

## Hypersimplex Normal Fan

The tropical polynomial `σ_K^{trop}` partitions input space into convex polyhedral cells — the **Normal Fan of the (N,K)-Hypersimplex**.

### Geometric Properties

| Property | Dense Network | MoE Router (Tropical) |
|----------|---------------|----------------------|
| Capacity Scaling | Polynomial (Depth × Width) | Binomial C(N,K) (Combinatorial) |
| Decision Boundaries | Hyperplane arrangements | Singular locus of tropical polynomials |
| Input Space Partition | Convex polyhedral cells | Normal Fan of a Hypersimplex |
| Quantization Response | Capacity collapse | Combinatorial Resilience |

### Combinatorial Resilience

The key insight: **sparsity is combinatorial depth**. Dense networks scale capacity polynomially, while MoE routers scale it combinatorially via `C(N,K)`.

When continuous weights are forced into the ternary lattice `{-1, 0, +1}`:
- Dense networks lose geometric capacity (decision boundaries flatten)
- MoE routers retain expressivity via transversality of routing cones

The combinatorial pathways through the Hypersimplex Normal Fan preserve input space partitioning even with extreme quantization.

## Mathematical Derivation

### Normal Fan Construction

Let `Δ(N,K)` be the (N,K)-Hypersimplex (the convex hull of all N-dimensional vectors with exactly K entries equal to 1, rest 0).

The **Normal Fan** of `Δ(N,K)` is the collection of all normal cones:

```
N_Δ(v) = { w ∈ R^N : w·x ≤ w·v for all x ∈ Δ(N,K) }
```

For each vertex v (a specific top-K coalition), the normal cone `N_Δ(v)` is the set of all input configurations where that coalition is optimal.

### Tropical Polynomial Singular Locus

The singular locus `Σ(σ_K^{trop})` is where two or more coalitions achieve the same maximum value:

```
Σ(σ_K^{trop}) = { l : ∃ S ≠ S' with |S|=|S'|=K, Σ_{i∈S} l_i = Σ_{j∈S'} l_j = σ_K^{trop}(l) }
```

These are the **decision boundaries** of the router — the non-differentiable creases where routing switches between expert coalitions.

### CISPO and Variance-Bounded Routing

Standard PPO/GRPO fails on tropical routing boundaries because clipping suppresses gradients at critical inflection points. CISPO solves this by:

1. Clipping importance weights: `w = clamp(ratio, 1-ε, 1+ε)`
2. Detaching clipped weights from gradient computation
3. Gradients flow through `log π_θ(a|s)` only

This ensures uniform exploration of the Hypersimplex Normal Fan, providing variance-bounded Markovian routing across the non-differentiable creases.

## Tier 4 Configuration (d_model = 8192)

For Tier 4 edge deployment targeting larger expert capacities:

```toml
[model]
d_model = 8192
num_ternary_blocks = 2
io_d_model = 8192
attention_backend = "tropical"

[cascade_curriculum_loop]
enabled = true
sa_initial_temperature = 1.0
sa_cooling_rate = 0.95
sa_min_temperature = 0.01
sa_acceptance_window = 0.5
```

The larger `d_model` and `num_ternary_blocks=2` increase the number of expert weight matrices, enabling more combinatorial routing pathways through the Hypersimplex.

## Implementation Status

### Completed
- Top-K routing selection (existing `CascadeToyPolicy`)
- CISPO variance-bounded optimization (existing `cascade_cispo_loss_tensor`)
- Simulated annealing quantization (new `apply_simulated_annealing`)

### Future
- Explicit tropical semiring operations for routing
- Affine Order-K Voronoi diagram computation for visualization
- Combinatorial resilience metrics
- PennyLane qutrit Clifford gate integration for quantum routing

## References

- Research paper: `docs/research/QMINIWASM_ Bayesian Markovian Edge AI.md`
- Implementation: `cpp/training/src/libtorch_ternary_trainer.cpp`
- Distillation docs: `docs/CASCADE_AND_MOPD.md`
- Bayesian cascade docs: `docs/research/BAYESIAN_CASCADE_TRAINING.md`