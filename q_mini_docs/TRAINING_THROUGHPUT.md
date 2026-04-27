# Training throughput: what limits speed, and what to do next

This document implements the throughput investigation plan: how to confirm CPU behavior, how to see where time goes in `process_batch`, and how to choose between **smaller model shape** vs **parallelism / GPU backend** work.

## What “samples” means in logs

Native `samples` (and MCP `samples`) are **real contrastive rows completed in the current epoch** (`samples_processed`), and `samples_total` is **real cumulative contrastive rows** (`samples_processed_total`). They are not ingestion counters and are never substituted from DataSynthesizer volume.

Large configs use micro-batches (`adaptive_collect_limit`, `adaptive_route_topk_cap` in `autonomous_training_pipeline.cpp`), so one batch can take a while before `current_batch` increments. During that in-flight window, use:
- `train_rows_in_batch=X/Y`
- `ff_routes_in_batch=Z`

to confirm active work rather than a stall.

`training.samples_per_epoch` must be an explicit integer `>= 1` in `config/training_config.toml` (no implicit auto in the DLL). The same `[training]` block must include `micro_batch_cap`, `collect_floor`, `timing_to_stderr`, `serial_expert_train`, `sycl_route_mode` (`"auto"` | `"on"` | `"off"`), `sycl_trit_quant_min_moe_dim`, `goodness_log_level`, `allow_generated_negatives`, prefill fields, queue caps, and `lazy_init_initial_experts`; `qminiwasm` validates required keys and hard-fails startup on missing/out-of-range values. Throughput tuning is not read from `QMINI_TRAIN_*` env vars on this path.

## Strict no-fallback policy on this path

- No fake sample progress is injected into training counters.
- Invalid training config is not auto-corrected; init fails with explicit error codes.
- Empty positive conversion, missing negatives (when `allow_generated_negatives=false`), or zero-route router results fail the batch/run instead of silently patching inputs.

## SYCL, GPU, and Python

- **`[sycl]` in `config/training_config.toml`** does not toggle CMake; build with **`USE_SYCL=ON`** for SYCL objects to link into `q_mini_wasm_v2_core` / `q_training`.
- **SYCL probe**: with `USE_SYCL`, startup logs **`[TrainingPipeline] SYCL default device: …`** from `gf3_sycl_probe_log_device()`.
- **MoE routing on SYCL (partial GPU offload)**: `MoERouter::compute_routing_logits` can call **`moe_routing_symplectic_scores_sycl`** (parallel over experts) when the build has **`USE_SYCL`** and **`training.sycl_route_mode`** allows it: **`"on"`** prefers SYCL whenever **`total_experts >= 8`**, **`"off"`** keeps CPU logits, **`"auto"`** uses SYCL only when **`total_experts >= 128`** (still requires **`>= 8`** for any SYCL attempt). Forward–Forward / `gf3_layers` matmuls remain CPU/OpenMP unless further kernels are added.
- **WASM bridge** (`q_gf3_wasm.dll`, `wasm_api.cpp`): not loaded by native **`q_training`** / `qminiwasm` training loop unless your host explicitly `LoadLibrary`s it and dispatches. Training slowdown is therefore **almost never** `wasm_api` unless you are on that bridge path. When enabled with **`USE_SYCL`**, `SYCL_DispatchCommand` uses **`gf3_uint8_*_batch_sycl`** for GF(3) batch mul/add and **`wasm_tableau_*_sycl`** for Clifford gates on USM mappings; pass **`numQutrits`** (4th word) for correct tableau width when not inferable.
- **GPU** will generally stay idle for this workload until a backend implements GF(3) / tropical ops on the device.
- **Python** is not invoked on the native training hot path.

## Parallelism today

- **DataSynthesizer**: acquisition / perturbation use thread pools (I/O bound work can overlap).
- **Training thread**: one `std::thread` runs `training_loop` → `process_batch`, but inside that thread:
  - **OpenMP** (CMake option `QMINI_TRAINING_OPENMP`, default **ON**; MSVC uses `/openmp`) parallelizes hot **output-dimension** loops in `gf3_layers.cpp` (`TropicalForward`, `StandardForward`, Hebbian updates, and parts of `TrainForwardForward`) when `_OPENMP` is defined and dimensions are large enough.
  - **Per-route experts**: after lazy `ensure_expert`, independent experts in a single route run **`TrainForwardForward` + `ComputeGoodness` in parallel** under OpenMP when `top_k > 1` unless **`training.serial_expert_train = true`** in TOML.
- If Task Manager still shows **one** logical CPU hot, try a config with **`moe_top_k > 1`** and wider layers so OpenMP has enough work; very small shapes may stay serial inside pragmas.

## 1) Verify CPU pattern (todo: single-core saturation vs idle)

Use **Task Manager** → Performance: if **one logical processor** is pegged while others are idle during slow `samples` growth, that matches single-thread scalar training.

Run the helper script while training is active (from repo root):

```powershell
powershell -NoProfile -File .\scripts\verify_training_cpu_pattern.ps1
```

It samples `qminiwasm` CPU counters over a short window and prints a heuristic interpretation. It does not replace a full profiler but supports the “one hot thread” hypothesis.

## 2) Profile hotspots inside a batch (todo: route vs FF vs collect)

Rebuild `q_training` after pulling changes, then set **`training.timing_to_stderr = true`** in `config/training_config.toml` and start the host.

Stderr will include lines like:

```text
[TrainingTiming] total_ms=... collect_ms=... route_ms=... ff_train_ms=... routes=... collect_limit=... top_k_cap=...
```

- **`ff_train_ms` dominates** → cost is in `TrainForwardForward` / `ComputeGoodness` (dense GF(3) loops). Mitigations are **shape** (smaller widths, fewer expert layers) or a future **vectorized / GPU** implementation—not toggling `[sycl]` in TOML alone.
- **`route_ms` dominates** → MoE routing / scoring is the hotspot (less common after router `active_experts` is aligned with the training cap, but still measurable here).
- **`collect_ms` dominates** → waiting on the synthesizer / queues (I/O or backpressure).

Combine with **external** sample profilers (Very Sleepy, Visual Studio CPU Usage, etc.) on `qminiwasm.exe` for call stacks.

## 3) Decide the engineering goal (todo: shape vs parallelism vs SYCL)

| Goal | Scope | Notes |
|------|--------|--------|
| **Faster iteration without new backends** | **Config / model shape** | Reduce `neurons_per_layer`, `moe_hidden_dim`, `moe_num_experts`, `moe_expert_internal_layers`, or use a smoke/proof TOML for development. Same code path, less work per route. |
| **Higher CPU throughput** | **Parallelism inside training** | OpenMP across GF3 layer loops plus parallel per-route experts (after materialization); use timing + CPU view to confirm. SYCL still needs real kernels for GPU speedup. |
| **GPU / SYCL** | **New implementation** | Requires real kernels or libraries for GF(3) ops in the expert stack; not activated by existing TOML flags alone. |

Pick one primary direction before large refactors; use **`[TrainingTiming]`** stderr lines from **`training.timing_to_stderr`** to justify it.
