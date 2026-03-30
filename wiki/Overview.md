# Overview: Stateful WASM Agent Manual

This is the read-first index for the **Stateful WASM Agent Paradigm** implemented by this repository.

Start with the conceptual “why” and then follow the operator “how”:

- **Primer:** [`wiki/Home.md`](Home.md)
- **Identity and trust protocol reference:** [`docs/IDENTITY_STACK_REFERENCE.md`](../docs/IDENTITY_STACK_REFERENCE.md)
- **Operator runtime flow (boot → ECL → CGE → WLES packaging):** [`docs/architecture/JOURNEY_OF_A_VECTOR.md`](../docs/architecture/JOURNEY_OF_A_VECTOR.md)
- **Tier semantics (EF + enclave tier):** [`docs/Q-Mini-WASM_ Edge AI Taxonomy.md`](../docs/Q-Mini-WASM_%20Edge%20AI%20Taxonomy.md)
- **Stateful WASM operations runbook:** [`docs/operations/OPERATIONS_RUNBOOK.md`](../docs/operations/OPERATIONS_RUNBOOK.md)
- **TPEM artifact format (sidecar spec):** [`docs/TPEM_ARTIFACT_FORMAT.md`](../docs/TPEM_ARTIFACT_FORMAT.md)
- **Implementation roadmap (tooling):** [`wiki/Roadmap.md`](Roadmap.md) and maintainer checklist [`docs/TODO.md`](../docs/TODO.md)
- **CPL implementation tracker:** [`docs/TODO.md`](../docs/TODO.md)

## What you should expect

- **State is first-class**: context regeneration uses **Ephemeral State Inversion (ESI)**, and suspend/resume uses **WASM Linear Execution Snapshots (WLES)**.
- **Cognition is bounded**: the edge agent runs **Edge Cognitive Looping (ECL)**, the continuous edge-to-host feedback mechanism for hard examples, until the emitted certainty scalar reaches the deployment threshold, then hands off via **Certainty-Gated Escalation (CGE)**.
- **Trust is continuous**: **Zero-Trust Ephemeral Enrollment (ZTEE)**, the cryptographic handshake and identity bootstrap required for secure node startup, separates **hardware identity** (host **X.509** PKI) from **cognitive identity** (enclave **OIDC/OAuth2 JWT** claims) and defines mTLS/JWKS bootstrap plus revocation semantics.

## Next steps

- If you’re trying to run **native training / WUI**: [`training-wui/README.md`](../training-wui/README.md)
- If you’re deploying: [`wiki/Deployment-Guide.md`](Deployment-Guide.md)
- If you’re integrating quantum-assisted routing: [`wiki/Mathematical-Formulation.md`](Mathematical-Formulation.md)

