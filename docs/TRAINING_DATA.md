# Training data and ML engine results

This document describes how **training data** reaches `QMiniWASM.hybrid_inference`, what each source is good for, and how to read **run metrics** from `python -m engine`.

## Structured config (TOML)

Hyperparameters and non-secret options should live in **TOML** under [`configs/training/`](../configs/training/) so runs are diffable and reproducible. Example files:

- [`configs/training/mesh_cpu.toml`](../configs/training/mesh_cpu.toml) — synthetic mesh on CPU
- [`configs/training/hf_tabular_example.toml`](../configs/training/hf_tabular_example.toml) — Hugging Face tabular (set `HF_DATASET_CONFIG` / tokens in `.env` as needed)
- [`configs/training/schema.toml`](../configs/training/schema.toml) — commented reference for all sections

Run training:

```bash
python -m engine --config configs/training/mesh_cpu.toml
```

**Precedence:** values set in the TOML file override environment variables for those keys. **`ACCELERATOR`** is still applied from the environment when set (after loading the file), so containers can pin the device without editing the file. **`HUGGING_FACE_HUB_TOKEN` / `HF_TOKEN`** are always read from the environment when not passed explicitly — **never commit tokens in TOML**.

If you omit `--config`, the engine falls back to environment variables only and logs a **deprecation warning**.

Cascade + MOPD helper: [`scripts/run_training_cascade_mopd.py`](../scripts/run_training_cascade_mopd.py) defaults to `--config configs/training/cascade_mopd.toml` and still injects `CASCADE_MOPD_*` / checkpoint paths via env for one-off overrides.

## Data sources (`TRAINING_DATA_SOURCE`)

| Source | Description | Aligns with WASM memory semantics? |
|--------|-------------|-------------------------------------|
| `mesh` | Embedded C snippets compiled to bare wasm32 (`hash`, `encrypt`, `network`, `routing`, `consensus`). Uses `WasmEngine` + linear memory snapshots encoded to 4096-dim vectors. | Yes (synthetic but real wasmtime execution). |
| `corpus` | JSON manifest listing paths to **bare wasm32** modules and export names. See [`corpus/manifest.json`](../corpus/manifest.json) and [`corpus/build_scratch_wasm.py`](../corpus/build_scratch_wasm.py). | Yes. |
| `hf_tabular` | Hugging Face `datasets`: each row is turned into UTF-8 text, then [`encode_linear_memory`](../qminiwasm/enclave/memory_encode.py) produces **hidden**; **target = hidden** (identity MSE). | No — trains the hybrid stack on encoded text, not wasm linear memory. |

**Deployment-aligned data:** For behavior that should track **real WASM linear memory**, prefer **`mesh`** or **`corpus`**. Use **`hf_tabular`** for cheap scale and diversity (e.g. CodeSearchNet) as **pretraining** or auxiliary signal; it does not substitute for wasmtime-backed encodings at the edge.

Install Hugging Face support (includes **`python-dotenv`** so a repo-root **`.env`** is loaded automatically by `python -m engine` and `engine.train.main`; put `HUGGING_FACE_HUB_TOKEN` or `HF_TOKEN` there—do not commit `.env`):

```bash
pip install -e ".[training]"
```

**Intel GPU training:** install PyTorch with **XPU** support first, then set `ACCELERATOR=xpu` — see **[INSTALL_TORCH_XPU.md](INSTALL_TORCH_XPU.md)**.

## Multiple Hugging Face datasets (WUI / TOML)

You can mix **up to nine** Hub dataset ids in one `hf_tabular` run: set `[data].path` and optional `[huggingface].dataset_config` for the **primary** dataset, then list up to **eight** more in `[huggingface].extra_specs` as TOML inline tables (`path`, optional `dataset_config`). The engine splits the HF row budget **evenly** across all selected datasets, concatenates the encoded samples, then applies **mesh blend** (`mesh_blend_fraction`) if set.

Alternatively, set **`HF_EXTRA_SPECS`** to a JSON array of objects, e.g. `[{"path":"openai/gsm8k"},{"path":"wikitext","dataset_config":"wikitext-2-raw-v1"}]` (same limits; TOML overrides when both are present).

## Hugging Face coding data (recommended public corpus)

Default example in docs and tests:

- **Dataset:** `code-search-net/code_search_net`
- **Config:** `python` (set `HF_DATASET_CONFIG=python`)
- **Split:** `train` (`HF_SPLIT=train`)

### Richer text for early experiments (same dataset)

