# Tooling and ops backlog (`qminiwasm-core`)

Use this file for **Phase 4 implementation tooling** tracking and **maintainer hygiene**. Prefer **GitHub Issues** for owned work items, execution order, and assignees.

## Phase 4 tooling (checklist)

Link to the full milestone narrative: [`wiki/Roadmap.md`](../wiki/Roadmap.md).

1. **WLES test harness**
   - Extend beyond envelope JSON tests toward **Wasmtime** snapshot / restore with **linear memory** identity checks.
   - Entrypoints: [`memory_encode.py`](../qminiwasm/enclave/memory_encode.py) (`build_wles_envelope`), [`test_wles_esi_harness.py`](../tests/test_wles_esi_harness.py), [`engine.py`](../qminiwasm/enclave/engine.py), [`trit_wasm_runtime.py`](../qminiwasm/enclave/trit_wasm_runtime.py).

2. **ZTEE local simulator**
   - **Done:** in-process CA, JWT/JWKS RS256, `jti` revocation, AES-256-GCM — [`ztee_local_simulator.py`](../qminiwasm/security/ztee_local_simulator.py), [`test_ztee_handshake_simulator.py`](../tests/test_ztee_handshake_simulator.py).
   - **Next:** HTTP **OIDC** stub (`localhost`); **gRPC**-shaped encrypted WLES transfer tests (see [`docs/ZTEE_FRAMEWORK.md`](ZTEE_FRAMEWORK.md)).

3. **CPL integration**
   - Read and refine spike: [`docs/CPL_INTEGRATION_SPIKE.md`](CPL_INTEGRATION_SPIKE.md).
   - Follow-on: canonical CPL **event schema** versioned in docs or `configs/`, then backend + verifier tooling as separate issues.

## Maintainer hygiene (CI and docs)

| Item | Notes |
|------|-------|
| **Security Scans workflow** | Uses `pip install -r requirements/security.txt` with fallback to minimal deps; local docs may mention `pip install -e ".[security]"`. |
| **OpenSCAP in CI** | Report may be missing; artifact upload uses `if-no-files-found: ignore`. |
| **Trivy image job** | `continue-on-error: true` in `.github/workflows/ci.yml` until container images are hardened consistently. |

### Wiki vs repo layout

Some **wiki** pages describe optional `containers/`, legacy `wui/`, or full-stack OpenTofu layouts that are not present in every checkout. For **training**, **engine**, and **RunPod**, use the repo [README.md](../README.md), [training-wui/README.md](../training-wui/README.md), and [docs/RUNPOD_QUICKSTART.md](RUNPOD_QUICKSTART.md).

## Issues backlog ([open issues](https://github.com/kennetholsenatm-gif/qminiwasm-core/issues))

- **Workflow failure noise** — Older runs filed a **new** issue per failure (title included the run number). [`.github/workflows/failed-run-to-issue.yml`](../.github/workflows/failed-run-to-issue.yml) now uses a **stable title** per workflow (`🚨 Workflow failed: <name>`): the first failure **opens** one issue; later failures **comment** on that issue only.
- **One-time triage** — Filter `label:workflow-failure is:open`, **close duplicates** (keep one thread per workflow such as CI vs Security Scans once you confirm the current failure mode). Rename old titles if needed so they match the new stable pattern, or simply close all after `main` is green and let the next failure open a fresh deduped issue.

## GitHub repository follow-ups

1. **Default branch** — Prefer **`main`** as the GitHub default so new clones and **`origin/HEAD`** match integration. Steps: [`.github/README.md`](../.github/README.md) (section **Default branch (main)**).
2. **Fresh symref** — After changing the default on the server: `git fetch origin` then `git remote set-head origin -a`.
3. **Optional branch cleanup** — `feature/stateful-wasm-realignment` may match **`main`** after merge; delete on the remote only if you no longer want that name. Do **not** delete **`white-paper-integration`** (or others) until the default branch is switched and no open PRs or automations depend on it.

**Last updated:** 2026-03-25 (Phase 4 tooling pivot)
