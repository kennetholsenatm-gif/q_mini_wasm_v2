# Hierarchical Edge-Quantum AI Architecture

## Track 1: General Overview (For Leadership & Laypersons)

Welcome. This track summarizes what the **qminiwasm-core** / Q-Mini-WASM effort is aiming for and why it matters, without assuming a technical background.

### What problem does this address?

**Stateful AI at the edge.** Many assistants lose context over long sessions or depend on a large cloud footprint. Teams also want models and tooling that can run closer to data—on laptops, servers, or constrained devices—while still using cloud services when appropriate.

**Trust and isolation.** Sensitive workflows benefit from strong isolation (for example **WebAssembly** sandboxes) and cryptography where it fits the threat model. Real systems are never “unhackable”; goals here are **reduced attack surface**, **local processing** where it helps, and **auditable** components—not absolute guarantees.

### What we combine

1. **Edge-oriented execution** — WASM-backed runtimes and encodings so ML pipelines can align with **linear memory** and deployment shapes you actually ship.
2. **Hybrid classical–quantum-style routing** — Quantum-inspired or backend-driven routing (for example **QAOA**-style layers in the stack) to explore structured search over internal state. With **`qiskit_ibm`** and IBM credentials, training has **successfully used real IBM Quantum hardware** for the MoE QAOA path (March 2026); otherwise behavior may use simulators or a no-op `pennylane` mode depending on configuration.
3. **Modern ML training and inference** — Supervised training (`python -m engine`), optional cascade reinforcement-learning warm-up, checkpoints, and HTTP inference. For design rationale, see **[AI Training Pipeline](AI-Training-Pipeline.md)**.

### Read first: Stateful WASM Agents + ZTEE

This wiki is written for a **stateful WASM agent** paradigm, not stateless “cloud-hosted calculators”.

Edge cognition uses:
- **Ternary-Packed Memory Enclave (TPEM)** and deterministic in-WASM **Edge Cognitive Looping (ECL)**.
- **Ephemeral State Inversion (ESI)** for context regeneration, and **WASM Linear Execution Snapshots (WLES)** for suspend/resume.

Trust continuity is governed by **Zero-Trust Ephemeral Enrollment (ZTEE)**:
- **Hardware identity**: host **X.509** PKI (internal CA / TPM-assisted where applicable)
- **Cognitive identity**: enclave **OIDC/OAuth2 JWT** claims (tier + EF + topic scopes)
- **Bootstrap + authorization**: mTLS 1.3 enrollment to broker + QAHR controller, with **JWKS** validation
- **Continuous revocation**: CRL/OCSP + IdP session/JWT family revocation and broker socket drop

Authoritative protocol text: [`docs/ZTEE_FRAMEWORK.md`](../docs/ZTEE_FRAMEWORK.md). For the end-to-end lifecycle, start with [`docs/ENCLAVE_LIFECYCLE.md`](../docs/ENCLAVE_LIFECYCLE.md). Tier semantics live in [`docs/Q-Mini-WASM_ Edge AI Taxonomy.md`](../docs/Q-Mini-WASM_%20Edge%20AI%20Taxonomy.md).

### Why it matters

- **Operations:** Smaller deployable units and clearer boundaries between edge and cloud.
- **Security and privacy:** More inference can stay local; combine encryption and sandboxing to match your compliance story.
- **Research and product:** One place to experiment with WASM traces, Hub-scale tabular pretraining, and deployment-aligned **mesh/corpus** data.

### Example directions

- **Defense and regulated environments** — Air-gapped or teleported access patterns (see **[Development](Development.md)** and security docs).
- **Healthcare and finance** — When policies require local inference or strict data residency; always validate against your own legal and security review.
- **Developer tooling** — Encoding code and WASM artifacts into fixed-width vectors for hybrid models.

### How this differs from generic LLM stacks

- An explicit path for **WASM linear memory encoding** alongside optional **large-scale text / Hugging Face** pretraining.
- A **training loop** that can mix cascade (GRPO) micro-steps with supervised MSE on 4096-d vectors—not only next-token language modeling.
- Focus on **reproducible, env-driven** runs and **checkpoint → serve** alignment.

### Looking ahead

The project evolves with hardware (CPU, CUDA, Intel **XPU**) and optional quantum backends—including **IBM Quantum** for the QAOA integration when configured. What you get depends on configuration, dataset licenses, environment flags, and cloud quotas.

---

**Ready for implementation detail?** See [Track 2: Infrastructure & DevSecOps](Architecture-Overview.md) and [Track 3: Academic & Theoretical](Mathematical-Formulation.md).

---

**Last Updated:** 2026-03-23  
**Version:** 2.1

## Related Resources

- **Wiki — AI training (why and how):** [AI Training Pipeline](AI-Training-Pipeline.md)
- **NotebookLM documentation:** [Hierarchical Edge-Quantum AI Architecture Notebook](https://notebooklm.google.com/notebook/62d6c7ee-8f93-4c5f-ac67-19b1a8956219)
