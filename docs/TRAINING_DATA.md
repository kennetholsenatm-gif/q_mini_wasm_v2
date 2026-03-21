# Training data and ML engine results

This document describes how **training data** reaches `QMiniWASM.hybrid_inference`, what each source is good for, and how to read **run metrics** from `python -m engine`.

## Data sources (`TRAINING_DATA_SOURCE`)

| Source | Description | Aligns with WASM memory semantics? |
|--------|-------------|-------------------------------------|
| `mesh` | Embedded C snippets compiled to bare wasm32 (`hash`, `encrypt`, `network`, `routing`, `consensus`). Uses `WasmEngine` + linear memory snapshots encoded to 4096-dim vectors. | Yes (synthetic but real wasmtime execution). |
| `corpus` | JSON manifest listing paths to **bare wasm32** modules and export names. See [`corpus/manifest.json`](../corpus/manifest.json) and [`corpus/build_scratch_wasm.py`](../corpus/build_scratch_wasm.py). | Yes. |
| `hf_tabular` | Hugging Face `datasets`: each row is turned into UTF-8 text, then [`encode_linear_memory`](../qminiwasm/wasm/memory_encode.py) produces **hidden**; **target = hidden** (identity MSE). | No — trains the hybrid stack on encoded text, not wasm linear memory. |

Install Hugging Face support:

```bash
pip install -e ".[training]"
```

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
| `HF_NUM_SAMPLES` | Cap rows loaded; if unset for `hf_tabular`, a large auto slice is used (floor 8k, cap 200k, scales with batch) |
| `HF_TEXT_FIELDS` | Comma-separated row keys for text blob |
| `SEED` | Reproducibility (Python / NumPy / torch) |
| `GRAD_CLIP_NORM` | Global grad clip (e.g. `1.0`) |
| `LR_PLATEAU_PATIENCE` | `ReduceLROnPlateau` patience; `0` disables. For `hf_tabular`, defaults to `2` if unset |
| `LR_PLATEAU_FACTOR`, `LR_PLATEAU_MIN_LR` | Scheduler tuning |
| `EARLY_STOP_PATIENCE` | Stop if best epoch MSE does not improve for N epochs |
| `LOG_LEVEL` | Root log level (`INFO`, `WARNING`, …) |
| `HF_TOKEN` | Optional Hub token for rate limits |

Run from repo root:

```bash
python -m engine
```

Logs go to **stdout** (friendlier for PowerShell). Third-party HTTP loggers are quieted to WARNING.

## Example results (illustrative)

Runs depend on **hardware**, **seed**, **sample count**, and **epochs**. The following patterns have been observed on **CPU** with **CodeSearchNet Python**, **identity MSE** (`target == hidden`), and modest batch sizes:

- **Mean MSE per epoch** often drops quickly in the first few epochs (e.g. into the 20s–30s for epoch mean MSE with the default hybrid stack), then **flattens or oscillates**. That is expected: the model cannot perfectly reconstruct arbitrary 4096-d encodings of truncated text, and the objective is **not** full next-token modeling.
- **Plateau** does not by itself mean “no more data” — it often reflects **optimization / representation limits** for this objective. `ReduceLROnPlateau` (default for `hf_tabular`) and optional **early stopping** help avoid wasting epochs on noise.

### Returned metrics

`python -m engine` prints a dict including:

- `epochs_run`: epochs **actually completed** (early stop may stop sooner than `EPOCHS`)
- `final_loss`: last epoch’s mean MSE
- `metrics.epoch_mean_mse`: per-epoch means
- `metrics.epoch_learning_rates`: LR after each epoch
- `metrics.best_epoch_mean_mse`, `metrics.stopped_early`, `metrics.num_samples`, etc.

Use these series to compare runs with the same `SEED`, `HF_NUM_SAMPLES`, and `BATCH_SIZE`.

## When you need “real” WASM traces

For architecture-aligned supervision, prefer **`corpus`** or **`mesh`** (wasmtime + linear memory), not `hf_tabular`. WASI-heavy binaries (many WAPM packages) are not wired in the default bare-wasm path; see issue tracker / future milestones for WASI linker support.

## License notes

Use only datasets and WASM corpora whose **licenses** allow your use (training, redistribution of artifacts, etc.). **SPEC CPU** and similar suites are often **not** freely redistributable; prefer permissive OSS benchmarks and Hub datasets with clear terms.
