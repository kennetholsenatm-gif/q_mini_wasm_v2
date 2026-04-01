# Training data and ML engine results

This document describes how **training data** reaches the **C++ LibTorch** training core (fixed-width rows → forward → loss), what each source is good for, and how **TOML and environment** line up with the stack. **Operator training** runs through the **Training WUI** and the C++ engine over **gRPC** using `configs/training/*.toml` (see **[TRAINING_NATIVE_PARITY.md](TRAINING_NATIVE_PARITY.md)**). Tables that name **environment** variables reflect **proto-mapped** fields and deployment overrides—see **[ENV_CI_OVERRIDES.md](ENV_CI_OVERRIDES.md)**. **[DEPYTHONIZATION.md](DEPYTHONIZATION.md)** tracks removal of interpreter-era wording elsewhere.

**Configuration policy:** Prefer **`configs/training/*.toml`** and the **[Training WUI](../training-wui/README.md)** for training knobs. Use **`.env`** only for **credentials** ([environment-variables.md](environment-variables.md)).

## Local training (native path)

Start **`qminiwasm_training_engine_server`** on **`grpc_addr`** from **`configs/wui.toml`**, then run the **Training WUI** from **`training-wui/`** (see **[training-wui/README.md](../training-wui/README.md)**). For a headless one-shot with the same TOML, use **`go run ./cmd/qmw-grpc-train`** from **`training-wui/`** with **`-root`**, **`-config`**, and **`-grpc`**. The sections below cover TOML, data sources, checkpoints, and metrics—not cloud provisioning.

## Structured config (TOML)

Hyperparameters and non-secret options should live in **TOML** under [`configs/training/`](../configs/training/) so runs are diffable and reproducible. Example files:

- [`configs/training/mesh_cpu.toml`](../configs/training/mesh_cpu.toml) — synthetic mesh on CPU
- [`configs/training/hf_tabular_example.toml`](../configs/training/hf_tabular_example.toml) — Hugging Face tabular (dataset fields in TOML; Hub tokens in `.env` per [environment-variables.md](environment-variables.md))
- [`configs/training/curriculum_phase1_stem_example.toml`](../configs/training/curriculum_phase1_stem_example.toml) — smoke-scale **Phase 1 STEM / math** Hub mix (see curated curriculum below)
- [`configs/training/curriculum_phase1_code_example.toml`](../configs/training/curriculum_phase1_code_example.toml) — smoke-scale **Phase 1 code** Hub ids
- [`configs/training/curriculum_phase3_cot_example.toml`](../configs/training/curriculum_phase3_cot_example.toml) — smoke-scale **Phase 3 CoT / MOPD-oriented** Hub ids
- [`configs/training/schema.toml`](../configs/training/schema.toml) — commented reference for all sections

Example (from **`training-wui/`** after the C++ server is up):

```bash
go run ./cmd/qmw-grpc-train -root .. -config configs/training/mesh_cpu.toml -grpc 127.0.0.1:50061
```

**Precedence:** values set in the TOML file override environment variables for those keys where both apply. **`ACCELERATOR`** is still applied from the environment when set (after loading the file), so containers can pin the device without editing the file — listed under [ENV_CI_OVERRIDES.md](ENV_CI_OVERRIDES.md). **`HUGGING_FACE_HUB_TOKEN` / `HF_TOKEN`** are always read from the environment when not passed explicitly — **never commit tokens in TOML**.

## Enclave tiers and memory boundaries (preset + override)

Use `[enclave]` in TOML to express runtime memory policy by tier.

| Tier | `enclave_tier` | Default pages | Approx linear memory | Memory64 default |
|------|----------------|---------------|----------------------|------------------|
| 1 | `micro` | `4096` | ~256 MiB | Off |
| 2 | `meso` | `32768` | ~2 GiB | Off |
| 3 | `macro` | `131072` | ~8 GiB | On (`wasm_memory64_max_mb=8192`) |
| 4 | `workgroup` | `262144` | ~16 GiB | On (`wasm_memory64_max_mb=16384`) |
| 5 | `enterprise_core` | `4194304` | ~256 GiB | On (`wasm_memory64_max_mb=262144`) |

