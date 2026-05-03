# Training throughput: what limits speed, and what to do next

This document implements the throughput investigation plan: how to confirm CPU behavior, how to see where time goes in `process_batch`, and how to choose between **smaller model shape** vs **parallelism / GPU backend** work.

## What “samples” means in logs

Native `samples` (and MCP `samples`) are **real contrastive rows completed in the current epoch** (`samples_processed`), and `samples_total` is **real cumulative contrastive rows** (`samples_processed_total`). They are not ingestion counters and are never substituted from DataSynthesizer volume.

Large configs use micro-batches (`adaptive_collect_limit`, `adaptive_route_topk_cap` in `autonomous_training_pipeline.cpp`), so one batch can take a while before `current_batch` increments. During that in-flight window, use:
- `train_rows_in_batch=X/Y`
- `ff_routes_in_batch=Z`

to confirm active work rather than a stall.

`training.samples_per_epoch` must be an explicit integer `>= 1` in `config/training_config.toml` (no implicit auto in the DLL). Core `[training]` keys (`micro_batch_cap`, `collect_floor`, timing/route/SYCL knobs, prefill fields, queue caps) are validated at startup; `qminiwasm` hard-fails startup on invalid ranges. Parent-derived knobs now resolve as follows when omitted or set to `0`: `training.target_routes_per_batch = effective_collect_limit * effective_top_k`, `training.collect_min_rows_per_batch = effective_collect_limit`, `training.lazy_init_initial_experts = effective_top_k` (lazy-init mode), and `model.moe_ff_active_internal_layers = model.expert_internal_layers`. Optional **`training.gf3_ff_batched_weight_mib`** remains `0` (native default 1024 MiB) or `>= 2` for explicit override. Native training does not read throughput or SYCL toggles from process environment variables on this path.

## Strict no-fallback policy on this path

- No fake sample progress is injected into training counters.
- Invalid training config is not auto-corrected; init fails with explicit error codes.
- Empty positive conversion, missing negatives (when `allow_generated_negatives=false`), or zero-route router results fail the batch/run instead of silently patching inputs.

## SYCL, GPU, and Python

- **`[sycl]` in `config/training_config.toml`** does not toggle CMake; native `q_mini_wasm_v2` **always** links SYCL (`USE_SYCL` is not optional—configure with **icx/icpx** or AdaptiveCpp; see `CONTRIBUTING.md`).
- **SYCL probe**: in SYCL-enabled native builds, startup logs **`[TrainingPipeline] SYCL default device: …`** from `gf3_sycl_probe_log_device()`.
- **MoE symplectic routing logits are SYCL-only** (no host-CPU scorer): `MoERouter::compute_routing_logits` uses **`moe_routing_symplectic_scores_sycl`** whenever **`total_experts >= 8`** and **`training.sycl_route_mode`** is **`"auto"`** or **`"on"`** (same effective threshold). Below 8 experts the SYCL kernel shape is unused and routing cannot use this path. If **`USE_SYCL`** is set but SYCL fails, routing throws. Forward–Forward / `gf3_layers` follow GPU-mandatory FF rules when built with SYCL.
- **WASM bridge** (`q_gf3_wasm.dll`, `wasm_api.cpp`): not loaded by native **`q_training`** / `qminiwasm` training loop unless your host explicitly `LoadLibrary`s it and dispatches. Training slowdown is therefore **almost never** `wasm_api` unless you are on that bridge path. When the WASM bridge is built with SYCL, `SYCL_DispatchCommand` uses **`gf3_uint8_*_batch_sycl`** for GF(3) batch mul/add and **`wasm_tableau_*_sycl`** for Clifford gates on USM mappings; pass **`numQutrits`** (4th word) for correct tableau width when not inferable.
- **GPU** runs the mandatory SYCL kernels for GF(3) feed, routing, and contrastive negatives in native `USE_SYCL` builds; idle GPUs mean configuration or dispatch failure, not an optional CPU substitute.
- **Python** is not invoked on the native training hot path.

## Parallelism today