Auto-detection prefers **`whole_func_string`** when present (docstring + full function body). That avoids duplicating the function body at the start of the UTF-8 blob and gives a **richer prefix** for the fixed-width byte encoder than `func_code_string` alone.

Override explicitly, for example:

```text
HF_TEXT_FIELDS=whole_func_string
```

Or legacy-style concatenation:

```text
HF_TEXT_FIELDS=func_code_string,func_documentation_string
```

## WASM linear memory encoding

[`qminiwasm/enclave/memory_encode.py`](../qminiwasm/enclave/memory_encode.py) maps bytes + scalars to a **4096-float** vector (stable layout: metadata slots + byte-derived floats). Training expects **hidden** and **target** each shaped `(4096,)`.

**Important:** only the first **~4088 UTF-8 bytes** of each row’s blob affect the embedding (the rest is unused). In **auto** HF mode (no `HF_TEXT_FIELDS`), the loader prepends a short labeled **`[context]`** block (`language`, `func_name`, `repo`, `path` when present) so that window mixes repository metadata with the **start** of the function body. Disable with `HF_CONTEXT_FIELDS=0`, or set `HF_CONTEXT_FIELDS=key1,key2` to override.

## Environment reference (engine / training loop)

| Variable | Role |
|----------|------|
| `TRAINING_DATA_SOURCE` | `mesh`, `corpus`, or `hf_tabular` |
| `DATA_PATH` | Corpus manifest path, or HF dataset id |
| `HF_EXTRA_SPECS` | JSON array of extra `{path, dataset_config?}` (optional; prefer TOML `extra_specs`) |
| `EPOCHS` | Max epochs (default 25 in engine config) |
| `BATCH_SIZE` | Batch size |
| `LEARNING_RATE` | AdamW LR |
| `HF_DATASET_CONFIG` | HF config name (e.g. `python`) |
| `HF_SPLIT` | HF split |
| `HF_NUM_SAMPLES` | Cap rows loaded from Hub (total across merged datasets). If unset for `hf_tabular`, auto slice uses floor **32k**, cap **300k**, scales with batch (×512). For **500k** or more, set explicitly (env, `[huggingface] num_samples` in TOML, or training WUI **Advanced data options → HF max rows**) |
| `HF_WASI_SLICE_ONLY` | If `1` / `true`, **stream** the split and keep only rows whose encoded blob matches WASI markers (e.g. `wasi_snapshot_preview`, `wasm32-wasip`); use with `HF_NUM_SAMPLES=100000` for large WASI-heavy slices |
| `HF_WASI_MAX_SCAN` | Optional cap on how many **source** rows to scan before stopping (loader default **12_000_000** if unset when `HF_WASI_SLICE_ONLY` is on) |
| `HF_DATASET_REVISION` | Git ref / commit for `datasets.load_dataset` (default **`main`** when unset) |
| `HF_MESH_BLEND_FRACTION` | For `hf_tabular` only: append mesh curriculum samples equal to **fraction × len(HF rows)** (min 1); combined list is **shuffled** when `SEED` is set |
| `HF_STREAMING` | If `1` / `true`, stream rows instead of materializing split |
| `HF_MAX_SCAN_ROWS` | Optional cap on scanned source rows for general HF path |
| `HF_MAX_BUFFERED_ROWS` | Optional memory cap for retained encoded rows in loader |
| `HF_TEXT_TRUNCATE_BYTES` | Optional per-row UTF-8 truncation before encoding |
| `HF_DETERMINISTIC_KEEP_EVERY_N` | Deterministic subsampling stride (`N`), keeps rows where `index % N == 0` |
| `HF_TEXT_FIELDS` | Comma-separated row keys for text blob |
| `HF_CONTEXT_FIELDS` | `0` / `off` / `false` disables `[context]` prefix; comma list selects metadata keys; unset uses defaults in auto mode only |
| `SEED` | Reproducibility (Python / NumPy / torch) |
| `GRAD_CLIP_NORM` | Global grad clip (e.g. `1.0`) |
| `LR_PLATEAU_PATIENCE` | `ReduceLROnPlateau` patience; `0` disables. For `hf_tabular`, defaults to `2` if unset |
| `LR_PLATEAU_FACTOR`, `LR_PLATEAU_MIN_LR` | Scheduler tuning |
| `EARLY_STOP_PATIENCE` | Stop if best epoch MSE does not improve for N epochs |
| `LOG_LEVEL` | Root log level (`INFO`, `WARNING`, …) |
| `HUGGING_FACE_HUB_TOKEN` | Optional Hub token (official name; passed to `datasets.load_dataset`) |
| `HF_TOKEN` | Same as above if `HUGGING_FACE_HUB_TOKEN` is unset (shorthand) |
| `CHECKPOINT_LOAD_PATH` | Load `quantum_router` + `ternary_expert` weights before the first training epoch (finetune / warm start) |
| `CHECKPOINT_SAVE_PATH` | After training, write final weights to this file (single `.pt`; parent dirs created) |
| `CHECKPOINT_BEST_PATH` | Whenever epoch **train** mean MSE improves, overwrite this file with the best-so-far weights |
| `EVAL_HOLDOUT_FRACTION` | Float in `(0,1)`: hold out that fraction for **eval** MSE (not used in optimizer). With `SEED` set, indices are shuffled deterministically; without `SEED`, the **last** fraction of rows is eval |
| `EVAL_EVERY_EPOCH` | If `1` / `true` and holdout is enabled, log **eval** mean MSE after each epoch (`metrics.epoch_eval_mean_mse`) |
| `TARGET_MEAN_MSE` | Optional threshold on **mean** `torch.nn.functional.mse_loss` over all elements (same scale as logged `mean_mse` / `eval_mean_mse`). Example: `1e-4` for **0.0001** |
| `STOP_ON_TARGET_MSE` | If `1` / `true`, stop early when the threshold is met (prefers **holdout eval** when `EVAL_EVERY_EPOCH=1`; otherwise train mean MSE — see training loop warning) |
| `HYBRID_ADAPTER` | If `1` / `true`, enable residual MLP after ternary (see subsection above) |
| `HYBRID_ADAPTER_HIDDEN` | Bottleneck width (integer ≥ 32; default **1024**) |
| `CASCADE_RL`, `CASCADE_POLICY_LR`, `CASCADE_STEPS_PER_EPOCH`, `CASCADE_GROUP_SIZE`, `CASCADE_STATE_DIM`, `CASCADE_NUM_ACTIONS`, `CASCADE_MOPD_LAMBDA`, `CASCADE_MOPD_FEAT_LOSS`, `CASCADE_SEED_FROM_HIDDEN` | Cascade GRPO phase before each epoch’s MSE batches (see `.env.example` and **[CASCADE_AND_MOPD.md](CASCADE_AND_MOPD.md)**) |
| `CASCADE_COUPLE_FORWARD` | If `0` / `false`, digest uses **input hidden mean only**; otherwise (default) blends **0.5 × input mean + 0.5 × `hybrid_inference` output mean** per batch |
| `USE_CASCADE_ROUTER` | Attach `CascadeRouter` on the model (trained by cascade optimizer, not main AdamW) |
| `CASCADE_LEARNED_PROJECTOR` | Loop-owned `CascadeRouter` when model has no router |
| `CASCADE_ROUTER_HIDDEN` | Router MLP width |

