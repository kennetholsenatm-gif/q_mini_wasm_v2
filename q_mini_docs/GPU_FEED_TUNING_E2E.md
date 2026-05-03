# Autonomous pipeline end-to-end tuning (GPU feed)

This document implements the **Autonomous Pipeline End-to-End Tuning Plan**: stage map, active profile rationale, runtime verification, and measurement loop for sustaining high device utilization. It does not replace `TRAINING_THROUGHPUT.md`; read both.

## 1) Pipeline map (stages and subsystems)

| Stage | Code / subsystem | Starvation / throttle signals |
|-------|-------------------|--------------------------------|
| **Prefill** | `DataSynthesizer` staging vs `prefill_target_samples`, `prefill_timeout_ms`, `prefill_poll_ms` | `PREFILL_STARVATION`, raw queue empty while prefill incomplete |
| **Acquire** | Acquisition thread pool × `acquisition_threads` | `acq_queue_cap` persistently full with low `train_rows_in_batch` |
| **Raw / perturb** | Perturbation pool × `perturbation_threads`, neg generation | `raw_queue_cap` backlog, neg missing counters |
| **Train queue** | Contrastive rows → `process_batch` | `train_queue_cap`, `collect_ms` high in `[TrainingTiming]` |
| **Collect window** | `collect_window_ms`, `collect_min_rows_per_batch`, adaptive collect | Batches flush early with thin rows → low GPU route work |
| **Route** | `MoERouter`, SYCL symplectic logits when `sycl_route_mode` + build | `route_ms` dominant; `router_sycl=0` in logs when SYCL expected |
| **FF / expert** | `TrainForwardForward`, `gf3_layers`, OpenMP, `parallel_contrastive_rows`, chunking | `ff_train_ms` dominant |
| **Checkpoint** | `checkpoint_interval` (batches + epoch boundary), `checkpoint_async_queue_max`, `checkpoint_data_dir` | Periodic stalls when disk flush aligns with hot batches |
| **Telemetry / stream** | `metrics_heartbeat_sec`, WUI streaming, MCP | Extra `update_metrics` / `emit_metrics` if heartbeat too aggressive |
| **Host bridge** | Native path: `qminiwasm` → `q_training.dll` only; WASM bridge **not** on this path unless explicitly loaded | Split-path bugs show as wrong DLL or missing SYCL in logs |

## 2) Active tuned profile (C:\q_mini_data\config\training_config.toml)

Edits are **configuration-only** for this pass: feed responsiveness, slightly fewer checkpoint interrupts, coarser telemetry heartbeat, and larger FF expert chunks for OpenMP. Model shape (experts, top_k, widths) is unchanged to preserve your frontier run.

**Rationale summary**

- **Feed**: Slightly faster `prefill_poll_ms` to reduce wakeup latency; queue caps unchanged (already pair-flood scale).
- **Collect**: Slightly shorter `collect_window_ms` to avoid holding rows for a full 200 ms when the train buffer is ready.
- **Compute**: Larger `ff_expert_chunk_size` so OpenMP has fewer, larger chunks on wide experts (`moe_top_k=80`).
- **Checkpoint / I/O**: Higher `checkpoint_interval` (batch/epoch gate) to reduce how often synchronous checkpoint work can coincide with heavy batches—trade progress-loss risk vs throughput (see below).
- **Telemetry**: Higher `metrics_heartbeat_sec` to cut periodic metric emit cost during long runs.
- **Data filter**: Slightly higher `checkpoint_interval_samples` to reduce indexer checkpoint chatter on huge corpora.

**Tradeoff (checkpoints vs throughput)**  
Fewer batch checkpoints improve steady throughput but increase hours-at-risk on crash. If you need maximum safety, lower `training.checkpoint_interval` toward the values in `TRAINING_THROUGHPUT.md` (e.g. 1–2) and accept more periodic I/O.

## 3) Verify runtime paths (checklist)