- **DataSynthesizer**: acquisition / perturbation use thread pools (I/O bound work can overlap).
- **`training.parallel_batches`**: `training_loop` may launch that many concurrent `process_batch()` calls (each uses a SYCL queue on its OS thread). Lazy MoE **resident expert** sizing multiplies per-batch route pressure by this count so it matches Go’s derived default `target_routes ≈ collect_limit × moe_top_k × parallel_batches`. Aggressive profiles are fine when **feed** stays ahead of work: scale **train/raw/acq** queues, prefill, and (if needed) **`target_routes_per_batch`** so **`parallel_batches × collect throughput`** does not outrun the synthesizer—same physics at frontier scale as at smaller scale.
- **Training thread**: one `std::thread` runs `training_loop` → `process_batch`, but inside that thread:
  - **GF(3) forward / FF / Hebbian** use **SYCL on GPU** in `USE_SYCL` builds (not a CPU “optional mode”).
  - **OpenMP** (CMake `QMINI_TRAINING_OPENMP`) may parallelize **orchestration** (e.g. per-route expert work dispatch) when `top_k > 1` — it does not replace GPU-mandatory layer math.
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

## Aggressive profile (high-throughput, GPU-backed routing)

When you want to push hard (and accept higher CPU/GPU load), prefer:

- `training.sycl_route_mode = "on"` or `"auto"` (with `moe_experts >= 8`) so symplectic routing stays on the **GPU** in native SYCL builds.
- `training.sycl_trit_quant_min_moe_dim = 1` so quantization does not silently stay on CPU for smaller inputs.
- High queue/prefill caps so the trainer never starves (`acq/raw/train` and `prefill_target_samples`).
- `training.micro_batch_cap` and `training.collect_floor` as large as you want (host validates positive integers only). The pipeline derives the per-batch collect limit from TOML (`min(batch_size, micro_batch_cap)` vs `collect_floor`).
- Leave `target_routes_per_batch = 0` and `collect_min_rows_per_batch = 0` unless you need explicit override; parent-derived values stay aligned with collect/top-k math.
- Explicit thread pools and I/O pacing (no hidden mapping from `features.worker_threads`): `training.acquisition_threads`, `training.perturbation_threads`, `training.checkpoint_async_queue_max`, `training.collect_empty_backoff_*`, `training.metrics_heartbeat_sec`.

New `[Pipeline]` logs expose effective sizing from config:

- `collect_effective=<n>`
- `route_topk=<k>`

If `collect_effective` stays very low while queues remain full, throughput is limited by your TOML collect sizing (or by batch/micro-batch settings), not by a hidden code cap.

## Stability tuning (dial down without “floor-slamming”)

When you are stabilizing a crashy run, **change one family of knobs at a time** and keep the profile *structurally* the same (large MoE, FF multi-row, SYCL path)—avoid resetting everything to minimums so you still learn what actually broke.

**Prefer (ratio / feed alignment)**

1. **Crash log breadcrumb** (`%LOCALAPPDATA%\q_mini_training_crash.log`) + last `[TrainingPipeline]` / `[Training]` line — note whether failure is collect, route, multi-row FF, or thread spawn (`POST_POLL_NATIVE_DEBUG.md`).
2. **Parallel workers vs feed**: if using large **`training.parallel_batches`**, confirm **train/raw/acq** queues and **prefill** can sustain **`parallel_batches × collect_limit`** pairs over time. Before shrinking model shape, try **one step down on `parallel_batches`** (e.g. 32 → 24 → 16) or **up on queue caps / producer threads** — that preserves aggressive scale while fixing starvation / contention.
3. **Route budget**: keep **`target_routes_per_batch = 0`** unless you have a measured mismatch; if you set it explicitly, derive from collect × top_k × parallel_batches rather than a tiny constant.
4. **Multi-row SYCL chunks**: adjust **`training.gf3_ff_batched_weight_mib`** and/or **`ff_multi_row_slots_chunk`** as a **pair** (host staging budget), not “turn off **`ff_multi_row_batch`**” unless the breadcrumb proves multi-slot FF.

**Use sparingly (single-axis shrink)**

- **`training.micro_batch_cap`** or **`batch_size`**: reduce by a **modest fraction** (e.g. 25–50% per step) to lower per-tick work, not to 1.
- **`training.parallel_contrastive_rows`**: temporarily false only if OpenMP row parallel is implicated in stacks.
- **SYCL / driver**: contrastive negatives and feed math are **GPU-mandatory**; fix the device/runtime — there is no host substitute knob in the training pipeline.

**Anti-pattern**: setting **`parallel_batches`**, **`batch_size`**, **all queue caps**, and **multi-row** knobs to their lowest values at once. That may stop the crash but erases which subsystem was at fault and often makes GPU/CPU look “fine” while hiding the next bottleneck.

## Resource probe + autoscale (Go → DLL)

