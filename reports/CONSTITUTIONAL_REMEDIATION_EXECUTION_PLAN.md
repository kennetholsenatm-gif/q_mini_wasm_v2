# Constitutional Remediation Execution Plan

Generated: 2026-04-06

## 1) Current Enforcement Snapshot (Evidence)

### `scripts/enforce_architecture.py --json reports/architecture_enforcement_report.json`
- Scanned files: **241**
- Violations: **274**
- Rule breakdown:
  - `FLOAT_CONTAMINATION_STRICT_CORE`: **252**
  - `NON_MCP_TRANSPORT`: **22**

### Top violation hotspots
1. `q_mini_wasm_v2/core/qgnn/error_correction.hpp` (62)
2. `q_mini_wasm_v2/core/qgnn/error_correction.cpp` (55)
3. `q_mini_wasm_v2/core/qgnn/zx_calculus.cpp` (37)
4. `q_mini_wasm_v2/core/qgnn/ternary_tree.cpp` (31)
5. `q_mini_wasm_v2/core/qgnn/ternary_tree.hpp` (30)
6. `agents/cmd/wui-server/main.go` (15)

Reference artifacts:
- `reports/architecture_enforcement_report.json`
- `reports/architecture_hotspots.tsv`
- `reports/gf3_integrity_latest.txt`
- `reports/language_inventory.tsv`
- `reports/runtime_python_surface.tsv`

### Repository inventory snapshot (git-tracked)
- Total tracked files: **579**
- Language/file mix:
  - Python: **69**
  - Go: **56**
  - C++ source (`.cpp`): **70**
  - C++ headers (`.hpp`): **50**
  - Markdown docs: **107**
- Runtime-surface Python files (agents/core/runtime/go scopes): **16**
  - See `reports/runtime_python_surface.tsv` for full path list.

---

## 2) Structural Findings

### A. MCP transport non-compliance (P0)
- WebSocket/HTTP server surfaces in:
  - `agents/cmd/wui-server/main.go`
  - `agents/cmd/wui-cli-bridge/main.go`
  - `q_mini_wasm_v2/go/pkg/rag/embedding.go` (`net/http`)

### B. Core GF(3) float contamination (P0)
- Contamination concentrated in `core/qgnn/*` subsystems:
  - `error_correction.*`
  - `zx_calculus.*`
  - `ternary_tree.*`
  - `graph_migration_adapter.*`
  - supporting declarations (`graph_*`, `betti_extractor.cpp`, etc.)

### C. Runtime→SYCL integration gap (P1)
- Documented in: `docs/architecture/runtime-to-sycl-traceability.md`
- `RuntimeOrchestrator` currently executes CPU task lambdas; direct production wiring to `sycl/tableau_kernels.*` is weak.
- `dll/wasm_bridge/wasm_api.cpp` has extensive CPU-fallback paths and explicit future-SYCL placeholders.

### D. Runtime language policy mismatch (P0)
- Requirement states runtime code must be Go/C++ with no Python runtime.
- Current tracked runtime-surface Python footprint is non-zero (**16 files**) under `agents/`.
- These include active agent modules and tests (`agents/base_agent.py`, `agents/research_agent.py`, `agents/improvement_cycle.py`, etc.).

### E. Stub/placeholder/prototype/testing gaps (P1)
- `agents/research_agent.py` retains placeholder structures for git/external research paths.
- `agents/improvement_cycle.py` contains placeholder return paths for implementation/validation phases.
- `q_mini_wasm_v2/core/agents/mcp_multiplexer.cpp` returns explicit “not implemented” responses for unresolved methods.
- `q_mini_wasm_v2/dll/wasm_bridge/wasm_api.cpp` includes explicit future-SYCL and CPU-fallback notes in critical command paths.
- `docs/research/re-audit-report-april-2026.md` currently overstates readiness (`production-ready`) versus measured violations.

---

## 3) Prioritized Remediation Phases

## Phase 0 — Guardrails (completed)
- Upgraded architecture scanner to structured rule engine with:
  - scoped checks (`core_strict`, `transport_surface`, `execution_surface`)
  - comment-aware scanning
  - JSON output (`--json`)
- Fixed indentation/control-flow bug in `scripts/validate_gf3_integrity.py` critical path loop.
- Added architecture artifacts upload + GF(3) integrity stage in workflow:
  - `.github/workflows/architecture-enforcement.yml`

## Phase 1 — Eliminate NON_MCP transport violations (P0)
1. Replace `agents/cmd/wui-server` HTTP/WebSocket with MCP stdio client or isolate as out-of-scope peripheral package excluded from production workflow.
2. Replace `agents/cmd/wui-cli-bridge` WebSocket flow with MCP-native calls.
3. Refactor `go/pkg/rag/embedding.go` external HTTP embedding dependency behind MCP-hosted capability (or disable in strict profile).

**Exit criteria:** `NON_MCP_TRANSPORT = 0` in `architecture_enforcement_report.json`.

## Phase 1.5 — Remove Python from runtime surfaces (P0)
1. Migrate `agents/*.py` runtime paths to Go/C++ MCP clients/handlers.
2. Keep Python only for non-runtime tooling (if permitted) or remove entirely under strict profile.
3. Add CI gate failing on Python files inside runtime-surface scopes.

**Exit criteria:** `runtime_python_surface.tsv` has header only (zero runtime Python files).

## Phase 2 — GF(3) strict core migration for QGNN (P0)
1. `core/qgnn/error_correction.*`: convert floating metrics/types to ternary/fixed-point representations at strict boundaries.
2. `core/qgnn/zx_calculus.*`: remove `double`/`std::complex<double>` usage from strict paths (or move to explicitly permitted non-core boundary module).
3. `core/qgnn/ternary_tree.*` + `graph_migration_adapter.*`: migrate runtime metrics and ratios from floating point to fixed-point integers.
4. `core/ternary/trit.hpp`: resolve energy conversion API to non-floating strict representation in core.

**Exit criteria:** `FLOAT_CONTAMINATION_STRICT_CORE = 0` in `architecture_enforcement_report.json`.

## Phase 3 — Runtime/SYCL execution hardening (P1)
1. Add explicit orchestrator dispatch adapters for SYCL-backed operations where available.
2. Replace WASM bridge CPU fallback placeholders with real SYCL command paths or strict feature gates.
3. Add coverage proving runtime invokes SYCL path in non-test execution.

**Exit criteria:** Traceability matrix entries move from “partial/weak” to “implemented/validated”.

---

## 4) Operational Controls

- Keep running on every PR:
  - `python scripts/enforce_architecture.py --json reports/architecture_enforcement_report.json`
  - `python scripts/validate_gf3_integrity.py`
- Treat scanner output as a merge gate; no “warning-only” mode for P0 rules.

---

## 5) Risks & Blockers

1. **Large QGNN float surface area** (252 findings) implies multi-file API churn.
2. **Peripheral WUI stack** is architecture-incompatible under strict MCP-only policy unless isolated from production pipeline.
3. **Metric/telemetry APIs** in strict core currently assume floating math; may require dedicated non-core metrics boundary.

---

## 6) Definition of Done

Repository reaches constitutional baseline when:
- `NON_MCP_TRANSPORT == 0`
- `FLOAT_CONTAMINATION_STRICT_CORE == 0`
- Runtime surfaces contain **no Python** implementation files
- Runtime→SYCL traceability document reflects validated production path (not test-only coverage)
- CI architecture workflow passes on PRs without suppression exceptions.
