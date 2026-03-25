# Overview: Stateful WASM Agent Manual

This is the read-first index for the **Stateful WASM Agent Paradigm** implemented by this repository.

Start with the conceptual “why” and then follow the operator “how”:

- **Primer:** [`wiki/Home.md`](Home.md)
- **Core ZTEE protocol (single source of truth):** [`docs/ZTEE_FRAMEWORK.md`](../docs/ZTEE_FRAMEWORK.md)
- **Operator lifecycle (boot → enroll → ECL → CGE → WLES packaging):** [`docs/ENCLAVE_LIFECYCLE.md`](../docs/ENCLAVE_LIFECYCLE.md)
- **Tier semantics (EF + enclave tier):** [`docs/Q-Mini-WASM_ Edge AI Taxonomy.md`](../docs/Q-Mini-WASM_%20Edge%20AI%20Taxonomy.md)
- **CQ / Stateful WASM Ops blueprint mapping:** [`docs/PIPELINE_STATEFUL_WASM_OPS.md`](../docs/PIPELINE_STATEFUL_WASM_OPS.md)
- **TPEM artifact format (sidecar spec):** [`docs/TPEM_ARTIFACT_FORMAT.md`](../docs/TPEM_ARTIFACT_FORMAT.md)
- **Implementation roadmap (tooling):** [`wiki/Roadmap.md`](Roadmap.md) and maintainer checklist [`docs/TODO.md`](../docs/TODO.md)
- **CPL integration spike:** [`docs/CPL_INTEGRATION_SPIKE.md`](../docs/CPL_INTEGRATION_SPIKE.md)

## What you should expect

- **State is first-class**: context regeneration uses **Ephemeral State Inversion (ESI)**, and suspend/resume uses **WASM Linear Execution Snapshots (WLES)**.
- **Cognition is bounded**: the edge agent runs **Edge Cognitive Looping (ECL)** until the emitted certainty scalar reaches the deployment threshold, then hands off via **Certainty-Gated Escalation (CGE)**.
- **Trust is continuous**: **ZTEE** separates **hardware identity** (host **X.509** PKI) from **cognitive identity** (enclave **OIDC/OAuth2 JWT** claims) and defines mTLS/JWKS bootstrap plus revocation semantics.

## Next steps

- If you’re trying to run locally: [`docs/getting-started/QUICKSTART_0_TO_1.md`](../docs/getting-started/QUICKSTART_0_TO_1.md)
- If you’re deploying: [`wiki/Deployment-Guide.md`](Deployment-Guide.md)
- If you’re integrating quantum-assisted routing: [`wiki/Mathematical-Formulation.md`](Mathematical-Formulation.md)