## Edge fast-iteration presets

- `configs/training/edge_fast_iter.toml`: tiny sampled curriculum for quick local cycle time.
- `configs/training/edge_full_curriculum_streaming.toml`: broader curated curriculum with bounded streaming.
- `configs/training/edge_curriculum_mix.toml`: reference dataset mix groups (reasoning/tool-use/alignment).

Run:

```bash
python -m engine --config configs/training/edge_fast_iter.toml
python -m engine --config configs/training/edge_full_curriculum_streaming.toml
python scripts/benchmark_edge_curriculum.py
```
| `QAOA_SIMULATOR_BACKEND` | `auto`, `statevector`, or `mps` for `qiskit_statevector` execution mode |
| `QAOA_MPS_MAX_BOND_DIM` | Optional MPS bond-dimension cap (when using MPS backend) |
| `QAOA_PRUNE_ENABLED`, `QAOA_PRUNE_THRESHOLD`, `QAOA_PRUNE_MIN_NODES` | Topology pruning controls before QUBO build |
| `QAOA_WARM_START_CACHE_TTL` | Number of recent topology fingerprints retained for angle warm-start cache |

**`hf_tabular` defaults (when env vars are unset):** `TARGET_MEAN_MSE=1e-4`, `GRAD_CLIP_NORM=1`, `CASCADE_POLICY_LR = 0.5 × learning_rate`, and `learning_rate=1.5e-4` when the engine constructor LR is the default **1e-4** and `LEARNING_RATE` is not set. Set env vars explicitly to override.

