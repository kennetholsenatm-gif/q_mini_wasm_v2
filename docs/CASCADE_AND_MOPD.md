# Cascade reinforcement learning and MOPD

This document describes **cascade GRPO/CISPO** (toy MDP policy optimization beside supervised loss) and **MOPD**-style feature regularization as implemented in the **C++ LibTorch training engine**. Configure knobs in **`configs/training/*.toml`** and **[`proto/training_engine.proto`](../proto/training_engine.proto)** (see **[TRAINING_DATA.md](TRAINING_DATA.md)**, **[TRAINING_NATIVE_PARITY.md](TRAINING_NATIVE_PARITY.md)**). Env names below map to **TrainingConfig** / telemetry fields; see **[ENV_CI_OVERRIDES.md](ENV_CI_OVERRIDES.md)**. [**DEPYTHONIZATION.md**](DEPYTHONIZATION.md) — interpreter-era loops are not documented here.

## Where the code lives (native)

| Component | Location |
|-----------|----------|
| Cascade curriculum, GRPO/CISPO, joint SFT+cascade | [`cpp/training/src/training_engine.cpp`](../cpp/training/src/training_engine.cpp), [`cpp/training/include/qminiwasm/training/training_engine.hpp`](../cpp/training/include/qminiwasm/training/training_engine.hpp) |
| LibTorch `CoreModule` train steps | [`cpp/training/`](../cpp/training/README.md) (see README for `train_step_joint_supervised_cascade`) |
| Config surface | [`training-wui/trainingconfig`](../training-wui/trainingconfig), gRPC + proto |

## Cascade RL (toy MDP + GRPO/CISPO)

**Not IBM Quantum:** The native cascade phase is **LibTorch toy MDP + policy loss**. It does **not** imply Qiskit Runtime jobs; quantum routing is a **separate** policy concern ([QUANTUM_QISKIT.md](QUANTUM_QISKIT.md)).

**Purpose:** On epochs/phases where cascade is enabled, the engine runs **on-policy** rollouts on a **small discrete-action MDP** with a trainable policy (`CascadeToyPolicy` / CISPO variant when configured). **GRPO-style** loss uses grouped returns and log-probs; see native implementation for exact math.

**Digest and coupling:** TOML / proto fields such as **`cascade_seed_from_hidden`** and **`cascade_couple_forward`** control whether MDP state is seeded from batch hiddens and whether forward outputs are blended into that digest—see schema and engine code.

## MOPD (multi-domain on-policy distillation)

**MOPD** adds a weighted **feature-matching** term (MSE or cosine) between student embeddings and a **synthetic noisy teacher** during the cascade phase when **`cascade_mopd_lambda` > 0**. It acts as a **regularizer**, not full distillation from a separate model. Parameters mirror **`CASCADE_MOPD_*`** env names in deployment docs.

## Training order (each native epoch)

1. **Cascade / curriculum phase** when enabled: toy rollouts + optional MOPD term + GRPO/CISPO update on the cascade policy.
2. **Supervised phase:** AdamW on mean MSE in **`CoreModule`** against targets; optional grad clip, plateau, early stop (see [TRAINING_DATA.md](TRAINING_DATA.md)).

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

### Phase B — Optional: cascade policy in checkpoints

- Train with **`use_cascade_router`** / matching **`cascade_state_dim`**, **`cascade_num_actions`**, **`cascade_router_hidden`** in TOML so the **LibTorch** checkpoint embeds the cascade head for future **native inference RPC** (HTTP **`POST /infer`** remains **501** until the Go↔C++ tensor bridge ships).
- With **`cascade_learned_projector`** only, the cascade head trains without attaching router logits to the main **CoreModule** export surface—see **[cpp/training/README.md](../cpp/training/README.md)** for tensor names.

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

From **WUI / gRPC telemetry** (and logs):

| Metric | Use |
|--------|-----|
| `epoch_cascade_loss`, `epoch_cascade_return` | Cascade phase health |
| `cascade_mopd_lambda`, `cascade_mopd_feat_loss` | Confirm env applied |
| `epoch_mean_mse` | Primary train quality |
| `eval_mean_mse`, `epoch_eval_mean_mse` | If **`EVAL_HOLDOUT_FRACTION`** > 0 and **`EVAL_EVERY_EPOCH=1`** |
| `cascade_policy_mode` | `tiny_mlp` vs `loop_router` vs `model_router` |

If **train MSE regresses** while cascade loss spikes, **lower `CASCADE_MOPD_LAMBDA`** or **`CASCADE_POLICY_LR`** before increasing **`CASCADE_STEPS_PER_EPOCH`**.

A copy-paste **`.env`** sketch lives in **[.env.example](../.env.example)** under “Next run: Cascade RL + MOPD”.

**Native run:** set cascade/MOPD fields in **`configs/training/*.toml`** (or the WUI), start **`qminiwasm_training_engine_server`**, then from **`training-wui/`**:

```bash
go run ./cmd/qmw-grpc-train -root .. -config configs/training/<your>.toml -grpc 127.0.0.1:50061
```

Use **[training-wui/README.md](../training-wui/README.md)** for flags and checkpoint paths; interpreter helper scripts under `scripts/` are **not** the operator path.

### Long runs, disconnecting, and “coming back”

**Keep the process running** while you close the terminal or SSH session:

- **Linux / macOS:** `tmux new -s qtrain` (or `screen`), run **`go run ./cmd/qmw-grpc-train …`** or the WUI inside the session, detach with `Ctrl+b` then `d` (tmux). Alternatively: `nohup go run … >> training.log 2>&1 &` and use `tail -f training.log`.
- **Windows:** Use **Windows Terminal** and leave the tab open, or run from **WSL** with `tmux` as above. Avoid closing the console that owns the training process unless you use a persistent session tool.

**Stopping and later continuing training (weights only):** The loop does **not** restore optimizer state or “resume at epoch N” automatically. It **does** write **`CHECKPOINT_LATEST_PATH`** after **each full epoch** (weights + `meta` including `epoch`).

**Resume weights:** set **`load_path`** / **`CHECKPOINT_LOAD_PATH`** in TOML or your environment to the **`latest.pt`** (or best) artifact, then run **`qmw-grpc-train`** again with the same **`grpc_addr`**. The C++ engine loads weights from the configured checkpoint table—see **[TRAINING_DATA.md](TRAINING_DATA.md)** and **[cpp/training/README.md](../cpp/training/README.md)**.

Expect a **fresh** epoch counter (epoch 1 of the new run), **new** AdamW state, and **reset** `ReduceLROnPlateau` / early-stop counters — only the **weights** carry over.

**Mid-epoch:** If you kill the process during an epoch, the **latest** file may be one epoch behind; prefer stopping after you see a “Wrote checkpoint” / epoch log line.

## Roadmap (not implemented)

- **Real teacher:** Second network, EMA weights, or main-model hidden projectors as `teacher_hidden_fn`.
- **KL branch in cascade:** Wire auxiliary logits into `MOPDLoss` with `lambda_kl` > 0 when a teacher provides matching logits.

## Tests

- [`tests/test_cascade_rl_mopd.py`](../tests/test_cascade_rl_mopd.py) — GRPO, MOPD loss, `cascade_rl_train_step` with MOPD.
- [`tests/test_loop_cascade_integration.py`](../tests/test_loop_cascade_integration.py) — `run_training_loop` including cascade with `CASCADE_MOPD_LAMBDA` > 0.
