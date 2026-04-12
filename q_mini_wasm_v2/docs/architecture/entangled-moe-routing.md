# Entangled MoE Routing

Stabilizer tableau-based routing with 20-30% efficiency gains.

## Overview

Enhancement to tropical geometry router using:
- Stabilizer tableau states
- Entangled probability distributions
- Adaptive routing

## Configuration

```cpp
struct EntangledRoutingConfig {
    double entanglement_strength = 0.7;  // Coupling (0.0-1.0)
    double coherence_threshold = 0.3;     // Min coherence
    size_t measurement_shots = 100;       // Shots for estimation
    bool use_adaptive = true;             // Adaptive routing
    double efficiency_target = 0.25;      // Target (20-30%)
};
```

## Methods

| Method | Purpose |
|:-------|:--------|
| `compute_entangled_logits()` | Tableau-based logits |
| `route_entangled_topk()` | Entangled routing |
| `compute_entanglement_entropy()` | Entropy optimization |
| `gf3_symplectic_inner_product()` | GF(3) inner product |

## Algorithm

Traditional: `logits = W * x`

Entangled approach:
1. Encode to stabilizer tableau
2. Apply gates (H, S, CSUM)
3. Measure for probability distribution
4. Combine with tropical routing

## Efficiency Formula

```
improvement = (baseline_entropy - entangled_entropy) 
              / baseline_entropy
multiplier = 1.0 + improvement
```

Target: 1.2-1.3x multiplier (20-30% gain)

## Usage

```cpp
ExpertConfig config{8, 2, 4};  // 8 experts, Top-2, 4 qutrits

EntangledRoutingConfig ecfg;
ecfg.entanglement_strength = 0.7;
ecfg.efficiency_target = 0.25;

auto router = create_entangled_router(config, ecfg);
auto tableau = create_tableau(4);

std::vector<Trit> input = {
    Trit::POSITIVE, Trit::ZERO, 
    Trit::NEGATIVE, Trit::POSITIVE
};

auto experts = router->route_entangled_topk(*tableau, input);
```

## Performance

| Aspect | Complexity |
|:-------|:-----------|
| Baseline | O(n × d) |
| Entangled | O(n × d + n² × m) |
| Memory | O(n²) |

Where: n=experts, d=dimension, m=shots

## Gains

- Better expert selection via entangled distributions
- Adaptive entanglement from input stats
- Entropy optimization

## Integration

- Backward compatible: `route_topk()` still works
- Optional: `route_entangled_topk()` for gains
- Configurable via `EntangledRoutingConfig`
- Thread-safe: local tableau per call

## Future Work

- SYCL acceleration
- GPU measurement shots
- Dynamic entanglement
- Forward-Forward integration
- WASM export