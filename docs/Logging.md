# Logging Standard

This document describes logging conventions for the Q-Mini-WASM codebase so that critical paths are observable and failures are not silent.

## Conventions

- **Module-level logger:** Use `logger = logging.getLogger(__name__)` in each module. Do not swallow exceptions in critical paths without logging.
- **Levels:**
  - **ERROR:** Operation failed; caller or user should be aware (e.g. WASM load failure, hybrid inference failure, QAOA execution failure).
  - **WARNING:** Recoverable or fallback path taken (e.g. quantum backend init failed, using simulator; run_forward fallback).
  - **INFO:** Normal progress (e.g. "Completed hybrid inference", "Initialized SYCLHardware stubs").
  - **DEBUG:** Detailed or high-volume data (e.g. packed trit counts, per-sample details).
- **Structured context (optional):** For inference or request-scoped code, consider adding a correlation ID or request ID to log records (e.g. via `logging.LoggerAdapter` or context vars) so that logs can be traced across edge → cloud.
- **Exceptions:** Prefer `logger.error("message: %s", e, exc_info=True)` when re-raising so that stack traces are captured; use `exc_info=False` for expected fallbacks to avoid noise.

## Critical paths

- **Edge cognitive loop:** Outcome (resolved vs escalate) and loop count are logged in `qminiwasm/cognitive/edge.py`.
- **Hybrid inference:** Start/failure and completion are logged in `model.py`.
- **Quantum backend / QAOA:** Init failures and QAOA execution failures are logged in `qminiwasm/fabric/router.py` and `qminiwasm/fabric/backend_registry.py`.
- **WASM load/compile/execute:** Failures are logged in `qminiwasm/wasm_host/engine.py`; re-raise after log where appropriate.
- **Data pipeline:** Per-algorithm generation failures are logged in `data/pipeline.py`; the batch continues.

## What not to do

- Do not use `except Exception: pass` in critical paths without at least a warning log.
- Do not log secrets (tokens, keys, or full payloads) at INFO or DEBUG.
