# Training data and ML engine results

This document describes how **training data** reaches `QMiniWASM.hybrid_inference`, what each source is good for, and how to read **run metrics** from `python -m engine`.

## Data sources (`TRAINING_DATA_SOURCE`)

| Source | Description | Aligns with WASM memory semantics? |
|--------|-------------|-------------------------------------|
| `mesh` | Embedded C snippets compiled to bare wasm32 (`hash`, `encrypt`, `network`, `routing`, `consensus`). Uses `WasmEngine` + linear memory snapshots encoded to 4096-dim vectors. | Yes (synthetic but real wasmtime execution). |
| `corpus` | JSON manifest listing paths to **bare wasm32** modules and export names. See [`corpus/manifest.json`](../corpus/manifest.json) and [`corpus/build_scratch_wasm.py`](../corpus/build_scratch_wasm.py). | Yes. |
| `hf_tabular` | Hugging Face `datasets`: each row is turned into UTF-8 text, then [`encode_linear_memory`](../qminiwasm/wasm/memory_encode.py) produces **hidden**; **target = hidden** (identity MSE). | No — trains the hybrid stack on encoded text, not wasm linear memory. |

**Deployment-aligned data:** For behavior that should track **real WASM linear memory**, prefer **`mesh`** or **`corpus`**. Use **`hf_tabular`** for cheap scale and diversity (e.g. CodeSearchNet) as **pretraining** or auxiliary signal; it does not substitute for wasmtime-backed encodings at the edge.

Install Hugging Face support (includes **`python-dotenv`** so a repo-root **`.env`** is loaded automatically by `python -m engine` and `engine.train.main`; put `HUGGING_FACE_HUB_TOKEN` or `HF_TOKEN` there—do not commit `.env`):

```bash
pip install -e ".[training]"
```

**Intel GPU training:** install PyTorch with **XPU** support first, then set `ACCELERATOR=xpu` — see **[INSTALL_TORCH_XPU.md](INSTALL_TORCH_XPU.md)**.

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

[`qminiwasm/wasm/memory_encode.py`](../qminiwasm/wasm/memory_encode.py) maps bytes + scalars to a **4096-float** vector (stable layout: metadata slots + byte-derived floats). Training expects **hidden** and **target** each shaped `(4096,)`.

**Important:** only the first **~4088 UTF-8 bytes** of each row’s blob affect the embedding (the rest is unused). In **auto** HF mode (no `HF_TEXT_FIELDS`), the loader prepends a short labeled **`[context]`** block (`language`, `func_name`, `repo`, `path` when present) so that window mixes repository metadata with the **start** of the function body. Disable with `HF_CONTEXT_FIELDS=0`, or set `HF_CONTEXT_FIELDS=key1,key2` to override.

## Environment reference (engine / training loop)

| Variable | Role |
|----------|------|
| `TRAINING_DATA_SOURCE` | `mesh`, `corpus`, or `hf_tabular` |
| `DATA_PATH` | Corpus manifest path, or HF dataset id |
| `EPOCHS` | Max epochs (default 25 in engine config) |
| `BATCH_SIZE` | Batch size |
| `LEARNING_RATE` | AdamW LR |
| `HF_DATASET_CONFIG` | HF config name (e.g. `python`) |
| `HF_SPLIT` | HF split |
| `HF_NUM_SAMPLES` | Cap rows loaded; if unset for `hf_tabular`, auto slice uses floor **32k**, cap **300k**, scales with batch (×512) |
| `HF_WASI_SLICE_ONLY` | If `1` / `true`, **stream** the split and keep only rows whose encoded blob matches WASI markers (e.g. `wasi_snapshot_preview`, `wasm32-wasip`); use with `HF_NUM_SAMPLES=100000` for large WASI-heavy slices |
| `HF_WASI_MAX_SCAN` | Optional cap on how many **source** rows to scan before stopping (loader default **12_000_000** if unset when `HF_WASI_SLICE_ONLY` is on) |
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

**Tokens:** Put the value in a local `.env` or your shell profile. **Do not commit** secrets; `.env` is gitignored. Create a read token at [https://huggingface.co/settings/tokens](https://huggingface.co/settings/tokens).

### Checkpoints and evaluation (making training useful)

1. Train and save a final checkpoint:

```bash
export CHECKPOINT_SAVE_PATH=./artifacts/qminiwasm_trainable.pt
export SEED=42
python -m engine
```

2. Optional holdout metric (same forward as training, data not seen in the train split):

```bash
export EVAL_HOLDOUT_FRACTION=0.05
export EVAL_EVERY_EPOCH=1   # optional; else eval runs once at the end
python -m engine
```

3. **Serving:** run the API with the same file (see README): set `QMINIWASM_CHECKPOINT` to the saved path so `POST /infer` uses trained weights.

Checkpoint files include `format_version`, `d_model`, `quantum_router`, `ternary_expert`, and optional `meta` (training tags). Loading uses `strict=False` and logs missing/unexpected keys.

Run from repo root:

```bash
python -m engine
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
- `checkpoint_saved`, `checkpoint_best_saved`, `checkpoint_load_path`, `checkpoint_save_path`, `checkpoint_best_path`

Use these series to compare runs with the same `SEED`, `HF_NUM_SAMPLES`, and `BATCH_SIZE`.

## When you need “real” WASM traces

For architecture-aligned supervision, prefer **`corpus`** or **`mesh`** (wasmtime + linear memory), not `hf_tabular`. **WASI-linked** modules (imports `wasi_snapshot_preview1`, etc.) are instantiated via wasmtime’s **`Linker` + `WasiConfig`** (`qminiwasm.wasm.wasi_link.instantiate_wasmtime_module`). To compile C as **wasm32-wasip1** with wasi-sdk, set **`WASI_SDK_PATH`** and **`QMINIWASM_WASM_C_LINK=wasip1`** (default remains bare `wasm32` for embedded mesh snippets).

## License notes

Use only datasets and WASM corpora whose **licenses** allow your use (training, redistribution of artifacts, etc.). **SPEC CPU** and similar suites are often **not** freely redistributable; prefer permissive OSS benchmarks and Hub datasets with clear terms.
