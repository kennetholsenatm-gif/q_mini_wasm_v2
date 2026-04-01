# Bayesian Cascade Training for 1.58-bit Quantization

**Research Reference:** "Stochastic Topologies in Extreme-Edge AI: Bayesian Distillation and Variance-Bounded Markovian Routing on the Hypersimplex for the QMINIWASM 1.58-bit Architecture" (see `docs/research/QMINIWASM_ Bayesian Markovian Edge AI.md`)

## Overview

The QMINIWASM 1.58-bit quantization architecture requires a specialized training pipeline to overcome the capacity collapse and gradient death that typically occur when forcing continuous manifolds into discrete ternary lattices. The Bayesian Cascade Training approach frames the entire process as a non-stationary Markov Decision Process (MDP) with four distinct phases.

## Phase Architecture

### Phase 1: Continuous Supervised Fine-Tuning (SFT)

**Status: Implemented** — The `train_step_supervised()` in `LibTorchTpemTrainer` provides Phase 1 training.

- **Goal:** Establish cognitive priors and foundational decision manifolds
- **Implementation:** Standard MSE training with Adam optimizer on FP32 `CoreModule`
- **Output:** Teacher checkpoint for Phase 2/3 distillation
- **Config:** `[training].epochs`, `[training].batch_size`, `[training].learning_rate`

### Phase 2: Progressive Quantization with Simulated Annealing

**Status: Implementing** — SA methods added to trainer; cascade loop integration pending.

**Theoretical Basis:** Models quantization as thermodynamic cooling rather than immediate discretization.

- **Mechanism:** Metropolis-Hastings Markov chain with exponential temperature decay
- **Acceptance Criterion:** `P(accept) = min(1, exp(-ΔE / T))` where `ΔE` = energy difference, `T` = temperature
- **Temperature Schedule:** `T(t) = T_0 × γ^t` (exponential cooling)
- **Implementation:** `LibTorchTpemTrainer::apply_simulated_annealing()`

**Mathematical Details:**

1. **Energy Function:** `E(θ) = Σ |ternary(w_i) × α - w_i|²`
   - α = mean(|w|) scaling factor for each weight tensor
   - Ternary projection: {−1, 0, +1} via sign-based rounding

2. **Metropolis-Hastings Step:**
   - Propose: `w' = w + N(0, T × α)` (Gaussian perturbation scaled by temperature)
   - Compute: `ΔE = E(w') - E(w)`
   - Accept: `min(1, exp(-ΔE / T))`

3. **Phase Transition Behavior:**
   - High T (T ≫ 0): Broad exploration of surrounding topological space, accepts sub-optimal configurations
   - Low T (T → 0): Progressive "freezing" to nearest ternary lattice vertices

**Configuration (TOML):**
```toml
[cascade_curriculum_loop]
sa_initial_temperature = 1.0     # T_0: initial temperature
sa_cooling_rate = 0.95           # γ: per-epoch cooling multiplier
sa_min_temperature = 0.01        # T_min: freeze threshold
sa_acceptance_window = 0.5       # target acceptance rate (0 = disable adaptive)
```

### Phase 3: Multi-Domain On-Policy Distillation (MOPD)

**Status: Partial** — Basic `train_step_distill()` exists with MSE student-teacher matching. Reverse-KL not yet implemented.

**Theoretical Basis:** Bayesian probability matching using continuous-to-discrete distillation.

- **Teacher:** FP32 SFT checkpoint (Phase 1 output)
- **Student:** Ternary-quantized `CoreModule` after Phase 2
- **Current Loss:** `L = MSE(student, teacher) + λ × MSE(student, target)`

**Missing (Not Yet Implemented):**

1. **Reverse-KL Divergence:**
   - **Forward-KL:** `D_KL(p_teacher || q_student)` — mean-seeking (penalizes missing teacher modes)
   - **Reverse-KL:** `D_KL(q_student || p_teacher)` — mode-seeking (aligns discrete student to highest-density teacher modes)
   - **Why Reverse-KL:** Discrete ternary lattice cannot smoothly interpolate; attempting to cover wide continuous distributions causes gradient death

2. **Token-Level Distillation Advantage:**
   - `A_distill(t) = log(π_teacher(a_t|s_t)) - log(π_student(a_t|s_t))`
   - Dense reward signal for token-by-token alignment

**Future Formula (when implemented):**
```
L_MOPD = L_MSE(student, teacher)
       + λ_KL · D_KL(q_student || p_teacher)
       + λ_target · L_target(student, target)
```

### Phase 4: Geometric Routing with CISPO

**Status: Implemented** — `train_step_cascade_cispo()` in trainer.

**Theoretical Basis:** Variance-bounded Markovian routing on Hypersimplex Normal Fan.

- **Purpose:** Prevent token dropout during RL policy optimization
- **Key Innovation:** Detach clipped importance weights from gradient computation

**CISPO Objective:**
```
L^CISPO(θ) = -E_t [ E_{a~π_θ} [ w_t^detach · A_t · log π_θ(a_t|s_t) ] ]
w_t^detach = clamp(π_θ(a|s)/π_old(a|s), 1-ε, 1+ε).detach()
```

- `w_t^detach`: Clipped importance weight, **detached from gradients** — acts as constant scalar
- `A_t`: Standardized advantage (group-relative returns)
- Gradients flow exclusively through `log π_θ(a_t|s_t)`

