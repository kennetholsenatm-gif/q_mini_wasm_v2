# WUI MCP Desktop Production Plan (Single-EXE Target)

## Scope

This document defines the production packaging and runtime plan for the WUI under the architectural constitution:

- MCP-only orchestration model (no ad-hoc REST/gRPC control plane)
- Desktop-first operator UX (non-mobile target)
- TOML-first authoritative configuration (`config/system.toml`)
- No simulated metrics in production control path

## Current Remediation Status

Completed in this remediation pass:

1. WUI transport migrated from `websocket-bridge.js` include to `mcp-host-bridge.js` for dashboard/config pages.
2. Dashboard behaviors hardened to strict mode:
   - no random measurement values
   - no offline synthetic FF progress loops
   - explicit backend-required error states
3. Config page migrated to host-backed TOML roundtrip:
   - load via `wui_get_system_toml`
   - save via `wui_set_system_toml`
4. `wui-cli-bridge` MCP server updated:
   - exposes TOML read/write tools
   - blocks passthrough-only controls in strict mode when no compiled backend is attached
   - routes training metrics to CGO pipeline when available
   - exposes system self-check + release lifecycle tools (`wui_build_release`, `wui_deploy_release`, `wui_rollback_release`, `wui_get_release_status`, `wui_get_ops_snapshot`)
5. Simulated pipeline update loop removed from active WUI control path (`pipeline_handlers.go` synthetic ticker/metrics removed).
6. Active WUI pages hardened for desktop operator workflow:
   - `index.html` now includes explicit operator readiness + release-ops panels
   - `config.html` now enforces validate-before-save TOML flow with backup visibility
7. Bridge runtime build path hardened to locate project root and execute release commands from module root safely.
8. MCP automation profile updated to use `mcp-host-bridge.js` resource and bridge-config terminology (legacy websocket resource references removed).
9. Legacy `websocket-bridge.js` converted to explicit deprecation tombstone that fails fast if loaded accidentally.
10. Desktop shell contract + bootstrap are now generated in-repo at runtime startup:
    - `runtime/desktop_shell_contract.json`
    - `runtime/desktop_shell_bootstrap.js`
11. `wui-cli-bridge` now exposes `wui_desktop_shell_handshake` and publishes deterministic host contract metadata:
    - required method: `window.qMiniMcpHost.callTool(name, arguments)`
    - request/response channels: `qmini-mcp-request` / `qmini-mcp-response`
12. Single-EXE packaging path now appends a deterministic embedded WUI asset bundle to `qminiwasm_wui.exe` with:
    - footer marker + bundle size
    - bundle SHA-256 verification
    - generated manifest (`runtime/embedded_asset_manifest.json`)
    - extraction + verification tooling in build/deploy/handshake flows
    - embedded native runtime artifact enforcement (`require_native_runtime=true` default)
13. Production bundle now excludes legacy fallback transport assets:
    - `wui/js/websocket-bridge.js`
    - `wui/js/wui-mcp-bridge.js`

## Single-EXE Packaging Blueprint

Target artifact:

- `qminiwasm_wui.exe`

Contained components:

1. **Go MCP Host Runtime**
   - process bootstrap
   - MCP tool routing
   - lifecycle management

2. **Embedded WUI Static Assets**
   - `wui/index.html`, `wui/config.html`, docs/help pages
   - `wui/js/mcp-host-bridge.js`

3. **Engine Integration Layer**
   - CGO-backed calls into C++ runtime (DLL/toolchain dependent)
   - strict startup checks for engine availability

4. **Configuration Service**
   - authoritative `config/system.toml` load/save path
   - backup-on-write semantics (`system.toml.bak`)

Current implementation note:

- Build/deploy lifecycle MCP tools are implemented in `agents/cmd/wui-cli-bridge/main.go` and can emit `qminiwasm_wui.exe` from the bridge module context.
- Release status and readiness checks are surfaced into the dashboard via `wui/js/mcp-host-bridge.js` events.

## Runtime Communication Model

Production communication path:

