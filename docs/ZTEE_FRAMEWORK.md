# Zero-Trust Ephemeral Enrollment (ZTEE)

A **protocol-oriented** architecture for securing decentralized, **stateful** WebAssembly agent meshes. ZTEE complements the Edge AI taxonomy (**WLES**, **CGE**, **QAHR**, **EF**, **LCI**, **EM**) by defining how nodes **enroll**, **migrate state**, and stay **revocable** under continuous verification.

**Tier alignment (EF taxonomy vs. narrative):** This repository classifies deployment sizes as **Tier 1 Micro**, **Tier 2 Meso**, **Tier 3 Macro**, **Tiers 4–5 Workgroup / Enterprise Core** ([`configs/serve/default.toml`](../configs/serve/default.toml), [`Concepts-Explained.md`](../Concepts-Explained.md)). General text that contrasts a *smaller* enclave with a *core datacenter* enclave maps to **Meso (Tier 2) vs. Enterprise Core (Tier 5)**—not to the separate **three-tier infrastructure** story (edge / transit / cloud fabric).

For pipeline context, see [`PIPELINE_STATEFUL_WASM_OPS.md`](PIPELINE_STATEFUL_WASM_OPS.md).

---

## 1. Root of trust: hardware and cognitive identity

To prevent spoofing or unauthorized ingestion of **WASM Linear Execution Snapshots (WLES)**, trust is asserted at both the **host transport** layer and the **agent (enclave)** layer.

### Hardware identity (X.509 PKI)

When a compute node (e.g. unified-memory workstation or edge appliance) is provisioned, infrastructure binds the host—ideally using a **TPM** or vendor secure enclave—to a **machine identity**. The host obtains a **short-lived X.509** certificate from an internal **Certificate Authority (CA)**. That certificate authorizes the machine on the network control plane only; it does not grant cognitive mesh privileges by itself.

### Cognitive identity (OAuth2 / OIDC)

Independently, an **OIDC-compliant Identity Provider (IdP)** authenticates the **WASM enclave** (or its control sidecar). On startup, the enclave completes an OAuth2 **client credentials** grant (or **device code** flow where appropriate) and receives a signed **JWT**. JWT claims SHOULD include:

- **Enclave tier** and **Enclave Footprint (EF)** hints, aligned with `enclave_tier` / `enclave_footprint_mb` in [`HierarchicalConfig`](../qminiwasm/config.py).
- **Authorized ingress/egress scopes** for broker topics and QAHR operations.
- **Session or key family identifier** for later revocation.

Hardware identity and cognitive identity are **orthogonal**: compromise of one must not silently imply the other.

---

## 2. Bootstrap handshake and broker registration

When an enclave moves from **idle** to **active** deployment, it enrolls into the **event broker fabric** without exposing raw state vectors on an open channel.

1. **mTLS 1.3** — The host uses its X.509 identity to open **mutual TLS** to the message broker and to the **QAHR controller** (control plane).
2. **JWT assertion** — Over mTLS, the enclave presents its OIDC JWT to the QAHR controller.
3. **Topology injection and authorization** — The controller validates the JWT (signature via **JWKS**), parses claims for EF/tier, and only then **admits** the node into the live routing problem: cost Hamiltonian / QUBO parameters reflect updated topology and policy. The enclave is granted **publish/subscribe** rights only on topics allowed by claims.

Conceptually this ties to QAHR escalation metadata produced in-process (e.g. [`prepare_escalation_payload`](../qminiwasm/cognitive/escalation.py)); the **operational** controller is out of tree ([`qminiwasm/fabric/`](../qminiwasm/fabric/) holds classical/shim logic used by trainers and tests).

---

## 3. Secure state migration (delta sync)

When **Certainty-Gated Escalation (CGE)** fires, **WLES** payloads MUST NOT be shipped in the clear across the broad mesh.

1. **Local package** — The enclave builds a WLES envelope (see [`memory_encode.build_wles_envelope`](../qminiwasm/enclave/memory_encode.py)), applies **delta compression** against an agreed baseline when available, then encrypts with **AES-256-GCM**.
2. **Out-of-band keys** — The **QAHR controller** (or dedicated key broker) delivers the AES **data-key** only to the **authorized target** enclave (e.g. Enterprise Core) over the existing **authenticated control plane**—never inside the WLES blob.
3. **Ephemeral data plane** — Ciphertext moves **peer-to-peer** (target interface: **gRPC** over **mTLS**) from source to destination. Intercepted packets remain useless without the control-plane key material.

Static weights may travel as **[TPEM sidecars](TPEM_ARTIFACT_FORMAT.md)** under separate policy; WLES here means **runtime linear memory / execution snapshot** state.

---

## 4. Continuous trust and autonomous eviction

Zero trust means **re-verification**, not one-time login.

### Telemetry and provenance

The **Cognitive Provenance Ledger (CPL)** is the conceptual audit trail that binds **JWT identity** to runtime evidence: **Local Containment Index (LCI)**, **Exact Match (EM)** / ESI fidelity signals, and other SOA metrics ([`monitoring/README.md`](../monitoring/README.md)). CI in this repo partially exercises LCI/EM smoke tests; production CPL is an external store.

### Anomaly handling

If a node emits corrupted state vectors, breaches OAuth2 scopes, or shows persistent fidelity collapse, automation MUST flag the identity.

### Cryptographic severance

Revocation is **two-pronged**:

1. **Transport** — Publish host certificate to **CRL** or update **OCSP**, breaking established mTLS.
2. **Cognitive** — Instruct the IdP to **revoke** the OAuth2 session and **blocklist** the JWT **family** (e.g. `jti`/token lineage).

The broker then **terminates** the session, removing the node from the mesh **mid-cycle** if necessary.

---

## 5. Implementation scope in this repository

| Capability | Status |
|------------|--------|
| Taxonomy + WLES/CGE/QAHR **concepts** in code/docs | In tree |
| OIDC client in WASM / sidecar | **Future** (e.g. `serverless/` or dedicated agent) |
| gRPC WLES transfer + protos | **Future** (infra or separate module) |
| QAHR controller / key broker API | **Future** (operational service) |
| CPL backing store + schemas | **Future** |

This file is the **normative prose** specification; runtime bindings should reference it from deployment playbooks and service repos.