Resolution order is deterministic:
1. explicit overrides (`max_linear_memory_pages`, `wasm_memory64_max_mb`, `use_memory64`)
2. tier defaults (`enclave_tier`)
3. generic runtime defaults

Example:

```toml
[enclave]
enclave_tier = "macro"
# explicit override wins over macro defaults:
max_linear_memory_pages = 196608
wasm_memory64_max_mb = 12288.0
use_memory64 = true
```

## Data sources (`TRAINING_DATA_SOURCE`)

| Source | Description | Aligns with WASM memory semantics? |
|--------|-------------|-------------------------------------|
| `mesh` | Embedded C snippets compiled to bare wasm32 (`hash`, `encrypt`, `network`, `routing`, `consensus`). Uses `WasmEngine` + linear memory snapshots encoded to 4096-dim vectors. | Yes (synthetic but real wasmtime execution). |
| `corpus` | JSON manifest listing paths to **bare wasm32** modules and export names. See [`corpus/manifest.json`](../corpus/manifest.json) and corpus build tooling under [`corpus/`](../corpus/). | Yes. |
| `hf_tabular` | Hugging Face rows: UTF-8 text blob → **4096-float** vector via the same layout as [`linear_memory_encode.hpp`](../cpp/wasm/linear_memory_encode.hpp); **target = hidden** (identity MSE). | No — scales text encodings, not wasm linear memory. |

**Deployment-aligned data:** For behavior that should track **real WASM linear memory**, prefer **`mesh`** or **`corpus`**. Use **`hf_tabular`** for cheap scale and diversity (e.g. CodeSearchNet) as **pretraining** or auxiliary signal; it does not substitute for wasmtime-backed encodings at the edge.

## Curated Cascade curriculum (Hub)

The whitepaper-style catalog **[QMINIWASM Dataset and Expert Curation.md](research/QMINIWASM%20Dataset%20and%20Expert%20Curation.md)** recommends specific Hugging Face datasets for each Cascade RL phase (STEM/math, execution-aware code, CoT-heavy distillation). This repo wires them in as **TOML examples** and **`hf_tabular`** / native-gRPC row fetch—not as full next-token SFT or execution-based RL.

**Important limitations (read before scaling):**

- **`hf_tabular` and native HF fetch** implement **identity MSE** on the **fixed-width encoder** vectors (`target == hidden`). They do **not** implement logits-level SFT, compiler rewards, or xCodeEval test execution from that document.
- Use **[TRAINING_NATIVE_PARITY.md](TRAINING_NATIVE_PARITY.md)** for the **LibTorch** capability surface; curated datasets here are **distribution shape**, not a guarantee of whitepaper-scale training.

**Phase → Hub ids → wiring hints**