**Tokens:** Put the value in a local `.env` or your shell profile. **Do not commit** secrets; `.env` is gitignored. Create a read token at [https://huggingface.co/settings/tokens](https://huggingface.co/settings/tokens).

**Gated datasets:** Some Hub datasets require accepting terms on the dataset page and setting **`HUGGING_FACE_HUB_TOKEN`** or **`HF_TOKEN`** before `load_dataset` can load them. Presets and sample configs in this repo use **public** datasets (e.g. CodeSearchNet) so default runs work without a token; if you add a gated id to `extra_specs`, expect to authenticate first.

**`datasets` 3.x+ / script-less Hub:** Datasets that only ship as legacy Python scripts may raise *Dataset scripts are no longer supported*. Prefer Parquet-backed repos (e.g. **`google-research-datasets/mbpp`** with **`dataset_config = "full"`** or **`"sanitized"`** instead of **`Muennighoff/mbpp`**).

**Multi-dataset without a primary Hub id:** Set **`[data].path`** to the reserved placeholder **`qminiwasm/hf-multi`** (alias: **`qminiwasm/multi`**) and list every real dataset under **`[huggingface].extra_specs`**. The engine does not call `load_dataset` on the placeholder; sample budget is split across extras only (up to **nine** Hub datasets in this mode). Use your **model name / WUI “Build + Run” name** to pick a new **`artifacts/models/<slug>/`** output directory; the placeholder only affects how Hub rows are merged, not where checkpoints are written.

### Cascade RL and MOPD

The training loop runs a short **cascade GRPO** phase (toy routing MDP) before each epoch’s supervised MSE when `CASCADE_RL` is enabled. Optional **`CASCADE_MOPD_LAMBDA`** > 0 adds a **MOPD feature loss** during that phase: student features are the final MDP state `s`; the teacher is currently a **synthetic** stop-gradient target **`s + noise`** (regularization), not distillation from another model. **`CASCADE_MOPD_FEAT_LOSS`** selects `mse` vs `cosine` for that feature term.

Full detail, equations, and code pointers: **[CASCADE_AND_MOPD.md](CASCADE_AND_MOPD.md)**.

**Continuation run (baseline → Cascade + MOPD):** see **§ Recommended continuation run** in [CASCADE_AND_MOPD.md](CASCADE_AND_MOPD.md) and the matching block in [.env.example](../.env.example).

### Checkpoints and evaluation (making training useful)

**Layout:** Prefer one directory per run or model under **`artifacts/models/<slug>/`** with stable names: **`final.pt`** (end-of-run / `save_path`), **`best.pt`**, **`latest.pt`**. This matches the **training WUI** (“Build + Run”) and the **`agent_bundle.json`** / **`serve.toml`** sidecars written next to those files.

1. Train and save a final checkpoint (paths can live in TOML `[checkpoint]` or in `.env`):

```bash
export SEED=42   # optional; or set [training].seed in TOML
python -m engine --config configs/training/mesh_cpu.toml
# with checkpoints only in env:
export CHECKPOINT_SAVE_PATH=./artifacts/models/qminiwasm/final.pt
export CHECKPOINT_BEST_PATH=./artifacts/models/qminiwasm/best.pt
export CHECKPOINT_LATEST_PATH=./artifacts/models/qminiwasm/latest.pt
python -m engine --config configs/training/mesh_cpu.toml
```

2. Optional holdout metric (same forward as training, data not seen in the train split): set `[eval]` in your TOML (`holdout_fraction`, `every_epoch`) or use `EVAL_HOLDOUT_FRACTION` / `EVAL_EVERY_EPOCH` when those keys are omitted from the file.

3. **Serving:** run the API with the same weights file. Set `QMINIWASM_CHECKPOINT` (or `CHECKPOINT_LOAD_PATH`), or point **`QMINIWASM_SERVE_CONFIG`** at a TOML file such as [`configs/serve/default.toml`](../configs/serve/default.toml) with a `[serve]` table (`checkpoint`, `hybrid_adapter`, cascade dims). Environment variables still fill any field omitted from that file. Optional **`USE_CASCADE_ROUTER=1`** and matching cascade dims align with training; responses may include **`cascade_logits`** per row. Set **`HYBRID_ADAPTER=1`** if the checkpoint contains `hybrid_adapter` weights (or rely on auto-attach on load).

Checkpoint files include `format_version`, `d_model`, `quantum_router`, `ternary_expert`, optional **`hybrid_adapter`**, optional **`cascade_policy`** (same tensors as `model.cascade_router` when used), and optional `meta` (training tags). Loading uses `strict=False` and logs missing/unexpected keys.

Run from repo root:

```bash
python -m engine --config configs/training/mesh_cpu.toml
```

Logs go to **stdout** (friendlier for PowerShell). Third-party HTTP loggers are quieted to WARNING.

## Example results (illustrative)

Runs depend on **hardware**, **seed**, **sample count**, and **epochs**. The following patterns have been observed on **CPU** with **CodeSearchNet Python**, **identity MSE** (`target == hidden`), and modest batch sizes:

- **Mean MSE per epoch** often drops quickly in the first few epochs (e.g. into the 20s–30s for epoch mean MSE with the default hybrid stack), then **flattens or oscillates**. That is expected: the model cannot perfectly reconstruct arbitrary 4096-d encodings of truncated text, and the objective is **not** full next-token modeling.
- **Plateau** does not by itself mean “no more data” — it often reflects **optimization / representation limits** for this objective. `ReduceLROnPlateau` (default for `hf_tabular`) and optional **early stopping** help avoid wasting epochs on noise.

### Goal: mean MSE ≈ **1e-4** on real-world data

- **What the number means:** training uses `F.mse_loss(output, target)` with default `reduction="mean"` over **all** elements in the batch (shape includes the full **4096**-dim vectors). Targets from **mesh/corpus** are bounded (roughly **[0, 1]** in most body slots); **HF tabular** uses the same encoder, so MSE is on a comparable scale.
- **Extra capacity (scoped):** set **`HYBRID_ADAPTER=1`** to add a **residual MLP** after the ternary expert (`4096 → HYBRID_ADAPTER_HIDDEN → 4096`, default hidden **1024**). It is included in the AdamW step, saved in checkpoints as **`hybrid_adapter`**, and **auto-built on load** when serving from a checkpoint that contains those weights. This is the supported path toward harder **low-MSE** fits without changing the loss.
- **Strict 1e-4 remains demanding** for very diverse rows: the quantum router stack is still mostly an **identity** forward; the adapter adds float MLP capacity while the ternary path stays STE-constrained. Combine with **holdout eval**, **LR schedule**, and **enough data/epochs** as needed.
- **Operational setup:** set `TARGET_MEAN_MSE=1e-4`, use a **holdout** (`EVAL_HOLDOUT_FRACTION`) and **`EVAL_EVERY_EPOCH=1`** so success is judged on **eval** mean MSE, and optionally `STOP_ON_TARGET_MSE=1`. After the run, check `metrics.target_mse_met`, `metrics.target_mse_reported_value`, and `metrics.target_mse_reported_name`.

### Returned metrics

`python -m engine` prints a dict including:

- `epochs_run`: epochs **actually completed** (early stop may stop sooner than `EPOCHS`)
- `final_loss`: last epoch’s mean MSE (on the **train** split when holdout is used)
- `metrics.epoch_mean_mse`: per-epoch train means
- `metrics.epoch_learning_rates`: LR after each epoch
- `metrics.best_epoch_mean_mse`, `metrics.stopped_early`, `metrics.num_samples`, `metrics.num_samples_train`, `metrics.num_samples_eval`
- `metrics.eval_mean_mse` when holdout is enabled (and finite)
- `metrics.epoch_eval_mean_mse` when `EVAL_EVERY_EPOCH=1`
- `metrics.target_mean_mse_goal`, `metrics.target_mse_met`, `metrics.target_mse_reported_value`, `metrics.target_mse_reported_name` when `TARGET_MEAN_MSE` is set
- `metrics.stopped_on_target_mse` when early exit on target is used
- `metrics.cascade_couple_forward`, `metrics.cascade_policy_mode` when cascade RL is enabled
- `metrics.hf_mesh_blend_fraction` when `hf_tabular` and blend > 0
- `checkpoint_saved`, `checkpoint_best_saved`, `checkpoint_load_path`, `checkpoint_save_path`, `checkpoint_best_path`

Use these series to compare runs with the same `SEED`, `HF_NUM_SAMPLES`, and `BATCH_SIZE`.

## When you need “real” WASM traces

For architecture-aligned supervision, prefer **`corpus`** or **`mesh`** (wasmtime + linear memory), not `hf_tabular`. **WASI-linked** modules (imports `wasi_snapshot_preview1`, etc.) are instantiated via wasmtime’s **`Linker` + `WasiConfig`** (`qminiwasm.enclave.wasi_link.instantiate_wasmtime_module`). To compile C as **wasm32-wasip1** with wasi-sdk, set **`WASI_SDK_PATH`** and **`QMINIWASM_WASM_C_LINK=wasip1`** (default remains bare `wasm32` for embedded mesh snippets).

## License notes

Use only datasets and WASM corpora whose **licenses** allow your use (training, redistribution of artifacts, etc.). **SPEC CPU** and similar suites are often **not** freely redistributable; prefer permissive OSS benchmarks and Hub datasets with clear terms.
