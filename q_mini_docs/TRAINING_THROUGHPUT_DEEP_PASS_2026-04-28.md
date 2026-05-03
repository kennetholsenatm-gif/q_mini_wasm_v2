# Training Throughput Deep Pass (2026-04-28)

## 1) Active runtime profile verification

Active runtime config path is `C:\q_mini_data\config\training_config.toml`.

This matches observed run telemetry:
- `collect_effective=4` from `training.collect_floor=4`
- `route_topk=256` from `model.moe_top_k=256`
- `prefill_tgt=16384` from `training.prefill_target_samples=16384`
- `queue_hint=65536` from `training.train_queue_cap=65536`

Conclusion: the run was not using repo-local `config/training_config.toml`; it was using the `C:\q_mini_data` runtime tree.

## 2) Baseline timing summary (from captured run logs)

Observed `TrainingTiming` samples (same run profile):

- `total_ms=244132 collect_ms=0 route_ms=1 ff_train_ms=2614628 routes=1024 rows=4 collect_limit=4 top_k_cap=256`
- `total_ms=196377 collect_ms=0 route_ms=0 ff_train_ms=2321880 routes=1024 rows=4 collect_limit=4 top_k_cap=256`
- `total_ms=200743 collect_ms=0 route_ms=0 ff_train_ms=2374743 routes=1024 rows=4 collect_limit=4 top_k_cap=256`

Quick baseline (n=3):
- `median total_ms ~= 200743`
- `median ff_train_ms ~= 2374743`
- `median collect_ms = 0`
- `median route_ms = 0`

Interpretation:
- Throughput is FF-train bound.
- Data collection and routing are not the primary bottlenecks in this window.
- Sample progress appears slow because each batch completion is expensive and rows per batch are low.

## 3) Throughput tuning applied

Updated `C:\q_mini_data\config\training_config.toml`:

- `training.collect_floor: 4 -> 16`
- `model.moe_top_k: 256 -> 64`
- `model.moe_hidden_dim: 8192 -> 4096`
- `model.expert_internal_layers: 3 -> 2`
- `model.moe_ff_active_internal_layers: 0 -> 2`

Why this mix:
- Previous route work per batch was `4 * 256 = 1024` routes.
- New target is `16 * 64 = 1024` routes, keeping rough route count similar while improving rows-per-batch cadence and reducing per-route FF cost.
- Reduced hidden size and active internal depth cut FF compute further, where timing shows the real bottleneck.

## 4) Parallelism / backend validation findings

Code path confirms:
- OpenMP is wired by default via `QMINI_TRAINING_OPENMP` in `q_mini_wasm_v2/CMakeLists.txt`.
- Per-route expert training parallelizes via OpenMP when `top_k > 1` (when the training build enables OpenMP).
- SYCL routing metrics are surfaced via `router_sycl_path_used` and route-mode logging.

Run evidence from captured logs:
- `route_t5=1` with `router_sycl=0` indicates packed trit route path was used but SYCL routing path was not active in those windows.

Implication:
- Even with `sycl_route_mode="on"`, FF remains dominant and mostly CPU-bound in current architecture.

## 5) What to verify on next run

After restarting training with the updated runtime TOML, verify:

1. `collect_effective` moves to 16.
2. `route_topk` moves to 64.
3. `TrainingTiming` medians improve (especially `total_ms` and `ff_train_ms`).
4. `samples` grows in larger steps and higher `samples/hour`.

Use this command:

```powershell
powershell -NoProfile -File .\scripts\run_qminiwasm_training_verbose.ps1
```

Then compare at least 5 completed `TrainingTiming` lines against this baseline.

## 6) Decision checkpoint

If throughput remains unsatisfactory after this config pass:
- Move to profiling-guided code optimization in `TrainForwardForward` hot loops.
- Prioritize vectorization / kernelization only where measurements confirm hotspot dominance.
