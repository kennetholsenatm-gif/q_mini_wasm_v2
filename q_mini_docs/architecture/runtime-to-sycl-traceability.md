# Runtime → SYCL/XPU Traceability

## Purpose

This document maps the executable path from runtime task orchestration to SYCL-enabled acceleration surfaces and highlights current implementation gaps. It is intended as an auditable bridge between architectural intent and implementation reality.

## End-to-End Flow

1. **Runtime task submission**
   - File: `q_mini_wasm_v2/runtime/orchestrator.hpp`
   - Entry points:
     - `submit_tableau_update(...)`
     - `submit_moe_routing(...)`
     - `submit_ff_training(...)`
2. **Task execution loop**
   - File: `q_mini_wasm_v2/runtime/orchestrator.cpp`
   - Worker threads pop lambda tasks from `task_queue_` and execute on CPU.
3. **Core subsystem invocation**
   - Stabilizer: `q_mini_wasm_v2/core/stabilizer/*`
   - MoE: `q_mini_wasm_v2/core/moe/*`
   - Forward-Forward: `q_mini_wasm_v2/core/learning/*`
4. **SYCL acceleration surfaces (declared)**
   - `q_mini_wasm_v2/sycl/tableau_kernels.hpp/.cpp`
   - `q_mini_wasm_v2/core/qgnn/message_passing_sycl.hpp/.cpp`
5. **Build-time SYCL enablement**
   - File: `q_mini_wasm_v2/CMakeLists.txt`
   - Gate: native CMake must find a SYCL toolchain (**icpx** / AdaptiveCpp / LLVM `-fsycl`)
   - Providers: IntelSYCL / AdaptiveCpp / oneAPI env fallback
6. **WASM bridge interface**
   - File: `q_mini_wasm_v2/dll/wasm_bridge/wasm_api.cpp`
   - Exposes SYCL init/map/dispatch calls with extensive CPU fallback behavior.

## Traceability Matrix

| Stage | Artifact | Current State | Evidence |
|---|---|---|---|
| Runtime orchestration | `runtime/orchestrator.*` | Implemented CPU thread pool | Task queue and worker loop are active in `orchestrator.cpp` |
| SYCL kernel declarations | `sycl/tableau_kernels.*` | Implemented but mostly isolated | Primarily referenced by `tests/test_sycl_kernels.cpp` |
| QGNN SYCL kernels | `core/qgnn/message_passing_sycl.*` | Implemented | Dedicated SYCL queue and kernels present |
| Build integration | `CMakeLists.txt` | Implemented | Mandatory SYCL + IntelSYCL shim / AdaptiveCpp / LLVM `-fsycl` detection |
| Runtime→SYCL wiring | Orchestrator to SYCL calls | Partial / weak coupling | `submit_*` methods in `runtime/orchestrator.cpp` call CPU lambdas/core APIs directly; no direct dispatch to `sycl_kernels::parallel_apply_*` |
| WASM SYCL bridge | `dll/wasm_bridge/wasm_api.cpp` | Partial with CPU fallback dominance | Multiple explicit "CPU fallback" and future SYCL TODO-style notes |

## Verified Gaps

### 1) Runtime-to-SYCL coupling gap
- `RuntimeOrchestrator` does not directly dispatch to `sycl_kernels::parallel_apply_*` paths.
- Existing SYCL kernels are validated mostly by tests and standalone modules.

### 2) Bridge execution gap
- `wasm_api.cpp` contains many paths where SYCL is initialized but execution still uses CPU fallback logic.
- Several command handlers include explicit placeholders for future SYCL kernel dispatch.

### 3) Documentation drift
- Some architecture docs overstate purity/completeness versus measured enforcement output.
- This traceability file is the canonical reconciliation point for runtime/SYCL execution path status.

## Code-Verified Evidence Snapshot (Apr 2026)

- `runtime/orchestrator.cpp`
  - `submit_tableau_update`, `submit_moe_routing`, and `submit_ff_training` enqueue CPU lambdas; no direct invocation of `sycl_kernels::*` APIs in the orchestrator path.
- `sycl/tableau_kernels.cpp`
  - File includes explicit section header: `Parallel Tableau Operations (CPU fallback without SYCL)`.
  - Routing helper currently uses floating point (`std::vector<double>` logits), which conflicts with strict GF(3)-only core purity expectations.
- `dll/wasm_bridge/wasm_api.cpp`
  - Contains explicit fallback notes (`using CPU fallback`, `Future: Implement proper SYCL kernel dispatch`) in command execution paths.

## Acceptance Criteria for “Code → XPU SYCL Traceability Complete”

1. Runtime orchestrator task types map to concrete SYCL dispatch adapters in production code paths (not only tests).
2. Bridge command handlers execute SYCL kernels where feature flags indicate SYCL availability; fallback paths are explicit and intentionally gated.
3. Strict GF(3) constrained modules avoid floating-point routing/attention logic.
4. CI evidence links each traceability row to executable validation (tests and/or runtime probes).

## Compliance Notes

- All recommended remediation must preserve:
  - GF(3)-only stabilizer loop semantics in core constrained paths
  - Forward-Forward only learning updates (no global gradient methods)
  - MCP-only communication for agent/gateway pathways

## Suggested Remediation Order

1. Wire orchestrator task types to explicit SYCL dispatch adapters where available.
2. Replace CPU-fallback placeholders in `wasm_api.cpp` command paths with real SYCL kernels or mark as intentionally disabled behind strict feature flags.
3. Keep this matrix updated with each integration milestone and CI evidence.
