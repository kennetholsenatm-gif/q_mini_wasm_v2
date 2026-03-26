# Master realignment execution status

This file tracks the **Q-Mini-WASM Master Realignment** prompt against what is **implemented on `main`**. It is updated when execution batches land.

*(This path is listed in `configs/ci/taxonomy_linter.json` → `skip_all_patterns_for_paths` so the weekly `--full` scan can describe linter policy without self-matching legacy token examples.)*

## Taxonomy gate policy (Phase 3)

| Mechanism | Role |
|-----------|------|
| **PR / push** | [`scripts/taxonomy_linter.py`](../scripts/taxonomy_linter.py) `--diff-base` in [`.github/workflows/ci.yml`](../.github/workflows/ci.yml) (`taxonomy-lint` job) — blocks **new** violations in added lines. |
| **Pre-commit** | [`.pre-commit-config.yaml`](../.pre-commit-config.yaml) `taxonomy-lint` with `--diff-working` — blocks uncommitted drift. |
| **Full tree** | [`.github/workflows/taxonomy-full.yml`](../.github/workflows/taxonomy-full.yml) — `python3 scripts/taxonomy_linter.py --full` on a **weekly schedule** and `workflow_dispatch`. |
| **FP dtype rules** | In [`configs/ci/taxonomy_linter.json`](../configs/ci/taxonomy_linter.json), `float32` / `float64` / `fp16` / `floating-point` patterns use **`apply_extensions`** (`.md`, `.toml`, `.yaml`, `.yml`) so Python/Go **numeric code** is not falsely flagged; prose and config remain enforced on `--full`. |
| **`checkpoint` term** | Lint rule applies under `qminiwasm/training/`, `qminiwasm/tpem/`, `qminiwasm/wasm_host/` only — **not** `qminiwasm/engine/` (serve/schema may keep legacy env/TOML names). |

---

## Phase 1 — Decruffing, vendor-neutral identity, layout

| Item | Status |
|------|--------|
| Vendor-neutral identity docs (OIDC IdP + internal X.509 CA wording) | **Done** — canonical: [`IDENTITY_STACK_REFERENCE.md`](IDENTITY_STACK_REFERENCE.md); taxonomy blocks **Keycloak** / **FreeIPA** in gated paths; PKI entry: [`internal_ca_pki_provisioning.py`](../scripts/security/internal_ca_pki_provisioning.py) |
| PKI script refactor | **Done** — [`scripts/security/internal_ca_pki_provisioning.py`](../scripts/security/internal_ca_pki_provisioning.py); compatibility shim [`scripts/security/freeipa_pki_provisioning.py`](../scripts/security/freeipa_pki_provisioning.py) |
| Unified Edge lexicon in code comments / docstrings | **Largely done** — e.g. **ECL step** replaces “forward pass” in hot `qminiwasm/` layers and cognitive modules; [`scripts/build_tpem_wasm_artifacts.py`](../scripts/build_tpem_wasm_artifacts.py) imports **`qminiwasm.wasm_host`** only |
| Flatten `qminiwasm/` into many top-level repo domains | **Deferred / RFC** — core code remains under [`qminiwasm/`](../qminiwasm/); pilots documented in [LAYOUT_REALIGNMENT_RFC.md](LAYOUT_REALIGNMENT_RFC.md) |
| Remove Docker / cloud-GPU-only MLOps assumptions | **Partial** — not exhaustively inventoried; training and serve paths remain for hybrid deployments |

---

## Phase 2 — Wiki / docs paradigm narrative

| Item | Status |
|------|--------|
| Stateful WASM / ZTEE / ECL / CGE “how” | **Done** — [`architecture/JOURNEY_OF_A_VECTOR.md`](architecture/JOURNEY_OF_A_VECTOR.md), [`IDENTITY_STACK_REFERENCE.md`](IDENTITY_STACK_REFERENCE.md) |
| Wiki entry + Tier 1–5 table | **Done** — [`wiki/Home.md`](../wiki/Home.md) (trainable TPEM vocabulary, Tier 1–5 summary); wiki passes aligned with taxonomy where updated ([`wiki/Vec2Text-Inversion.md`](../wiki/Vec2Text-Inversion.md), [`wiki/Architecture-Overview.md`](../wiki/Architecture-Overview.md), [`wiki/Mathematical-Formulation.md`](../wiki/Mathematical-Formulation.md)) |

---

## Phase 3 — CI / CQ

| Item | Status |
|------|--------|
| Taxonomy linter (CI + pre-commit + scheduled full) | **Done** — see **Taxonomy gate policy** above |
| WASM + TPEM artifacts in CI | **Done** — `.github/workflows/ci.yml` `build-artifacts`; [`build_tpem_wasm_artifacts.py`](../scripts/build_tpem_wasm_artifacts.py) |
| Stateful verification (WLES / ESI / ZTEE) | **Advanced** — Wasmtime WLES snapshot/restore: [`qminiwasm/wasm_host/wles_wasmtime_harness.py`](../qminiwasm/wasm_host/wles_wasmtime_harness.py), [`tests/test_wles_wasmtime_harness.py`](../tests/test_wles_wasmtime_harness.py); ESI **EM** extended: [`tests/test_vec2text_diffusion.py`](../tests/test_vec2text_diffusion.py); ZTEE OIDC metadata helper + test: [`ztee_local_simulator.build_oidc_provider_metadata`](../qminiwasm/security/ztee_local_simulator.py), [`tests/test_ztee_handshake_simulator.py`](../tests/test_ztee_handshake_simulator.py) |
| Legacy eval tests (ROUGE/BLEU) | **Done** — none in `tests/`; EM-focused tests preferred |

---

## Phase 4 — Roadmap / tooling pivot

| Item | Status |
|------|--------|
| Roadmap / TODO pivot to implementation tooling | **Done** — [`wiki/Roadmap.md`](../wiki/Roadmap.md), [`docs/TODO.md`](TODO.md) |

**Last updated:** Master realignment batch — taxonomy full workflow + prose-only FP rules; WLES Wasmtime harness; wiki/Home Tier 1–5; ESI EM + ZTEE OIDC metadata tests; ECL lexicon pass.
