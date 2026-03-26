# Master realignment execution status

This file tracks the **Q-Mini-WASM Master Realignment** prompt against what is **implemented on `main`**. It is updated when execution batches land.

## Phase 1 — Decruffing, vendor-neutral identity, layout

| Item | Status |
|------|--------|
| Vendor-neutral identity docs (OIDC IdP + internal X.509 CA wording) | **In progress** — canonical doc: [`IDENTITY_STACK_REFERENCE.md`](IDENTITY_STACK_REFERENCE.md); taxonomy linter blocks named vendor lock-in in gated paths |
| PKI script refactor | **Done** — [`scripts/security/internal_ca_pki_provisioning.py`](../scripts/security/internal_ca_pki_provisioning.py); legacy shim [`scripts/security/freeipa_pki_provisioning.py`](../scripts/security/freeipa_pki_provisioning.py) |
| Repo-wide legacy ML lexicon (`checkpoint`, etc.) | **Partial** — `qminiwasm/training/` uses **trainable TPEM** APIs + scoped taxonomy rule; engine kwargs remain ``checkpoint_*``; **TOML** may use preferred ``[tpem]`` (+ ``[serve].tpem``) per [LAYOUT_REALIGNMENT_RFC.md](LAYOUT_REALIGNMENT_RFC.md) |
| Flatten `engine/`, `qminiwasm/`, etc. into top-level domains | **RFC + P2–P6** — [LAYOUT_REALIGNMENT_RFC.md](LAYOUT_REALIGNMENT_RFC.md); P4: **`qminiwasm.engine`** only (top-level `engine/` removed) [ENGINE_QMINIWASM_BOUNDARY.md](ENGINE_QMINIWASM_BOUNDARY.md); P5: **`qminiwasm.enclave` removed**; P6: `qminiwasm.cli train`; optional: drop `qminiwasm.wasm` / `state` aliases |

## Phase 2 — Wiki / docs paradigm narrative

| Item | Status |
|------|--------|
| Stateful WASM / ZTEE / ECL / CGE narrative | **Partial** — see [`wiki/Roadmap.md`](../wiki/Roadmap.md), [`docs/ZTEE_FRAMEWORK.md`](ZTEE_FRAMEWORK.md) |

## Phase 3 — CI / CQ

| Item | Status |
|------|--------|
| Taxonomy linter (CI + pre-commit) | **Done** |
| WASM + TPEM artifacts in CI | **Done** (see `.github/workflows/ci.yml`) |
| Full removal of legacy eval tests | **Partial** — no ROUGE/BLEU in `tests/`; expand ESI/EM + ZTEE coverage over time |

## Phase 4 — Roadmap / tooling pivot

| Item | Status |
|------|--------|
| Roadmap / TODO pivot to implementation tooling | **Done** — [`wiki/Roadmap.md`](../wiki/Roadmap.md), [`docs/TODO.md`](TODO.md), [`CPL_INTEGRATION_SPIKE.md`](CPL_INTEGRATION_SPIKE.md) |

**Last updated:** Top-level `engine/` removed; `python -m qminiwasm.engine`; `qminiwasm.wasm` submodule shims collapsed.
