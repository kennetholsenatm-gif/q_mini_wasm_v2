# Master realignment execution status

This file tracks the **Q-Mini-WASM Master Realignment** prompt against what is **implemented on `main`**. It is updated when execution batches land.

## Phase 1 — Decruffing, vendor-neutral identity, layout

| Item | Status |
|------|--------|
| Vendor-neutral identity docs (OIDC IdP + internal X.509 CA wording) | **In progress** — canonical doc: [`IDENTITY_STACK_REFERENCE.md`](IDENTITY_STACK_REFERENCE.md); taxonomy linter blocks named vendor lock-in in gated paths |
| PKI script refactor | **Done** — [`scripts/security/internal_ca_pki_provisioning.py`](../scripts/security/internal_ca_pki_provisioning.py); legacy shim [`scripts/security/freeipa_pki_provisioning.py`](../scripts/security/freeipa_pki_provisioning.py) |
| Repo-wide legacy ML lexicon (`checkpoint`, etc.) | **Partial** — incremental; taxonomy covers diff-based gates for selected terms |
| Flatten `engine/`, `qminiwasm/`, etc. into top-level domains | **Not started** — high churn; requires import graph and release plan |

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

**Last updated:** execution batch (Phase 1 vendor-neutral identity + gates).
