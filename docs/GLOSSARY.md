# qminiwasm-core glossary

Acronyms and domain terms used in this repository. Extended narrative vocabulary lives in [Q-Mini-WASM_ Edge AI Taxonomy.md](Q-Mini-WASM_%20Edge%20AI%20Taxonomy.md); identity and enrollment patterns are detailed in [IDENTITY_STACK_REFERENCE.md](IDENTITY_STACK_REFERENCE.md).

| Term | Meaning |
|------|---------|
| **WASM linear memory** | The contiguous, byte-addressable memory space of a WebAssembly module. The runtime enforces explicit bounds; the stack uses this for deterministic execution and snapshots. |
| **Ternary weights** | Model parameters constrained to `{-1, 0, 1}` (after scaling), reducing memory and arithmetic cost versus full-precision tensors for edge-sized footprints. |
| **TPEM (Ternary-Packed Memory Enclave)** | The packed representation of ternary weights in linear memory—e.g. multiple ternary values per byte via base-3 packing—so more effective capacity fits in the same bytes and memory traffic. See the taxonomy doc for bit-packing detail. |
| **State 1 / State 2** | **State 1**: default local inference in WASM with bounded memory. **State 2**: optional triggered path when classical expert/path assignment hits a configured combinatorial or latency wall; routing may use a Qiskit-backed **QAOA** solve. |
| **QAOA (Quantum Approximate Optimization Algorithm)** | Hybrid quantum-classical optimization used in this stack for combinatorial routing when State 2 is engaged—not the default inference path. |
| **ECL (Edge Cognitive Looping)** | Continuous edge-side reasoning and feedback for hard examples; pairs with escalation and routing policies in the hybrid stack. |
| **ZTEE (Zero-Trust Ephemeral Enrollment)** | Reference pattern for secure node startup: host identity via **X.509** / **mTLS 1.3**, workload identity via **OIDC/OAuth2 JWT** claims (tier, footprint, scopes). See [IDENTITY_STACK_REFERENCE.md](IDENTITY_STACK_REFERENCE.md). |
| **Enclave tier (1–5)** | Named memory envelopes (micro → enterprise core) mapping to default WASM page counts, Memory64 policy, and operator-facing capacity labels. Overrides in TOML/env win over tier defaults. |
| **QAHR (Quantum-Assisted Hierarchical Routing)** | Framing for quantum-assisted routing across edge/fog/cloud when escalation applies; related to State 2 and QAOA in architecture docs. |
| **WLES (WASM Linear Execution Snapshots)** | Serialization and resume of linear memory and execution state across host/Wasm boundaries (see `wles_wasmtime_harness` and architecture docs). |
