# Recursive Constitutional Audit Report (2026-04-07)

## Scope

Recursive audit across `q_mini_wasm_v2`, `agents`, `docs`, and `reports`,
cross-checked against `.clinerules` constitutional constraints and top-tier
research intent documents.

Primary references reviewed:
- `docs/research/Quantum Betti Numbers Integration Analysis.md`
- `docs/research/Autonomous Forward-Forward Training Plan.md`
- `docs/architecture/runtime-to-sycl-traceability.md`
- `scripts/enforce_architecture.py`

## Repository Snapshot (Evidence)

- Tracked files: **579**
- Language mix:
  - Python: **69**
  - Go: **56**
  - C++ (`.cpp`): **70**
  - C++ (`.hpp`): **50**
  - Markdown: **107**
- Runtime-surface Python footprint (`agents/`, `core/`, `runtime/`, `go/`):
  **16 files**

## Key Findings

### 1) Enforcement gate currently reports false confidence

`python scripts/enforce_architecture.py --json ...` reports **0 violations**,
but code inspection shows unresolved constitutional risk in strict-core and
execution surfaces.

Observed weaknesses in `scripts/enforce_architecture.py`:
- Float rule matches only a narrow token set (`float|double|f32|f64`).
- It does **not** catch many continuous-math patterns used in strict-core,
  including decimal literals and operations like `std::exp`, `std::arg`,
  `std::fmod`, or `M_PI` usage.

### 2) Strict-core GF(3) purity drift remains in QGNN subsystem

High-risk files:
- `q_mini_wasm_v2/core/qgnn/error_correction.cpp`
- `q_mini_wasm_v2/core/qgnn/error_correction.hpp`
- `q_mini_wasm_v2/core/qgnn/zx_calculus.cpp`
- `q_mini_wasm_v2/core/qgnn/zx_calculus.hpp`
- `q_mini_wasm_v2/core/qgnn/ternary_tree.cpp`
- `q_mini_wasm_v2/core/qgnn/ternary_tree.hpp`

Examples observed:
- Mixed fixed-point intent with continuous semantics and decimal constants.
- `std::complex<...>` phase logic and `std::exp/std::arg/std::fmod` usage.
- API/type signatures still expose boolean-heavy control surfaces in strict
  mathematical pathways that are intended to be ternary/native-discrete.

### 3) Runtime→SYCL traceability is still partial

Evidence:
- `q_mini_wasm_v2/runtime/orchestrator.cpp`
  - `submit_tableau_update`, `submit_moe_routing`, `submit_ff_training`
    enqueue CPU lambdas; no direct production SYCL dispatch wiring.
- `q_mini_wasm_v2/dll/wasm_bridge/wasm_api.cpp`
  - Multiple explicit **CPU fallback** and **Future: Implement proper SYCL**
    placeholders.
- `q_mini_wasm_v2/sycl/tableau_kernels.cpp`
  - Routing logits path still uses `std::vector<double>` and floating logic.

### 4) Communication model mismatch remains in training ingestion path

`q_mini_wasm_v2/core/training/data_synthesizer.cpp` contains direct HTTP/socket
client behavior (`libcurl` path + socket headers + API calls). Under strict
constitutional interpretation (MCP-only communications), this remains an
architectural mismatch unless explicitly isolated as non-production/
non-constitutional surface.

### 5) Python runtime surface is non-zero (policy mismatch)

Runtime-surface Python files include:
- `agents/base_agent.py`
- `agents/research_agent.py`
- `agents/improvement_cycle.py`
- `agents/rag_client.py`
- and related modules/tests (total 16)

If runtime execution policy requires Go/C++ only, this remains open.

### 6) Stub/placeholder and integration completeness gaps

- `q_mini_wasm_v2/core/agents/mcp_multiplexer.cpp`
  - unimplemented method fallback path and empty native handler bootstrap.
- `q_mini_wasm_v2/dll/wasm_bridge/wasm_api.cpp`
  - explicit future-SYCL placeholders in command paths.

### 7) Documentation drift and corruption

- `docs/README.md` and `reports/implementation_status_matrix.md` present
  “complete/validated” narratives that conflict with observed implementation
  evidence.
- Documentation contamination detected:
  `docs/guides/building.md`, `docs/architecture/moe-routing.md`,
  `docs/architecture/forward-forward.md` contain embedded artifact tags such as
  `<write_to_file>`, `<path>`, `</content>`.

## Priority Remediation Plan

### P0 — Re-establish trustworthy enforcement
1. Expand architecture scanner rules to detect continuous-math proxies:
   decimal literals in strict-core expressions, `std::exp`, `std::arg`,
   `std::fmod`, `M_PI`, and complex-number phase APIs.
2. Add explicit scanner tests with known bad fixtures from current QGNN files.
3. Fail CI on scanner regressions.

### P0 — Strict-core QGNN constitutional cleanup
1. Refactor `error_correction.*`, `zx_calculus.*`, `ternary_tree.*` to
   integer/fixed-point + GF(3)-native operations only.
2. Remove/replace continuous complex-phase routines in strict-core pathways.
3. Convert remaining bool/continuous control semantics in strict loops to
   ternary/fixed discrete representations where constitutionally required.

### P0 — Clarify and enforce communication boundaries
1. For `core/training/data_synthesizer.cpp`, either:
   - migrate API acquisition behind MCP host capabilities, or
   - isolate file as non-production experimental surface with CI exclusion and
     documented constitutional waiver.
2. Keep transport surfaces MCP-only for agents/gateway paths.

### P1 — Runtime→SYCL completion
1. Add production SYCL dispatch adapters in `runtime/orchestrator.*`.
2. Replace `wasm_api.cpp` fallback placeholders with real kernel execution or
   explicit strict feature gating.
3. Eliminate floating routing logits in SYCL kernel interfaces.

### P1 — Runtime language policy closure
1. Migrate runtime-relevant Python agent paths to Go/C++ MCP clients.
2. Add gate that blocks Python files in runtime scopes when strict mode is on.

### P2 — Documentation integrity
1. Remove embedded artifact tags from docs.
2. Reconcile “production-ready/complete” claims with measured compliance.
3. Link each architecture claim to executable evidence (test/probe/report).

## Bottom Line

The repository shows meaningful progress but is **not yet constitutionally
closed** under strict interpretation. The largest risks are (1) strict-core
QGNN semantic drift, (2) incomplete runtime→SYCL production wiring,
(3) scanner blind spots producing false green CI signals, and
(4) policy/documentation drift around runtime Python and communication model.
