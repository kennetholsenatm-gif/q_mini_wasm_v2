# Project Taxonomy Glossary

This is the canonical glossary for project-specific terminology used across `qminiwasm-core`.

If a term changes or a new term is introduced, update this file first and then link to it from other docs instead of redefining terms repeatedly.

## Core Terms

### TPEM (Ternary-Packed Memory Enclave)
- Static, immutable packed-weight payload loaded into WASM linear memory.
- Defines the deployable reasoning payload boundary for a node.
- Related: `EF`, `WLES`.

### EF (Enclave Footprint)
- Memory budget allocated to an enclave runtime payload.
- Used for tier sizing and capacity planning across devices.
- Related: `TPEM`, `MSA`.

### ECL (Edge Cognitive Looping)
- The enclave's local iterative reasoning cycle.
- Local decision loop that emits certainty signals as it executes.
- Related: `CGE`, `ESI`.

### CGE (Certainty-Gated Escalation)
- Deterministic escalation trigger when local certainty drops below policy.
- Packages state and hands work to higher-tier routing/execution paths.
- Related: `ECL`, `QAHR`.

### ESI (Ephemeral State Inversion)
- Context recovery approach that reconstructs needed state on demand.
- Maintains bounded memory behavior instead of unbounded historical growth.
- Related: `MSA`, `WLES`.

### QAHR (Quantum-Assisted Hierarchical Routing)
- Hierarchical edge-fog-cloud routing policy with quantum-assisted optimization.
- Chooses escalation path under latency, trust, and topology constraints.
- Related: `CGE`, `ZTEE`.

### WLES (WASM Linear Execution Snapshots)
- Suspend/resume mechanism for serializing and restoring WASM linear state.
- Enables fast idle/restore transitions with deterministic runtime continuity.
- Related: `TPEM`, `ESI`.

### ZTEE (Zero-Trust Ephemeral Enrollment)
- Enrollment and trust lifecycle model for enclave nodes.
- Separates host hardware identity from enclave cognitive identity.
- Related: `QAHR`, `CPL`.

### CPL (Cognitive Provenance Ledger)
- Provenance-oriented trust record for cognitive actions and handoffs.
- Supports auditability and continuous trust verification signals.
- Related: `ZTEE`, `CGE`.

### MSA (Maximum State Aperture)
- Explicit upper bound of represented conversational or task state.
- Used to ensure state remains within enclave memory and policy constraints.
- Related: `ESI`, `EF`.

## Legacy to Preferred Term Map

Use the preferred taxonomy below in docs and operational artifacts.

| Legacy wording family | Preferred term |
|---|---|
| generic weight/tensor payload wording | `TPEM` |
| generic memory budget wording | `EF` |
| generic local reasoning-step wording | `ECL` |
| generic confidence-based handoff wording | `CGE` |
| generic context-growth wording | `ESI` + `MSA` |
| generic multi-tier routing wording | `QAHR` |
| generic suspend/restore state wording | `WLES` |
| generic zero-trust enrollment wording | `ZTEE` |
| generic provenance log wording | `CPL` |

## Canonical References

- [Project Goals](Project-Goals.md)
- [Q-Mini-WASM Edge AI Taxonomy](Q-Mini-WASM_%20Edge%20AI%20Taxonomy.md)
- [Enclave Lifecycle](ENCLAVE_LIFECYCLE.md)
