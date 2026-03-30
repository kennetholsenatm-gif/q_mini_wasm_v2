# Enclave Lifecycle (Operator How): ZTEE → ECL → CGE → WLES

This page is the single “how it works” lifecycle for **stateful WASM agents** in this repository.

It intentionally keeps **ZTEE** as a single source of truth:
- ZTEE protocol text: [`docs/ZTEE_FRAMEWORK.md`](ZTEE_FRAMEWORK.md)

The lifecycle is written in the **Unified Edge Vocabulary** so operator docs, security docs, and telemetry naming stay consistent.

## Core actors and identities

The lifecycle uses two identity layers:

- **Hardware identity (host X.509 PKI)**: short-lived certificates issued by an internal CA, used for **mTLS 1.3** bootstrap to broker/control-plane services.
- **Cognitive identity (enclave OIDC/OAuth2 JWT)**: enclave obtains a signed JWT whose claims encode **enclave tier**, **EF capacity**, and **topic scopes** (authorized publish/subscribe permissions).

Tier semantics and claim mapping live in:
- [`docs/Q-Mini-WASM_ Edge AI Taxonomy.md`](Q-Mini-WASM_%20Edge%20AI%20Taxonomy.md)
- `configs/serve/default.toml` (`enclave_tier`, `enclave_footprint_mb`) and Mission Control / engine TOML under `configs/` (see [CONFIGURATION_POLICY.md](CONFIGURATION_POLICY.md))

## Lifecycle overview

```mermaid
flowchart LR
  Boot[Boot + mTLS bootstrap] --> Enroll[ZTEE enroll + QAHR validation]
  Enroll --> ECL[ECL reasoning (certainty-gated)]
  ECL --> CGE[CGE trigger + halt ECL]
  CGE --> WLES[Package WLES envelope + encrypted payload]
  WLES --> Route[Quantum-routed delivery to target enclave]
  Route --> Trust[Continuous trust + CPL telemetry]
```

## Step-by-step lifecycle

### 1. Boot + mTLS bootstrap (host identity proves transport legitimacy)

1. The host provisions (or retrieves) a short-lived **X.509** certificate.
2. The enclave runtime establishes **mTLS 1.3** connections to the message broker and the QAHR controller using the host certificate.
3. The control plane is now ready to accept **cognitive identity** assertions from the enclave.

### 2. ZTEE enrollment + topology injection (JWT proves cognitive scope)

1. The enclave executes an OIDC flow and obtains a signed **JWT**.
2. The QAHR controller validates the JWT signature against **JWKS**.
3. Claims are used to determine:
   - **enclave tier** and **EF capacity**
   - authorized ingress/egress topic scopes
4. QAHR injects the approved node topology into its cost/optimization model and authorizes only permitted event topics.

For the full handshake specifics (mTLS/JWKS/revocation semantics), see [`docs/ZTEE_FRAMEWORK.md`](ZTEE_FRAMEWORK.md).

### 3. Edge Cognitive Looping (ECL) (reason until certainty reaches threshold)

1. The enclave runs the deterministic **Edge Cognitive Looping (ECL)**.
2. ECL emits **certainty scalars** continuously.
3. The loop may finish locally once certainty reaches the configured deployment threshold (`certainty_scalar_threshold` / `$T_{conf}$`).

State is bounded by EF / tier configuration, and context is maintained via **ESI** (Ephemeral State Inversion), not unbounded cache growth.

### 4. Certainty-Gated Escalation (CGE) trigger (halt ECL and capture required state)

When certainty is insufficient:

1. ECL halts autonomously.
2. The orchestration layer captures the required enclave state for migration.
3. A **WLES** (WASM Linear Execution Snapshot) envelope is packaged, with the payload prepared for secure transport:
   - delta-compress where applicable
   - encrypt the payload (AES-256-GCM)
   - keep encryption keys out-of-band (supplied via the control plane / key broker semantics)

**C++ tier chain (portable handoffs):** Native code can attach a versioned binary prefix [`QmwEscalationEnvelopeHeader`](../cpp/escalation/escalation_c_api.h) before the encrypted or plaintext payload body, and drive **multi-hop escalation** with `qmw_escalation_resolve_next`: Tier 1 (edge) → Tier 2 (state migration) → Tier 3 (QAHR-classical) → optional Tier 4 (native OpenQASM / trinary sim when `QMINIWASM_WITH_QUANTUM=ON`) → optional Tier 5 (external QPU placeholder). This is the **portable policy surface** for CGE vocabulary on embedded hosts; WLES payload bodies remain operator-defined. Optional: `qmw_escalation_run_native_openqasm_if_applicable` delegates to `qmw_openqasm_expval_pauli_z0` only when the last resolved tier is Tier 4.

