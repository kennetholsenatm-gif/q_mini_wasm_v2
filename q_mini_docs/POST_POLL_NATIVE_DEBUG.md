# Post-poll training (first `process_batch`) — debugging a silent exit

If prefill completes, `C.Training_StartTraining` returns `0`, and the first Go poll succeeds, the next work runs in the **native** `training_thread_` inside `q_training.dll` (see `AutonomousTrainingPipeline::training_loop` in `q_mini_wasm_v2/core/training/autonomous_training_pipeline.cpp`).

**Helpers:** `scripts/collect_training_crash_artifacts.ps1` (tail the crash log + phase cheat sheet), `scripts/verify_native_training_build.ps1` (exe + DLL beside repo root), `scripts/run_qminiwasm_training_verbose.ps1 -SmokePostPoll` (post-poll smoke TOML + `KMP_DUPLICATE_LIB_OK`). On Windows, `qminiwasm` sets `KMP_DUPLICATE_LIB_OK=TRUE` when unset to reduce duplicate OpenMP-runtime SIGABRT with SYCL/MKL.

## Confirm builds and crash hooks

On **`Training_InitSession`** you should see:

- Stdout: **`[Training] crash_hooks_installed=1`** — verifies this DLL includes vectored hooks that **do not** append MSVC C++ EH noise (`0xE06D7363`) to `%LOCALAPPDATA%\q_mini_training_crash.log`.
- Stderr: **`[q_training.dll] crash hooks: ... 0xE06D7363 is not logged`**
- After the first background batch, stderr:  
  **`[TrainingPipeline] training_loop: entering first process_batch()`** then  
  **`[TrainingPipeline] training_loop: first process_batch returned, samples_trained=...`**

If the process dies **after** the “entering” line and **before** the “returned” line, the fault is inside the first `process_batch()`.

## First-batch phase markers (stderr)

On the **first** `process_batch()` call only, the native pipeline prints sub-phase lines so the **last** line before a hard exit pinpoints the fault:

| Line | Meaning |
|------|---------|
| `[TrainingPipeline] process_batch: phase=enter` | Function entered (before micro-batch collect). |
| `phase=collect_done rows=N collect_limit=M` | Contrastive rows acquired; about to route/train. |
| `phase=first_route_done topk=K` | First row: MoE router returned `K` experts (after top-k cap). |
| `phase=first_row_ff_train_done route_experts=N` | First row: `TrainForwardForward` finished for `N` routed experts. |

If you see `enter` but not `collect_done`, the fault is in the **collect** loop (queues / timeout / synthesizer). If you see `collect_done` but not `first_route_done`, the fault is in **extract/route** for the first row. If you see `first_route_done` but not `first_row_ff_train_done`, the fault is in **lazy expert materialize** or **OpenMP** setup for that row. If you see `first_row_ff_train_done`, the fault is **after** the first FF train for that row (metrics, later rows, batch finalization).

These lines are independent of `training.timing_to_stderr` and `training.goodness_log_level`.

## Config binary search (no code)

1. **Symplectic routing is GPU-mandatory** (`moe_experts >= 8` and **`auto`/`on`**). Fix SYCL init or adjust TOML. Or use the checked-in profile  
   [`config/training_config.smoke_postpoll.toml`](../config/training_config.smoke_postpoll.toml) which also shrinks MoE and microbatch for a smaller first batch.

2. **Point the host at the smoke TOML** (absolute or under `DataDir/config`):

   `QMINI_TRAINING_CONFIG=training_config.smoke_postpoll.toml`

3. **Always** run `qminiwasm.exe` from the directory that contains the **`q_training.dll`** you built (same folder as repo root layout).

## Bisect after smoke (full config vs smoke)

Use [`config/training_config.smoke_postpoll.toml`](../config/training_config.smoke_postpoll.toml) first (`QMINI_TRAINING_CONFIG=training_config.smoke_postpoll.toml`). Then interpret:

| Result | Next step |
|--------|-----------|
| **Smoke OK, full config crashes** | In your full TOML, reduce `model.moe_experts` / `training.lazy_init_initial_experts`, or bisect with `training_config.smoke_postpoll.toml`. Use **phase markers** to see which sub-step fails. |
| **Smoke also crashes** | Same **crash log** + (optional) WER dump; confirm **PDB matches** the `q_training.dll` you load. Attach a debugger to the smoke run if the log is only `0xC0000005` without a symbol. |

## Crash log (hard faults only)

Open **`%LOCALAPPDATA%\q_mini_training_crash.log`** after a failure. Meaningful entries look like **`vectored SEH code=0xC0000005`** or **`std::terminate()`**. If this file explodes again with only `0xE06D7363`, you are loading an **old** DLL without the filter—replace `q_training.dll` and delete the log to start clean.

## WER minidumps (no debugger yet)

Run elevated:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\enable_wer_localdumps.ps1
```

Crash dumps land under **`%LOCALAPPDATA%\q_mini_wer_dumps`** by default (override with `-DumpFolder`). Inspect the `.dmp` in Visual Studio or WinDbg against **`q_training.dll`** PDBs from your Release SYCL build.
