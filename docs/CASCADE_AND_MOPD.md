# Cascade reinforcement learning and MOPD

This document describes **cascade GRPO** (group-relative policy optimization for the escalation / routing head) and **MOPD** (multi-domain on-policy distillation) as implemented in **qminiwasm-core**, how they fit into `run_training_loop`, and which environment variables control them. **Prefer TOML** under **`configs/training/`** (see **[TRAINING_DATA.md](TRAINING_DATA.md)**); the helper script **`scripts/run_training_cascade_mopd.py`** defaults to **`--config configs/training/cascade_mopd.toml`** and may still inject **`CASCADE_MOPD_*`** and checkpoint paths via env for quick overrides. For the full env table, see **[TRAINING_DATA.md](TRAINING_DATA.md)**; maintainer-oriented process-env inventory: **[ENV_CI_OVERRIDES.md](ENV_CI_OVERRIDES.md)**.

## Where the code lives

| Component | Module |
|-----------|--------|
| GRPO loss (advantage-weighted policy gradient, optional group normalization) | [`qminiwasm/rl/cascade_grpo.py`](../qminiwasm/rl/cascade_grpo.py) |
| MOPD composite loss (optional KL on aux logits + per-key feature matching) | [`qminiwasm/training/distillation.py`](../qminiwasm/training/distillation.py) |
| Rollout + `cascade_rl_train_step` | [`qminiwasm/training/cascade_rl.py`](../qminiwasm/training/cascade_rl.py) |
| Synthetic MOPD teacher helpers | [`qminiwasm/training/cascade_mopd_teacher.py`](../qminiwasm/training/cascade_mopd_teacher.py) |
| Epoch orchestration (cascade phase then AdamW MSE) | [`qminiwasm/training/loop.py`](../qminiwasm/training/loop.py) |

## Cascade RL (toy MDP + GRPO)

**Not IBM Quantum:** The cascade phase is **PyTorch-only** (toy env + GRPO). It does **not** submit jobs to **IBM Quantum** or any Qiskit Runtime queue. Seeing `cascade_rl mean_loss=…` in logs does **not** imply a hardware quantum job ran.

**Default `quantum_router` in training:** `HybridQuantumMoE` ([`router.py`](../qminiwasm/fabric/router.py)) currently implements `forward` as a **pass-through** (`return hidden_states`). So the main supervised phase also does **not** execute circuits on IBM hardware by default. Config fields like `quantum_backend` are reserved for future / alternate code paths; they are **not** wired into this cascade MDP or that default router.

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

## Recommended continuation run (Cascade RL + MOPD)

Use this after a baseline supervised run (same data slice, seed, batch size, `HYBRID_ADAPTER`, and accelerator) so you can compare metrics fairly.

### Phase A — Enable MOPD

- Set **`CASCADE_MOPD_LAMBDA`** between **0.05** and **0.1** initially; increase toward **0.2** only if **train mean MSE** stays stable and cascade metrics look healthy.
- Use **`CASCADE_MOPD_FEAT_LOSS=mse`** first; try **`cosine`** in a separate run if you want a different geometry on the state embedding.
- Keep **`CASCADE_STEPS_PER_EPOCH=2`** and **`CASCADE_GROUP_SIZE=4`** unless you deliberately want more cascade compute (**`CASCADE_STEPS_PER_EPOCH`** 3–4); **`CASCADE_GROUP_SIZE`** must stay **≥ 2** when GRPO normalizes advantages.

### Phase B — Optional: cascade on the model for serving

- To expose **`cascade_logits`** on **`POST /infer`**, set **`USE_CASCADE_ROUTER=1`** during training and serving, with matching **`CASCADE_STATE_DIM`**, **`CASCADE_NUM_ACTIONS`**, **`CASCADE_ROUTER_HIDDEN`**.
- Alternatively use **`CASCADE_LEARNED_PROJECTOR=1`** without **`USE_CASCADE_ROUTER`**: the cascade head still trains, but the main `QMiniWASM` module has no attached router for inference-time logits.

### Phase C — Warm start

- Set **`CHECKPOINT_LOAD_PATH`** to your **best** artifact from the prior run (e.g. the file written by **`CHECKPOINT_BEST_PATH`** when train MSE improved).
- Point **`CHECKPOINT_SAVE_PATH`**, **`CHECKPOINT_BEST_PATH`**, and optionally **`CHECKPOINT_LATEST_PATH`** at **new** filenames so you do not overwrite the baseline.

### Environment checklist (goals → variables)

