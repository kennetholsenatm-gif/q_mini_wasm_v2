# Cognitive Provenance Ledger (CPL): integration spike

This document is an **architecture spike** for integrating an **append-only Merkle transparency log** as the operational backbone of the **Cognitive Provenance Ledger (CPL)** concept. It does **not** implement a log service in this repository.

Normative mesh enrollment and migration semantics remain in [`docs/ZTEE_FRAMEWORK.md`](ZTEE_FRAMEWORK.md). CPL ingestion is assumed to run **outside** this repo; here we define **what to log**, **candidate backends**, and **how verifiers could reason about proofs**.

## Purpose

- Provide a **tamper-evident** audit trail for:
  - **Certainty scalars** emitted during **Edge Cognitive Looping (ECL)** (or aggregated summaries over windows).
  - **WLES** / **CGE** migration evidence (hashes of envelopes or ciphertext, correlation IDs).
  - **ZTEE** identity binding **by reference** (e.g. JWT `sub`, `jti`, issuer, certificate fingerprint)—not raw long-lived secrets.
- Support **cross-mesh** forensic comparison: given a log entry, verify inclusion in an append-only tree and detect fork or rewrite if witnesses disagree.

## Candidate backends

| Option | Summary | Fit |
|--------|---------|-----|
| **Trillian** | General-purpose Merkle tree transparency log (often MySQL-backed); flexible payloads. | Strong when you want **custom CPL event types**, own retention policy, and internal operation. |
| **Sigstore Rekor** | Public-good transparency log with **canonical Rekor entry types**; strong ecosystem for **supply-chain** and artifact signing. | Strong when CPL events align with **artifact-centric** records (e.g. signed attestations over WLES/TPEM digests) and you want **interop** with Sigstore clients. |

**Selection criteria (non-exhaustive):**

- Operator cost, region, and data residency.
- **Retention** and legal hold requirements for certainty telemetry.
- **Federation** or multi-tenant isolation (separate logs per trust domain).
- Alignment with organizational **SLSA** / attestation workflows (favors Rekor-shaped attestation pipelines).
- Need for **custom schemas** vs off-the-shelf entry types (favors Trillian or Rekor with custom `type` only if supported).

No recommendation is final; Phase 4 stops at this comparison + schema sketch.

## Suggested event model

Events are **JSON (or canonical CBOR)** normalized before hashing. Field names are illustrative; version them under `cpl_schema_version`.

| Field | Description |
|--------|-------------|
| `cpl_schema_version` | Integer or semver string for forward compatibility. |
| `event_type` | e.g. `certainty_scalar`, `wles_envelope_sealed`, `cge_handoff`, `ztee_enrollment_binding`. |
| `timestamp` | RFC 3339 UTC from trusted clock or log server receipt time. |
| `enclave_subject` | OIDC `sub` or stable enclave identifier claim. |
| `enclave_jti` | JWT ID for the active cognitive session when applicable. |
| `issuer` | OIDC `iss` or CA identifier reference. |
| `certainty_scalar` | Scalar or compact summary for `certainty_scalar` events. |
| `cge_correlation_id` | UUID or monotonic ID tying CGE to downstream routing. |
| `wles_envelope_sha256` | SHA-256 over canonical WLES envelope bytes or agreed serialization (see [`memory_encode.py`](../qminiwasm/enclave/memory_encode.py)). |
| `ciphertext_sha256` | Optional: hash of encrypted payload after **AES-GCM** packaging (per ZTEE). |
| `parent_event_hash` | Optional chain pointer for application-level sequencing (not a substitute for Merkle inclusion). |
| `host_x509_fingerprint_sha256` | Optional: fingerprint of short-lived host cert for transport binding (no private keys). |

**Hashing rule (sketch):** `event_digest = SHA256(canonical_encode(payload))` as the leaf input to the transparency log’s Merkle tree per the chosen backend.

## Verification story

1. **Submit** — Producer (or broker adapter) appends an event; receives a **signed tree head** or entry handle from the log.
2. **Inclusion proof** — Verifier obtains Merkle **inclusion path** (and **signed tree head**) from the log or an auditor mirror.
3. **Offline check** — Verifier recomputes root from `event_digest` + path; compares to trusted tree head (key pinning or witness consensus).
4. **CLI / CI hook (future)** — Small read-only tool: `cpl-verify --entry <blob> --proof <proof>` without running a full mesh.

## Non-goals (this repo, today)

- Running **Trillian** or **Rekor** in default **GitHub Actions** CI.
- Storing raw **WLES** plaintext or **data keys** in the log (only **hashes** and **references**).
- Replacing [`docs/ZTEE_FRAMEWORK.md`](ZTEE_FRAMEWORK.md) as the ZTEE specification.

## Related documents

- [`docs/ZTEE_FRAMEWORK.md`](ZTEE_FRAMEWORK.md) — continuous trust, CPL mention, revocation.
- [`wiki/Roadmap.md`](../wiki/Roadmap.md) — CPL milestone list.
- [`docs/TODO.md`](TODO.md) — tooling checklist and maintainer hygiene.

**Last updated:** 2026-03-25