- [ ] **`qminiwasm.exe`** and **`q_training.dll`** from the same repo root build (`scripts/build_qminiwasm.ps1`).
- [ ] **`training.sycl_route_mode = "on"`** and native SYCL DLL (logs: `[TrainingPipeline] SYCL default device`, `[GF3 SYCL]` device lines, no `-17` SYCL-missing init).
- [ ] **`QMINI_DATA_DIR`** (or default `C:/q_mini_data`) points at the TOML you edited.
- [ ] **No** `QMINI_TRAINING_CONFIG` override unless intentional (smoke tests use repo `config/training_config.smoke.toml`).
- [ ] **`OMP_NUM_THREADS`** set in the environment for GF3 OpenMP (see `run_qminiwasm_training_verbose.ps1`; defaults to processor count).

## 4) Timing-driven iteration (GPU floor > 50%)

**Reality check (from `TRAINING_THROUGHPUT.md`)**  
Much of the expert stack is still **host GF(3) loops**; **SYCL** today mainly accelerates **routing logits** when enabled. Sustained **GPU utilization > 50%** may not be achievable until more GF(3) / FF work is device-offloaded. This tuning pass targets **removing host-side starvation** (collect/queues/checkpoints) so the **GPU routing path is fed consistently**—necessary but not always sufficient for the 50% floor.

**Low % in Task Manager on Iris Xe (often expected)**  
Windows Task Manager averages GPU utilization over relatively long intervals. **Short, bursty** SYCL work (routing + chunked FF) often shows **1–5%** even when the device is doing real work. For a clearer signal, use **Intel Graphics Software** / **Intel PresentMon** / vendor metrics, and treat **`[TrainingTiming] route_ms`** plus logs for **multi-row SYCL** as ground truth.

**Train queue depth vs `collect_limit` (starvation → single-row batches)**  
In `autonomous_training_pipeline.cpp`, **`collect_limit` is capped by synthesizer `queue_depth / 2` (queued contrastive pairs)**. If the train queue is only **one pair** ahead, **`collect_limit` becomes 1** → each `process_batch` is **single-row** → the **multi-row SYCL FF path** (`batch_work.size() >= 2`) does not run → GPU stays quiet while CPU still orchestrates. Mitigations: keep **prefill**, **acq/raw/train** caps, and **acquisition_threads / perturbation_threads** high enough that the train queue stays **well ahead** of training; optionally set **`ff_multi_row_slots_chunk`** (e.g. **2048**) for larger device chunks when multi-row batches do form; raise **`target_routes_per_batch`** so **`route_budget_rows`** is less likely to cap **`collect_limit`** below what the queue could supply.

**Procedure**

1. Set **`training.timing_to_stderr = true`** (on in your tuned profile).
2. Run from repo root with verbose polling:  
   `powershell -NoProfile -File .\scripts\run_qminiwasm_training_verbose.ps1`  
   (add `-Smoke` only for quick path checks).
3. Watch stderr **`[TrainingTiming] total_ms=... collect_ms=... route_ms=... ff_train_ms=...`** each batch.
4. **Decision loop** (see plan mermaid):
   - **`collect_ms` high** → raise prefill/queue caps or producer threads; soften empty-backoff if oscillating.
   - **`route_ms` high** → confirm SYCL routing path; reduce route fan-out only if router is the proven bottleneck.
   - **`ff_train_ms` high** → shape or parallelism (smaller widths, `worker_threads`, OpenMP env); device offload is engineering beyond TOML.
   - **Periodic stalls** → align `checkpoint_interval`, `checkpoint_async_queue_max`, heartbeat/streaming cadence.
5. **External**: Task Manager GPU engine + `scripts/verify_training_cpu_pattern.ps1` for host/GPU correlation.

## 5) Before / after evidence template

| Metric | Baseline run | After this profile |
|--------|----------------|-------------------|
| Date / commit | | |
| `[TrainingTiming]` medians (collect / route / ff) | | |
| Queue depth stability (acq/raw/train) | | |
| GPU utilization floor (approx., Task Manager) | | |
| Notes | | |

## 6) Decision note

- **Config-only success** means `[TrainingTiming]` shows balanced buckets without chronic `collect_ms`, and stalls correlate with checkpoints only at expected intervals.
- **Remaining gap**: If `ff_train_ms` dominates and GPU stays low, the next engineering step is **additional SYCL / GPU kernels for GF(3) expert work**, not more TOML toggles alone.