`qminiwasm` can scale volatile concurrency **down together** from your TOML **ceiling** using quick probes (logical CPUs + installed physical RAM, optional SYCL **global_mem_size** via `q_training.dll`). This keeps queue / `parallel_batches` / thread ratios aligned instead of hand-tuning each knob.

Enable in **`[training]`**:

- **`resource_autoscale = true`**
- **`resource_autoscale_ref_logical_cpus`** / **`resource_autoscale_ref_ram_gib`**: the machine your TOML was tuned for (defaults: 32 CPUs, 128 GiB if unset or ≤0).
- **`resource_autoscale_ram_headroom`**: fraction (0–1) of physical RAM used in the comparison (default 0.9 if missing or out of range).
- **`resource_autoscale_min_parallel_batches`**: floor for **`parallel_batches`** after scaling (default 1).
- **`resource_autoscale_scale_threads`**: also scale acquisition / perturbation / checkpoint async queue (default true).
- **`resource_autoscale_scale_gf3_ff_mib`**: also scale **`gf3_ff_batched_weight_mib`** (default true).
- **`resource_autoscale_include_vram`**: fold SYCL device VRAM into the scale (default true when autoscale is on — see below).

**Scale** is `min(1, cpuRatio, ramRatio [, vramRatio])`: on a *larger* host than the reference, you still cap at 1.0 and keep your TOML values. On a *smaller* host, **`parallel_batches`**, prefill, acq/raw/train caps, directory/jsonl sample caps, optional thread counts, optional GF(3) MiB hint, and an **explicit** positive **`target_routes_per_batch`** are multiplied by that scale (integers rounded, never above the TOML ceiling). **`target_routes_per_batch = 0`** stays 0 (native still derives from collect × top_k × parallel batches).

At init / start, when enabled, stderr gets one line: **`[Training] resource_autoscale: ... parallel_batches a→b ...`**. RAM may be unknown on some platforms (then only CPU vs reference is used).

### SYCL device VRAM (optional)

When **`resource_autoscale_include_vram = true`** (default), `qminiwasm` calls **`Training_ProbeSyclDevice`** in `q_training.dll` before `Training_InitSession`. It resolves the SYCL device using the same rules as **`training.sycl_gpu_device_index`** (enumerated GPUs, preferred GPU when index is `-1`, then `gpu_selector_v` / `default_selector_v`), reads **`info::device::global_mem_size`**, and folds a **VRAM ratio** into the same multiplicative scale:

- **`resource_autoscale_ref_vram_gib`**: reference VRAM for your profile (default **32** GiB when unset or ≤0 so it lines up with typical `sycl_prereserve_gib` frontier configs).
- **`resource_autoscale_vram_headroom`**: fraction (0–1) applied to reported global memory before comparing to the reference (default **0.85** if missing or out of range).

If the probe fails (non-SYCL build, no device, driver error), autoscale **falls back to CPU/RAM only** (`vram_f=1` in the ratio sense; the log shows `vram=?`).

**Not covered yet**: live VRAM pressure, unified memory carve-out accuracy, or **mid-run** re-scaling — only a **one-shot** probe at pipeline init/start.

## Aggressive checkpointing (do not lose long runs)

`training.checkpoint_interval` now controls frequent checkpoint cadence (batches, with epoch-boundary checkpoints still preserved). For long-running frontier jobs, keep this value small (for example `1-2`) so a crash does not discard hours of progress.

Expected behavior:

- frequent `[TrainingPipeline] Checkpoint written: checkpoint_epoch_..._batch_....qmini` logs
- successful stop/restart resume from the newest checkpoint file

Tradeoff:

- lower interval => more disk I/O and slight training overhead
- higher interval => better peak throughput but higher progress-loss risk

## Windows SYCL bring-up (one command)

Use:

```powershell
powershell -NoProfile -File .\scripts\build_qminiwasm.ps1
```

What it does:

- checks for `dpcpp` / `icpx` on `PATH`
- configures native build with **Ninja + `icx-cl`** (SYCL is always required by `q_mini_wasm_v2` CMake; do not pass `-DUSE_SYCL=OFF`)
- rebuilds `q_training` and **`qminiwasm.exe` at the repo root** (same directory as `q_training.dll`; not under `cmd/qminiwasm/`)
- copies Intel oneAPI runtime DLLs next to that `qminiwasm.exe` so launch does not depend on `PATH`
- prints runtime verification targets (`SYCL default device`, `router_sycl=1`, `collect_effective` / `route_topk` fields)

If oneAPI is not installed, the script still performs a deterministic configure/build and reports the exact blocker.
