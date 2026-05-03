# Training TOML: when `0` is valid

Several `[training]` / `[model]` integers use **0** to mean *derive*, *disable a cap*, or *use a pipeline default*. `qminiwasm` validation allows these where the native `q_training.dll` and `resolveEffectiveTrainingParams` agree.

| TOML key | `0` means |
|----------|-----------|
| `training.prefill_target_samples` | **Native:** ring-buffer warmup waits until at least `max(1, value)` contrastive pairs exist; **`0` ⇒ first pair only** (minimal delay before `training_loop`; larger values prime more throughput headroom before the first FF batch). |
| `training.micro_batch_cap` | No extra cap: per-batch collect limit uses `batch_size` (see `training_micro_batch_cap_for` in the native pipeline). |
| `training.collect_floor` | Does not further lower the collect limit; effective limit is `min(batch_size, micro_batch_cap)` (whichever are non-zero). |
| `training.lazy_init_initial_experts` | **Only when `features.lazy_init = true`:** derive initial resident experts from `model.moe_top_k` (same as omitting the key). If `lazy_init` is false, this key must equal `model.moe_experts` (not 0). |
| `training.target_routes_per_batch` | **Native (`q_training.dll`):** `0` means derive `max(1, adaptive_collect_limit × effective_top_k)` (see `effective_target_routes_per_batch`). **Go** (`resolveEffectiveTrainingParams`) uses `batch_size × moe_top_k × parallel_batches` when the key is unset for WUI messaging; align with native by storing `0` or omitting depending on which path owns the derivation for your workflow. See `TRAINING_THROUGHPUT.md`. |
| `training.collect_min_rows_per_batch` | **Native:** `0` means use the full `collect_limit` for the minimum rows before the collect timer can flush (see `process_batch`). |
| `training.ff_expert_chunk_size` | One FF progress chunk per route (no sub-chunking). |
| `training.sycl_prereserve_gib` | No SYCL device pre-reserve. |
| `training.sycl_prereserve_chunk_mib` | Use pipeline / generated default chunk size when pre-reserve runs. |
| `training.sycl_gpu_device_index` | **-1** in TOML is typical for “default device”; not the same as 0. |
| `training.gf3_sycl_min_weight_cells` | Native default / off (see InitSession logs). |
| `training.gf3_ff_layers_per_batch` | All expert FF layers in one SYCL layer batch. |
| `training.ff_multi_row_slots_chunk` | Derive slot cap from `gf3_ff_batched_weight_mib` and host budget. |
| `training.gf3_ff_multislot_slots_chunk_max` | No extra ceiling on multi-row chunk slots (see pipeline stderr chunk diagnostics). |
| `training.lazy_moe_resident_cap` | Derive resident cap from other MoE knobs (see stderr pipeline banner). |
| `model.moe_ff_active_internal_layers` | Use full `model.expert_internal_layers` (see `resolveEffectiveTrainingParams`). |

## Not valid as `0`

- **`training.sycl_trit_quant_min_moe_dim`** — must be **≥ 1** (ABI field; `q_training.dll` rejects 0).
- **Queue / thread sizes** (`acq_queue_cap`, `raw_queue_cap`, `train_queue_cap`, `acquisition_threads`, etc.) — must be **≥ 1** in Go validation.
- **`training.epochs`**, **`training.batch_size`**, **`training.samples_per_epoch`**, **`model.*` dimensions** — must be positive where validated.

For behavior of native-only knobs (GF3 MiB, multislot flags), see stderr lines from `Training_InitSession` and `TRAINING_THROUGHPUT.md`.
