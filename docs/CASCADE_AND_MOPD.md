# Cascade reinforcement learning and MOPD

This document describes **cascade GRPO** (group-relative policy optimization for the escalation / routing head) and **MOPD** (multi-domain on-policy distillation) as implemented in **qminiwasm-core**, how they fit into `run_training_loop`, and which environment variables control them. For data sources and the full env table, see **[TRAINING_DATA.md](TRAINING_DATA.md)**.

## Where the code lives

| Component | Module |
|-----------|--------|
| GRPO loss (advantage-weighted policy gradient, optional group normalization) | [`qminiwasm/rl/cascade_grpo.py`](../qminiwasm/rl/cascade_grpo.py) |
| MOPD composite loss (optional KL on aux logits + per-key feature matching) | [`qminiwasm/training/distillation.py`](../qminiwasm/training/distillation.py) |
| Rollout + `cascade_rl_train_step` | [`qminiwasm/training/cascade_rl.py`](../qminiwasm/training/cascade_rl.py) |
| Synthetic MOPD teacher helpers | [`qminiwasm/training/cascade_mopd_teacher.py`](../qminiwasm/training/cascade_mopd_teacher.py) |
| Epoch orchestration (cascade phase then AdamW MSE) | [`qminiwasm/training/loop.py`](../qminiwasm/training/loop.py) |

## Cascade RL (toy MDP + GRPO)

**Purpose:** Before each epoch’s supervised MSE updates, run a few **on-policy** steps on a small discrete-action MDP (`ToyRoutingEnv` in [`cascade_rl.py`](../qminiwasm/training/cascade_rl.py)). The policy can be:

- **`TinyCascadePolicy`** — MLP state → logits (default when no router on the model),
- **loop-owned `CascadeRouter`** — if `CASCADE_LEARNED_PROJECTOR=1`,
- **`QMiniWASM.cascade_router`** — if `USE_CASCADE_ROUTER=1`.

**GRPO:** For a group of `G` trajectories, each contributes a scalar **sum of log-probabilities** along the episode and a **total return**. Let `A` be the return tensor (optionally normalized to zero mean / unit variance across the group when `CASCADE_GROUP_SIZE` ≥ 2). The module minimizes:

**Loss ≈ − mean( A_i · (Σ_t log π(a_t | s_t))_i )**

so higher-return trajectories up-weight their action sequences. See [`CascadeGRPO.forward`](../qminiwasm/rl/cascade_grpo.py) for the exact implementation.

**Digest and coupling:** With `CASCADE_SEED_FROM_HIDDEN` (default on), the MDP initial state is derived from training hiddens. With `CASCADE_COUPLE_FORWARD` (default on), that digest blends **mean input hidden** and **mean `hybrid_inference` output** so the cascade phase tracks the live hybrid stack, not raw inputs only.

## MOPD (multi-domain on-policy distillation)

**Composite loss** (see [`MOPDLoss`](../qminiwasm/training/distillation.py)):

**L = λ_KL · L_KL + λ_feat · Σ_k w_k · L_feat(h_k^student, h_k^teacher)**

- **L_KL:** Optional KL between student and **detached** teacher auxiliary logits (temperature-scaled). The cascade phase in the training loop currently passes **no** auxiliary logits (`lambda_kl` is fixed at 0 for that call path).
- **L_feat:** Per named key `k` (a “domain”), MSE or cosine distance between student and **detached** teacher feature tensors. Multiple keys are supported for true multi-domain alignment.

**`MOPDLossConfig.feat_loss`** is `mse` or `cosine`, controlled by env **`CASCADE_MOPD_FEAT_LOSS`** when running via the engine.

### What the training loop does today

When **`CASCADE_MOPD_LAMBDA` > 0**, the loop adds an MOPD term on **final MDP state** embeddings:

- **Student:** `{"emb": s}` with gradients flowing through the cascade policy path as usual.
- **Teacher:** `{"emb": stopgrad(s + noise)}` — a **synthetic** target (Gaussian noise on the same state vector). See [`build_noise_state_mopd_fns`](../qminiwasm/training/cascade_mopd_teacher.py).

This is **not** distillation from a separate trained model or dataset; it acts as a **feature regularizer** that encourages the policy to be stable under small perturbations of the state embedding. The `MOPDLoss` API is intentionally general so future work can plug in real teachers (e.g. second checkpoint, EMA policy, or projector outputs from `hybrid_inference`).

## Training order (each epoch)

1. **Cascade phase** (if `CASCADE_RL` is on and `CASCADE_STEPS_PER_EPOCH` > 0): for each step, run `cascade_rl_train_step` with `CASCADE_GROUP_SIZE` rollouts; optionally add **λ_mopd · L_MOPD** with λ_mopd = `CASCADE_MOPD_LAMBDA`.
2. **Supervised phase:** AdamW on mean MSE between `hybrid_inference(hidden)` and targets; optional grad clip, plateau, early stop, holdout eval (see [TRAINING_DATA.md](TRAINING_DATA.md)).

## Environment variables (cascade + MOPD)

| Variable | Role |
|----------|------|
| `CASCADE_RL` | Set `0` / `false` to disable the cascade phase entirely. |
| `CASCADE_POLICY_LR` | Adam LR for the cascade policy (default tied to main LR; special case for `hf_tabular` in engine config). |
| `CASCADE_STEPS_PER_EPOCH` | Number of `cascade_rl_train_step` calls per epoch. |
| `CASCADE_GROUP_SIZE` | Trajectories per step; should be ≥ 2 when GRPO normalizes advantages. |
| `CASCADE_STATE_DIM`, `CASCADE_NUM_ACTIONS` | Toy MDP and policy output shape. |
| `CASCADE_MOPD_LAMBDA` | Weight of MOPD feature loss in the cascade step (`0` = GRPO only). |
| `CASCADE_MOPD_FEAT_LOSS` | `mse` (default) or `cosine` for feature matching inside MOPD. |
| `CASCADE_SEED_FROM_HIDDEN` | Seed toy MDP from data-derived digest. |
| `CASCADE_COUPLE_FORWARD` | Blend input and output means for digest (default on). |
| `USE_CASCADE_ROUTER`, `CASCADE_LEARNED_PROJECTOR`, `CASCADE_ROUTER_HIDDEN` | Which policy module is trained. |

Checkpoints can store **`cascade_policy`**; loading maps into `model.cascade_router` when `USE_CASCADE_ROUTER=1`. See [TRAINING_DATA.md](TRAINING_DATA.md) § Checkpoints and serving.

## Roadmap (not implemented)

- **Real teacher:** Second network, EMA weights, or main-model hidden projectors as `teacher_hidden_fn`.
- **KL branch in cascade:** Wire auxiliary logits into `MOPDLoss` with `lambda_kl` > 0 when a teacher provides matching logits.

## Tests

- [`tests/test_cascade_rl_mopd.py`](../tests/test_cascade_rl_mopd.py) — GRPO, MOPD loss, `cascade_rl_train_step` with MOPD.
- [`tests/test_loop_cascade_integration.py`](../tests/test_loop_cascade_integration.py) — `run_training_loop` including cascade with `CASCADE_MOPD_LAMBDA` > 0.