### 5. Quantum-routed delivery (secure, short-lived data plane channel)

1. The control plane selects the target enclave tier/path using **QAHR** cost shaping.
2. The Tier 2 → Tier 5 transfer path uses an authenticated, short-lived **mTLS gRPC data plane** (as the target interface).
3. The encrypted WLES payload arrives at the target enclave, which then resumes compute using its own validated lifecycle and scopes.

### 6. Continuous trust and autonomous eviction (CPL + revocation)

Once enrolled, trust is continuously evaluated:

1. Telemetry ingestion updates **Cognitive Provenance Ledger (CPL)** signals and operational metrics such as:
   - **LCI** (local containment effectiveness)
   - **EM** / ESI fidelity (reconstruction correctness)
2. If anomalies indicate corrupted state or scope violations:
   - publish the host certificate revocation to **CRL/OCSP**
   - revoke the IdP session / JWT family and drop the broker socket
3. The enclave is removed from the active fabric mid-cycle.

## Expert fleet (multi-WASM coordinator pattern)

Some deployments place a **FleetCoordinator** role at **enterprise-scale** capacity (see EF `enterprise_core` in the taxonomy): a control-plane process that **routes** work to many **ExpertMember** enclaves—each typically a **macro** or **workgroup** WASM expert with its own linear-memory budget—without replacing those modules’ local execution.

- **FleetCoordinator:** holds routing policy, optional telemetry aggregation, and **assignment** only (no substitute for per-expert WASM semantics). In-repo this lines up with [`qmw_expert_fleet_topk`](../cpp/router/expert_fleet_c_api.h) / [`qmw_expert_fleet_partition`](../cpp/router/expert_fleet_c_api.h) over [`qmw_route_*`](../cpp/qubo/dqaoa_routing_runtime_c_api.h), and with [`on_route_hint`](../cpp/wasm/wasm_host_hooks_c_api.h) on the WASM host path. **Multi-node scheduling, health checks, and RPC between enclaves are out of scope** here; use your broker/mTLS fabric (see above) and [`wiki/Deployment-Guide.md`](../wiki/Deployment-Guide.md).

- **ExpertMember:** WASM instance with stable `instance_id`, optional `capability_mask`, and a fixed-length **route embedding** (`double` vector) supplied by the operator or derived from encoded hidden state. Descriptor POD: `QmwExpertMemberDescriptor` in [`expert_fleet_c_api.h`](../cpp/router/expert_fleet_c_api.h) (v1 metadata only; IPC is operator-defined).

**Tier vocabulary (do not conflate):**

| Concept | Where it lives | Meaning (short) |
|--------|----------------|-----------------|
| EF **enclave tier** (`micro` … `enterprise_core`) | Taxonomy / `enclave_tier` in TOML | Capacity / pages / Memory64 envelope |
| **Training `TaxonomyTier`** (`kEdgeConstrained`, `kFogNode`, `kXpuCluster`) | [`taxonomy_tier.hpp`](../cpp/training/include/qminiwasm/training/taxonomy_tier.hpp) | Native training loop resource policy (throughput vs edge); **not** the same as inference fleet layout |
| **`QmwEscalationTier`** | [`escalation_c_api.h`](../cpp/escalation/escalation_c_api.h) | Certainty-gated **handoff** chain (incl. external QPU placeholder at T5); **not** “cluster head for N WASM experts” |
| **FleetCoordinator (this section)** | Docs + `expert_fleet_*` C API | Logical **router** over many ExpertMembers at deploy time |

## Operator references (what to read next)

- ZTEE protocol spec: [`docs/ZTEE_FRAMEWORK.md`](ZTEE_FRAMEWORK.md)
- CQ/stateful ops mapping: [`docs/PIPELINE_STATEFUL_WASM_OPS.md`](PIPELINE_STATEFUL_WASM_OPS.md)
- Static packed payload (TPEM sidecar spec): [`docs/TPEM_ARTIFACT_FORMAT.md`](TPEM_ARTIFACT_FORMAT.md)