| Phase / role | Hub dataset | `text_fields` / notes |
|--------------|-------------|------------------------|
| Phase 1 — STEM / logic | `nvidia/OpenMathInstruct-2` | Often message-style rows: set **`text_fields`** explicitly after inspecting the dataset card (e.g. `problem`, `generated_solution`, nested `messages`). When unset, the **C++** row fetch uses deterministic key-concatenation heuristics (`hf_datasets_rows.cpp`). |
| Phase 1 — STEM / multi-trace | `open-r1/OpenR1-Math-220k` | Prefer explicit **`text_fields`** from the dataset schema (e.g. problem + solution columns). |
| Phase 1 — code / execution | `NTU-NLP-sg/xCodeEval` | Multi-config dataset: set **`dataset_config`** per [Hyperparameters Hub README](https://huggingface.co/datasets/NTU-NLP-sg/xCodeEval); choose columns that flatten to text for the encoder. |
| Phase 1 — edge-oriented code | `nex-agi/coding-eval` | Set **`text_fields`** from the dataset viewer; may require gated access + **`HF_TOKEN`**. |
| Phase 3 — CoT / distillation | `a-m-team/AM-DeepSeek-R1-Distilled-1.4M` | Long reasoning traces: list all string columns you need in order (e.g. prompt + full response / chain-of-thought sections) via **`[huggingface].text_fields`**. |
| Phase 3 — skeleton CoT | `melongena/SSR-CoT-16k` | Use **`text_fields`** matching SSR fields from the dataset card. |
| Phase 3 — open reasoning anchor | `open-thoughts/OpenThoughts-114k` | Use **`text_fields`** per card; pair with low **`num_samples`** for smoke runs. |

**TOML → proto → C++:** When **`[huggingface].text_fields`** is set, the Training WUI / **`BuildHfProto`** maps it to **`HfDatasetParams.text_fields`** in [`proto/training_engine.proto`](../proto/training_engine.proto). The engine passes those keys into [`hf_datasets_rows.cpp`](../cpp/training/src/hf_datasets_rows.cpp). If **`text_fields`** is empty, fetch concatenates top-level string fields in key order.

**Theory pillars (experts):** The same doc’s “Task 2” experts (1.58-bit, CISPO, MOPD, tropical routing, stabilizer simulation) map to implementation status in **[research/ARCHITECTURE_WHITEPAPERS.md](research/ARCHITECTURE_WHITEPAPERS.md)** (traceability matrix) and **[TRAINING_NATIVE_PARITY.md](TRAINING_NATIVE_PARITY.md)**—link there instead of duplicating long citations in this file.

**Hub authentication:** put **`HUGGING_FACE_HUB_TOKEN`** or **`HF_TOKEN`** in a local **`.env`** (see [environment-variables.md](environment-variables.md)); never commit secrets.

**Intel GPU / LibTorch:** native engine device selection follows TOML and `ACCELERATOR` as mapped in **[ENV_CI_OVERRIDES.md](ENV_CI_OVERRIDES.md)** and **[INSTALL_TORCH_XPU.md](INSTALL_TORCH_XPU.md)** (toolchain-focused; operator paths are Go + C++).

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

Alternative comma-separated concatenation:

```text
HF_TEXT_FIELDS=func_code_string,func_documentation_string
```

## WASM linear memory encoding

[`cpp/wasm/linear_memory_encode.hpp`](../cpp/wasm/linear_memory_encode.hpp) defines **`encode_linear_memory_u8`**: bytes + scalars → **4096-float** vector (metadata slots + byte-derived floats). Training expects **hidden** and **target** each shaped `(4096,)`.

**Model `io_d_model`:** default width **4096**; `[model].io_d_model` in TOML must match. **`CoreModule`** checkpoints require consistent stem geometry—see **[cpp/training/README.md](../cpp/training/README.md)**.

**Important:** only the first **~4088 UTF-8 bytes** of each row’s blob affect the embedding (the rest is unused). In **auto** HF mode (no `HF_TEXT_FIELDS`), the loader prepends a short labeled **`[context]`** block (`language`, `func_name`, `repo`, `path` when present) so that window mixes repository metadata with the **start** of the function body. Disable with `HF_CONTEXT_FIELDS=0`, or set `HF_CONTEXT_FIELDS=key1,key2` to override.

## Environment reference (engine / training loop)

The variables below are **process-environment** hooks (shell, CI, containers). **Prefer TOML** and the WUI when the schema exposes the same knob. **Secrets** are summarized in [environment-variables.md](environment-variables.md); a broader maintainer list lives in [ENV_CI_OVERRIDES.md](ENV_CI_OVERRIDES.md).

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
| `SEED` | Reproducibility (engine / HF / LibTorch seed chain) |
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
| `TARGET_MEAN_MSE` | Optional threshold on **mean MSE** over all elements (same scale as logged `mean_mse` / `eval_mean_mse`). Example: `1e-4` |
| `STOP_ON_TARGET_MSE` | If `1` / `true`, stop early when the threshold is met (prefers **holdout eval** when `EVAL_EVERY_EPOCH=1`; otherwise train mean MSE — see training loop warning) |
| `HYBRID_ADAPTER` | If `1` / `true`, enable residual MLP after ternary (see subsection above) |
| `HYBRID_ADAPTER_HIDDEN` | Bottleneck width (integer ≥ 32; default **1024**) |
| `CASCADE_RL`, `CASCADE_POLICY_LR`, `CASCADE_STEPS_PER_EPOCH`, `CASCADE_GROUP_SIZE`, `CASCADE_STATE_DIM`, `CASCADE_NUM_ACTIONS`, `CASCADE_MOPD_LAMBDA`, `CASCADE_MOPD_FEAT_LOSS`, `CASCADE_SEED_FROM_HIDDEN` | Cascade GRPO phase before each epoch’s MSE batches (prefer [`configs/training/cascade_mopd.toml`](../configs/training/cascade_mopd.toml); see **[CASCADE_AND_MOPD.md](CASCADE_AND_MOPD.md)**; CI env in [ENV_CI_OVERRIDES.md](ENV_CI_OVERRIDES.md)) |
| `CASCADE_COUPLE_FORWARD` | If `0` / `false`, digest uses **input hidden mean only**; otherwise (default) blends **0.5 × input mean + 0.5 × CoreModule forward output mean** per batch |
| `USE_CASCADE_ROUTER` | Train checkpoint with attached cascade router head (native `CoreModule` surface) |
| `CASCADE_LEARNED_PROJECTOR` | Train loop-owned cascade projector without attaching router to main export |
| `CASCADE_ROUTER_HIDDEN` | Router MLP width |
| `QAOA_SIMULATOR_BACKEND` | `auto`, `statevector`, or `mps` for `qiskit_statevector` execution mode |
| `QAOA_MPS_MAX_BOND_DIM` | Optional MPS bond-dimension cap (when using MPS backend) |
| `QAOA_PRUNE_ENABLED`, `QAOA_PRUNE_THRESHOLD`, `QAOA_PRUNE_MIN_NODES` | Topology pruning controls before QUBO build |
| `QAOA_WARM_START_CACHE_TTL` | Number of recent topology fingerprints retained for angle warm-start cache |

## Edge fast-iteration presets

- `configs/training/edge_fast_iter.toml`: tiny sampled curriculum for quick local cycle time.
- `configs/training/edge_full_curriculum_streaming.toml`: broader curated curriculum with bounded streaming.
- `configs/training/edge_curriculum_mix.toml`: reference dataset mix groups (reasoning/tool-use/alignment).

Run (native CLI; requires C++ server as above):

```bash
cd training-wui
go run ./cmd/qmw-grpc-train -root .. -config configs/training/edge_fast_iter.toml
go run ./cmd/qmw-grpc-train -root .. -config configs/training/edge_full_curriculum_streaming.toml
```

**`hf_tabular` defaults (when env vars are unset):** `TARGET_MEAN_MSE=1e-4`, `GRAD_CLIP_NORM=1`, `CASCADE_POLICY_LR = 0.5 × learning_rate`, and `learning_rate=1.5e-4` when the engine constructor LR is the default **1e-4** and `LEARNING_RATE` is not set. Set env vars explicitly to override.

**Tokens:** Put the value in a local `.env` or your shell profile. **Do not commit** secrets; `.env` is gitignored. Create a read token at [https://huggingface.co/settings/tokens](https://huggingface.co/settings/tokens).

**Gated datasets:** Some Hub datasets require accepting terms on the dataset page and setting **`HUGGING_FACE_HUB_TOKEN`** or **`HF_TOKEN`** before `load_dataset` can load them. Presets and sample configs in this repo use **public** datasets (e.g. CodeSearchNet) so default runs work without a token; if you add a gated id to `extra_specs`, expect to authenticate first.

**`datasets` 3.x+ / script-less Hub:** Datasets that only ship as Hub Python scripts may raise *Dataset scripts are no longer supported*. Prefer Parquet-backed repos (e.g. **`google-research-datasets/mbpp`** with **`dataset_config = "full"`** or **`"sanitized"`** instead of **`Muennighoff/mbpp`**).

**Multi-dataset without a primary Hub id:** Set **`[data].path`** to the reserved placeholder **`qminiwasm/hf-multi`** (alias: **`qminiwasm/multi`**) and list every real dataset under **`[huggingface].extra_specs`**. The engine does not call `load_dataset` on the placeholder; sample budget is split across extras only (up to **nine** Hub datasets in this mode). Use your **model name / WUI “Build + Run” name** to pick a new **`artifacts/models/<slug>/`** output directory; the placeholder only affects how Hub rows are merged, not where checkpoints are written.

### Cascade RL and MOPD

The **native engine** runs a **cascade GRPO/CISPO** phase (toy MDP) before supervised MSE when enabled. Optional **`CASCADE_MOPD_LAMBDA`** > 0 adds **MOPD-style** feature regularization during that phase. **`CASCADE_MOPD_FEAT_LOSS`** selects `mse` vs `cosine`.

Full detail: **[CASCADE_AND_MOPD.md](CASCADE_AND_MOPD.md)**.

**Continuation run (baseline → Cascade + MOPD):** see **§ Recommended continuation run** in [CASCADE_AND_MOPD.md](CASCADE_AND_MOPD.md) and [`configs/training/cascade_mopd.toml`](../configs/training/cascade_mopd.toml).

### Checkpoints and evaluation (making training useful)

**Layout:** Prefer one directory per run or model under **`artifacts/models/<slug>/`** with stable names: **`final.pt`** (end-of-run / `save_path`), **`best.pt`**, **`latest.pt`**. This matches the **training WUI** (“Build + Run”) and the **`agent_bundle.json`** / **`serve.toml`** sidecars written next to those files.

1. Train and save checkpoints via the **WUI** or **`qmw-grpc-train`** (prefer TOML `[checkpoint]`; env below is for shell/CI — see [ENV_CI_OVERRIDES.md](ENV_CI_OVERRIDES.md)):

```bash
export SEED=42   # optional; or set [training].seed in TOML
cd training-wui && go run ./cmd/qmw-grpc-train -root .. -config configs/training/mesh_cpu.toml
# with checkpoints only in env:
export CHECKPOINT_SAVE_PATH=./artifacts/models/qminiwasm/final.pt
export CHECKPOINT_BEST_PATH=./artifacts/models/qminiwasm/best.pt
export CHECKPOINT_LATEST_PATH=./artifacts/models/qminiwasm/latest.pt
go run ./cmd/qmw-grpc-train -root .. -config configs/training/mesh_cpu.toml
```

2. Optional holdout metric (same forward as training, data not seen in the train split): set `[eval]` in your TOML (`holdout_fraction`, `every_epoch`) or use `EVAL_HOLDOUT_FRACTION` / `EVAL_EVERY_EPOCH` when those keys are omitted from the file.

3. **Serving:** use **Go `qmw-serve`** or the WUI **`POST /api/serve/start`** with **`serve.toml`** / **`agent_bundle.json`** next to the model (see [training-wui/README.md](../training-wui/README.md)). Prefer **`configs/serve/*.toml`** (e.g. [`configs/serve/default.toml`](../configs/serve/default.toml) with a `[serve]` table: `checkpoint`, `hybrid_adapter`, cascade dims). Env such as `QMINIWASM_CHECKPOINT` / `QMINIWASM_SERVE_CONFIG` is documented in [ENV_CI_OVERRIDES.md](ENV_CI_OVERRIDES.md). Optional **`USE_CASCADE_ROUTER=1`** and matching cascade dims align with training; HTTP **`POST /infer`** may include **`cascade_logits`** per row when the stack supports it. Set **`HYBRID_ADAPTER=1`** if the checkpoint contains `hybrid_adapter` weights (or rely on auto-attach on load).

Checkpoint files include `format_version`, `d_model`, `quantum_router`, `ternary_expert`, optional **`hybrid_adapter`**, optional **`cascade_policy`** (same tensors as `model.cascade_router` when used), and optional `meta` (training tags). Loading uses `strict=False` and logs missing/unexpected keys.

Training logs stream on **stderr/stdout** from **`qmw-grpc-train`** and in the **WUI** Mission Control panel.

## Example results (illustrative)

Runs depend on **hardware**, **seed**, **sample count**, and **epochs**. The following patterns have been observed on **CPU** with **CodeSearchNet Python**, **identity MSE** (`target == hidden`), and modest batch sizes:

- **Mean MSE per epoch** often drops quickly in the first few epochs (e.g. into the 20s–30s for epoch mean MSE with the default hybrid stack), then **flattens or oscillates**. That is expected: the model cannot perfectly reconstruct arbitrary 4096-d encodings of truncated text, and the objective is **not** full next-token modeling.
- **Plateau** does not by itself mean “no more data” — it often reflects **optimization / representation limits** for this objective. `ReduceLROnPlateau` (default for `hf_tabular`) and optional **early stopping** help avoid wasting epochs on noise.

### Goal: mean MSE ≈ **1e-4** on real-world data

- **What the number means:** the **C++ engine** uses **mean MSE** over **all** elements in the micro-batch (including full **4096**-dim vectors). Targets from **mesh/corpus** are bounded (roughly **[0, 1]** in most body slots); **HF tabular** rows use the same width, so MSE is on a comparable scale.
- **Extra capacity (scoped):** enable the **hybrid adapter** in TOML (`hybrid_adapter` / env mapped in proto) to add a **residual MLP** after the ternary expert (`4096 → hidden → 4096`). It is optimized with the main loop and saved in checkpoints when present.
- **Strict 1e-4 remains demanding** for very diverse rows: combine **holdout eval**, **LR schedule**, and **enough data/epochs** as needed.
- **Operational setup:** set `TARGET_MEAN_MSE=1e-4`, use a **holdout** (`EVAL_HOLDOUT_FRACTION`) and **`EVAL_EVERY_EPOCH=1`** so success is judged on **eval** mean MSE, and optionally `STOP_ON_TARGET_MSE=1`. After the run, check `metrics.target_mse_met`, `metrics.target_mse_reported_value`, and `metrics.target_mse_reported_name`.

### Returned metrics

The **WUI** and **gRPC telemetry** expose epoch loss, eval MSE, LR, cascade fields, and sample counts (see **`training-wui/telemetry_grpc_payload.go`** and **`proto/training_engine.proto`**). Use telemetry series to compare runs with the same `SEED`, `HF_NUM_SAMPLES`, and `BATCH_SIZE`.

## When you need “real” WASM traces

For architecture-aligned supervision, prefer **`corpus`** or **`mesh`** (WASM + linear memory), not `hf_tabular`. **WASI-linked** modules (`wasi_snapshot_preview1`, etc.) are loaded by your **host WASM runtime** with a WASI-capable linker. To compile C as **wasm32-wasip1** with wasi-sdk, set **`WASI_SDK_PATH`** and your toolchain’s **wasip1** link flags (bare `wasm32` remains typical for embedded mesh snippets).

## Remote GPU / OpenTofu

For RunPod pods, OpenTofu lifecycle under `infra/runpod/`, serverless workers, SSH sync, and remote **native** training (`qmw-grpc-train` on the pod), use the operator runbook only—do not duplicate provisioning commands here. See **[OPERATIONS_RUNBOOK.md](operations/OPERATIONS_RUNBOOK.md)** and the **[`infra/runpod/`](../../infra/runpod/)** tree.

## License notes

Use only datasets and WASM corpora whose **licenses** allow your use (training, redistribution of artifacts, etc.). **SPEC CPU** and similar suites are often **not** freely redistributable; prefer permissive OSS benchmarks and Hub datasets with clear terms.
