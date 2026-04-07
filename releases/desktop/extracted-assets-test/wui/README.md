# q_mini_wasm_v2 WUI (Desktop MCP Surface)

## Purpose

The `wui/` pages are a desktop-first operator surface for q_mini_wasm_v2.  
All control paths are MCP-host mediated (via `wui/js/mcp-host-bridge.js`) and
are intended to work with the Go MCP bridge in `agents/cmd/wui-cli-bridge`.

## Current Production Pages

- `index.html`
  - runtime telemetry (metrics / training / Betti)
  - strict-mode-aware quantum controls
  - operator readiness/self-check panel
  - desktop release operations (build / deploy / rollback / status)
- `config.html`
  - authoritative `config/system.toml` editor
  - MCP-host validation + save with backup (`.bak`) support
  - self-check summary and section-governance visibility
- `wiki.html`, `glossary.html`, `api-docs.html`
  - reference/support pages

## Transport and Governance Rules

1. **MCP-only runtime communication**
   - UI pages load `js/mcp-host-bridge.js`.
   - Legacy `websocket-bridge.js` / `wui-mcp-bridge.js` are not loaded in active
     control pages.
   - `websocket-bridge.js` is retained as a deprecation tombstone that fails fast
     if loaded accidentally.
   - MCP automation profile resources now point to `mcp-host-bridge.js`.

2. **Strict-mode behavior**
   - UI does not fabricate synthetic runtime success states.
   - Operations requiring backend passthrough fail with explicit operator errors.

3. **TOML-first authority**
   - `config/system.toml` is edited/read through host tools only:
     - `wui_get_system_toml`
     - `wui_validate_system_toml`
     - `wui_set_system_toml`

## Local Validation Checklist

From repository root:

```bash
# validate MCP bridge JS syntax
node --check wui/js/mcp-host-bridge.js

# validate Go bridge build/test surface
cd agents/cmd/wui-cli-bridge
go test ./...
```

For HTML inline script syntax checks, run the PowerShell helper used in
remediation notes (see report updates in `reports/`).

## Notes

- This WUI is optimized for desktop operators (non-mobile target).
- Constitutional constraints still apply end-to-end (MCP paradigm, FF-only
  learning pathway, GF(3)-native core boundaries).