| Goal | Variables |
|------|-----------|
| MOPD on | `CASCADE_MOPD_LAMBDA` (e.g. `0.1`), optional `CASCADE_MOPD_FEAT_LOSS` |
| Same hardware | `ACCELERATOR=xpu` (or `cpu` / `cuda` as in baseline) |
| Repro / fair compare | `SEED`, same `HF_NUM_SAMPLES` / `DATA_PATH` / `HF_*` fields |
| Continuation | `CHECKPOINT_LOAD_PATH` → best prior artifact |
| New artifacts | Fresh `CHECKPOINT_SAVE_PATH`, `CHECKPOINT_BEST_PATH` |
| Optional eval | `EVAL_HOLDOUT_FRACTION`, `EVAL_EVERY_EPOCH=1` for eval MSE |

Also: [.env.example](../.env.example), [TRAINING_DATA.md](TRAINING_DATA.md) § Cascade RL and MOPD.

### Metrics to compare after each run

From the dict returned by **`python -m qminiwasm.engine`** (and logs):

| Metric | Use |
|--------|-----|
| `epoch_cascade_loss`, `epoch_cascade_return` | Cascade phase health |
| `cascade_mopd_lambda`, `cascade_mopd_feat_loss` | Confirm env applied |
| `epoch_mean_mse` | Primary train quality |
| `eval_mean_mse`, `epoch_eval_mean_mse` | If **`EVAL_HOLDOUT_FRACTION`** > 0 and **`EVAL_EVERY_EPOCH=1`** |
| `cascade_policy_mode` | `tiny_mlp` vs `loop_router` vs `model_router` |

If **train MSE regresses** while cascade loss spikes, **lower `CASCADE_MOPD_LAMBDA`** or **`CASCADE_POLICY_LR`** before increasing **`CASCADE_STEPS_PER_EPOCH`**.

A copy-paste **`.env`** sketch lives in **[.env.example](../.env.example)** under “Next run: Cascade RL + MOPD”.

**Script (injects env then runs the engine):** from repo root,

```bash
python scripts/run_training_cascade_mopd.py
python scripts/resume_training_cascade_mopd.py
python scripts/run_training_cascade_mopd.py --dry-run
python scripts/run_training_cascade_mopd.py --checkpoint-load ./artifacts/models/qminiwasm/best.pt --use-cascade-router
```

See `python scripts/run_training_cascade_mopd.py --help`.

### Long runs, disconnecting, and “coming back”

**Keep the process running** while you close the terminal or SSH session:

- **Linux / macOS:** `tmux new -s qtrain` (or `screen`), run your command inside the session, detach with `Ctrl+b` then `d` (tmux). Reattach later with `tmux attach -t qtrain`. Alternatively: `nohup python scripts/run_training_cascade_mopd.py >> training.log 2>&1 &` and use `tail -f training.log`.
- **Windows:** Use **Windows Terminal** and leave the tab open, or run from **WSL** with `tmux` as above. Avoid closing the console that owns the training process unless you use a persistent session tool.

**Stopping and later continuing training (weights only):** The loop does **not** restore optimizer state or “resume at epoch N” automatically. It **does** write **`CHECKPOINT_LATEST_PATH`** after **each full epoch** (weights + `meta` including `epoch`).

**One command** to start again from that file (sets `CHECKPOINT_LOAD_PATH` for you):

```bash
python scripts/resume_training_cascade_mopd.py
```

Equivalent: `python scripts/run_training_cascade_mopd.py --resume`. Resolution order: path from **`--checkpoint-latest`** (default `artifacts/models/cascade_mopd/latest.pt`), else **`CHECKPOINT_LATEST_PATH`** in `.env` if that file exists.

Expect a **fresh** epoch counter (epoch 1 of the new run), **new** AdamW state, and **reset** `ReduceLROnPlateau` / early-stop counters — only the **weights** carry over.

**Mid-epoch:** If you kill the process during an epoch, the **latest** file may be one epoch behind; prefer stopping after you see a “Wrote checkpoint” / epoch log line.

## Roadmap (not implemented)

- **Real teacher:** Second network, EMA weights, or main-model hidden projectors as `teacher_hidden_fn`.
- **KL branch in cascade:** Wire auxiliary logits into `MOPDLoss` with `lambda_kl` > 0 when a teacher provides matching logits.

## Tests

- [`tests/test_cascade_rl_mopd.py`](../tests/test_cascade_rl_mopd.py) — GRPO, MOPD loss, `cascade_rl_train_step` with MOPD.
- [`tests/test_loop_cascade_integration.py`](../tests/test_loop_cascade_integration.py) — `run_training_loop` including cascade with `CASCADE_MOPD_LAMBDA` > 0.