**Why This Works:**
- Standard PPO clips `min(ratio · A, clipped_ratio · A)` — large ratios trigger clipping, gradients go to zero
- CISPO detaches the clipping coefficient — every token gets unbiased gradient
- Critical "inflection point" tokens ("Wait," "However") can be learned regardless of initial probability

**Implementation:** See `cascade_cispo_loss_tensor()` in `cpp/training/src/libtorch_ternary_trainer.cpp`

**Configuration (TOML):**
```toml
[cascade]
policy_optimizer = "cispo"
cispo_clip_epsilon = 0.2
group_size = 4
```

## Simulated Annealing Integration

### Current Implementation Location

The SA methods are implemented in:
- `cpp/training/include/qminiwasm/training/libtorch_ternary_trainer.hpp` — method declaration
- `cpp/training/src/libtorch_ternary_trainer.cpp` — `Impl::apply_simulated_annealing()`

### Cascade Curriculum Loop Integration

The SA step should be inserted into `run_cascade_curriculum()` in `training_engine.cpp`. Current flow:

1. **Teacher Phase** (existing): Train FP32 → save checkpoint
2. **PTQTP Phase** (existing): Apply ternary reconstruction
3. **Heal Phase** (existing): Distill from teacher

**Proposed SA replacement of PTQTP:**

After teacher phase, instead of immediate PTQTP:

```cpp
// Phase 2: Simulated Annealing (replaces or augments PTQTP)
double T = config_.cascade_loop.sa_initial_temperature;
const double gamma = config_.cascade_loop.sa_cooling_rate;
const double T_min = config_.cascade_loop.sa_min_temperature;
double acc_rate = 0.0;
for (std::int64_t sa_ep = 0; T > T_min; ++sa_ep) {
  libtorch_trainer_->apply_simulated_annealing(
      T, gamma, T_min,
      config_.cascade_loop.sa_acceptance_window,
      config_.seed ^ static_cast<std::uint64_t>(sa_ep + 100),
      &acc_rate, &err);
  // Telemetry: temperature, acceptance rate, val loss
  T = T * gamma;  // Cool down
  emit_phase("cascade_phase_sa", ("anneal_" + std::to_string(sa_ep)).c_str());
}
// Then proceed to heal phase
```

## Metropolis-Hastings Mathematical Derivation

### Setup

Let the quantization task be: minimize quantization error `E(θ) = ||θ_q(θ) - θ||²` where `θ_q(θ)` is the ternary projection of continuous weights.

### Metropolis-Hastings Algorithm

1. **Current State:** Configuration `θ` with energy `E(θ)`
2. **Proposal:** Perturb individual weight `w_i`:
   - `w_i' = w_i + N(0, T × α)` where `α = mean(|w|)` scales noise to weight magnitude
3. **Acceptance:**
   ```
   r = exp(-(E(θ') - E(θ)) / T)
   u ~ Uniform(0, 1)
   if u < r: accept w_i'
   else: keep w_i
   ```
4. **Cooling:** `T ← T × γ` after each epoch over all weights

### Convergence Properties

- **Detailed Balance:** `π(θ)P(θ→θ') = π(θ')P(θ'→θ)` ensures stationary distribution exists
- **Ergodicity:** High temperature enables exploration of all weight configurations
- **Convergence to Global Optimum:** When cooling is slow enough (logarithmic schedule), converges to global optimum with probability approaching 1

### Practical Considerations for 1.58-bit Quantization

1. **Weight Tensor Scoring:** `α = mean(|w|)` per tensor ensures consistent scaling across layers
2. **Ternary Projection:** `sign(w) × clamp(round(|w|/α), 0, 1)` with scale factor α
3. **Memory Efficiency:** Process weights element-by-element to avoid O(d_model²) temporary allocations

## Bayesian Interpretation

### Teacher as Likelihood

The continuous-phase teacher distribution `π_teacher(a|s)` provides a likelihood function:
- `P(D|θ) = π_teacher(a|s; θ)` — likelihood of observed token a given state s and model parameters

### Student as Prior

The quantized student distribution `π_student(a|s)` serves as the prior:
- `P(θ)` = probability mass concentrated on discrete ternary vertices `{-1, 0, +1}`

### Posterior Update

Distillation implements approximate Bayesian updating:
- `P(θ|D) ∝ P(D|θ) × P(θ)` — posterior proportional to likelihood × prior
- Reverse-KL ensures student concentrates mass on teacher's highest-confidence regions

## Next Steps / TODO

1. **Integrate SA into cascade curriculum loop** (`training_engine.cpp::run_cascade_curriculum`)
2. **Implement Reverse-KL MOPD** with actual token-level divergence computation
3. **Add SA telemetry** — temperature schedule, acceptance rate tracking in training events
4. **Create `docs/research/TROPICAL_MOE_ROUTING.md`** for Phase 4 geometric routing details
5. **Plan PennyLane qutrit Clifford integration** for quantum-classical hybrid training

## References

- Research paper: `docs/research/QMINIWASM_ Bayesian Markovian Edge AI.md`
- Existing cascade docs: `docs/CASCADE_AND_MOPD.md`
- Training engine implementation: `cpp/training/src/training_engine.cpp`
- LibTorch trainer: `cpp/training/src/libtorch_ternary_trainer.cpp`