`WUI (embedded) -> in-process MCP host adapter -> MCP tool handlers -> CGO/C++ engine`

Notes:

- WebSocket/HTTP are not required for core production operation.
- Optional debug adapters may exist in dev profile only, disabled in production profile.

## Desktop UX Constraints (Cognitive Ergonomics)

Design constraints enforced:

1. Desktop-first layout widths (`min-width` enforced on key pages).
2. Operator workflows prefer explicit state transitions over background animation/simulation.
3. Error visibility prioritized (strict mode logs actionable backend requirements).
4. Config editing is host-authoritative, preventing hidden drift from hardcoded templates.

## Remaining Work (Next Execution Phase)

1. Integrate signed resource extraction or equivalent deterministic delivery for native runtime dependencies when dynamic linking is used.
2. Add E2E harness validating:
   - training lifecycle (init/start/pause/resume/stop)
   - metrics live updates
   - TOML load/save roundtrip with backup verification
3. Add CI assertions that fail release if embedded bundle footer/manifest/hash verification fails.
4. Add CI assertion that `embedded_native_runtime_assets > 0` for production release profile.

## Verification Snapshot (This Pass)

Executed checks:

1. `cd agents/cmd/wui-cli-bridge && go test ./...` ✅
2. `cd agents/cmd/wui-cli-bridge && go build .` ✅
3. `node --check wui/js/mcp-host-bridge.js` ✅
4. Inline script syntax checks for `wui/index.html` and `wui/config.html` via temp JS extraction + `node --check` ✅
5. MCP automation server metadata scan (`.mcp-servers/qminiwasm-wui-automation.json`) for legacy websocket resource/config references ✅
6. JSON-RPC packaging validation (`go run .`) invoking:
   - `wui_build_release`
   - `wui_desktop_shell_handshake` (`extract_assets=true`)
   - `wui_get_release_status` ✅

Observed packaging evidence from JSON-RPC validation:

- Artifact: `releases/desktop/qminiwasm_wui_test.exe`
- Artifact SHA-256: `06aeee40d462a1c55556cf8bb67dd13bdf04320a59cc6c26ffb0cd24d19c11b3`
- Embedded asset bundle SHA-256: `f285bffbfeb3b9f825a8d39db6b425fa6dcf73236635781ca63cd6bd34aa7c99`
- Embedded asset bytes: `7404799`
- Embedded manifest extracted and validated (`asset_count=12`) ✅
- Embedded native runtime assets: `1` (`runtime/native/q_mini_wasm_v2.exe`) ✅
- `ready_for_operator_pipeline=false` in this environment because runtime execution is unavailable (`CGO_ENABLED=0`, no backend passthrough) ⚠️

CGO/runtime execution gate validation:

- `wui_build_release(require_cgo=true)` fails fast when local C toolchain is absent (`gcc not found`) ✅
- `wui_build_release(require_cgo=false)` still produces deterministic operator bundle for packaging QA ✅

Functional checks completed by code inspection + event wiring:

- Dashboard receives and renders:
  - `systemSelfCheck`
  - `desktopShellHandshake`
  - `releaseStatusUpdate`
  - `opsSnapshotUpdate`
- Config page receives and renders:
  - `systemTomlLoaded`
  - `systemTomlValidated`
  - `systemTomlSaved`
  - `systemSelfCheck`

## Acceptance Criteria for Production Sign-Off

1. No synthetic/random operational metrics in production control pages.
2. All WUI command paths execute via MCP host bridge.
3. `config/system.toml` is the single source of truth for WUI config operations.
4. Build produces a single distributable executable artifact with documented startup behavior.
5. Constitutional constraints are preserved (MCP paradigm, FF learning semantics, GF(3) core boundaries).

Status against criteria in this pass:

- (1) ✅ active control pages avoid synthetic/random runtime success states
- (2) ✅ active control pages execute via MCP host bridge
- (3) ✅ authoritative config editing path is TOML-first
- (4) ✅ single-executable build now embeds deterministic WUI assets and exposes desktop shell handshake contract
- (5) ✅ no constitutional transport regressions introduced
