# Project Goals: Stateful WASM Agent Framework

This repository develops a **deterministic Stateful WASM Agent** framework for edge-first execution, using the **Unified Edge Vocabulary** and the **ZTEE** security lifecycle.

## Core outcomes

- **TPEM as the static payload:** deploy logic with a **Ternary-Packed Memory Enclave (TPEM)** loaded into WASM linear memory, accounted under **Enclave Footprint (EF)**.
- **ECL as the reasoning primitive:** the agent’s cognitive activity is expressed as **Edge Cognitive Looping (ECL)** gated by emitted **certainty scalars**.
- **ESI for bounded context:** context regeneration uses **Ephemeral State Inversion (ESI)** rather than KV-style growth; deployable state is expressed in an **MSA**-bounded form.
- **WLES for suspend/resume:** the runtime can serialize and restore **WASM Linear Execution Snapshots (WLES)** to move from active compute to idle efficiently.
- **CGE for deterministic handoff:** when local certainty is insufficient, **Certainty-Gated Escalation (CGE)** packages the required state for escalation.
- **ZTEE for zero-trust continuity:** enrollment and authorization are driven by **ZTEE**, separating **hardware identity** (host **X.509** PKI) from **cognitive identity** (enclave **OIDC/OAuth2 JWT** claims), with continuous revocation.

## Security + trust metrics (operational intent)

The framework targets continuous trust through the **Cognitive Provenance Ledger (CPL)** concept, with operational signals such as:

- **LCI**: local containment effectiveness
- **EM** (and ESI fidelity): reconstruction correctness under inversion

## Platform direction

- **Training and WUI orchestration** target **Go plus C++ (LibTorch over gRPC)**; Python remains for tests, WASM artifact tooling, and workflows where no practical Go or C++ replacement exists yet.

## Success criteria (definition of done)

- Documentation and artifacts consistently use the unified vocabulary (TPEM/EF/ECL/ESI/WLES/ZTEE/CPL).
- CI includes a taxonomy gate so forbidden legacy terms do not enter the docs/code paths.
- Test coverage validates the stateful flows (ECL outcomes, CGE payload packaging, WLES/ESI envelope invariants).

