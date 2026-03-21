# Hierarchical Inference Architecture: Module Map

This document maps the **Hierarchical Inference Architecture** white paper (Tier 1/2/3) to the LLM_Pract codebase.

## Tier 1 — Edge (WASM + Cognitive Looping)

| White paper concept | Module / API |
|---------------------|--------------|
| WASM execution (Wasm32-WASI) | `qminiwasm.wasm.engine.WasmExecutor`: `compile_wasm()`, `execute()` |
| Linear memory, stack snapshot at halt | `WasmExecutor.capture_deltas(instance)` |
| Cognitive Looping (ACT), certainty scalar | `qminiwasm.inference.edge.run_edge_cognitive_loop()` |
| N-loop halting, escalation trigger | Same; returns `EdgeOutcome.RESOLVED_LOCAL` or `ESCALATE_TO_CLOUD` |
| Config (N_max_loops, T_conf) | `qminiwasm.config.HierarchicalConfig`, env `N_MAX_LOOPS`, `T_CONF` |
| Edge inference entry | `QMiniWASM.run_edge_inference()` |

## Tier 2 — State Migration and Cloud Ingestion

| White paper concept | Module / API |
|---------------------|--------------|
| Delta compression (diff vs baseline) | `qminiwasm.state.delta_compression.compress_deltas()`, `delta_payload_struct` |
| Escalation payload (no network) | `qminiwasm.inference.escalation.prepare_escalation_payload()` |
| State migration path | `qminiwasm.quantum.interconnect.StateMigrationInterconnect.accept()` |
| HullKVCache, (addr, value) → 2D, convex hull | `qminiwasm.layers.attention.TropicalAttention.ingest_deltas()`, `clear_delta_buffer()` |
| Re-hydration, resume at N+1 | `QMiniWASM.inference_from_escalation(payload, continuation_hidden_states)` |

## Tier 3 — Quantum MoE and Ternary

| White paper concept | Module / API |
|---------------------|--------------|
| QUBO / Ising formulation | `qminiwasm.quantum.qubo.qubo_hamiltonian()`, `qubo_to_ising()`, `build_affinity_from_compressed()` |
| QAOA router | `qminiwasm.quantum.router.HybridQuantumMoE`, `quantum_router_circuit` |
| Ternary experts (STE) | `qminiwasm.layers.ternary.TernaryWASMExpert` |
| Grover / RC Oracle (optional) | `qminiwasm.quantum.ternary_optimizer.GroverTernaryOptimizer`, `grover_ternary_optimizer_stub()` |
| SYCL, ternary packing (5 trits/byte) | `qminiwasm.hardware.sycl_stubs.SYCLHardware.pack_ternary_weights()`, `unpack_ternary_weights()` |

## End-to-End

| Concept | API |
|---------|-----|
| Single entry point | `QMiniWASM.run_hierarchical(wasm_code, func_name, args, continuation_hidden_states=..., config=...)` |
| Config centralization | `qminiwasm.config.HierarchicalConfig.from_env()` |
| WASM traces for training/tests | `DataPipeline.load_wasm_traces(num_traces)` |

## References

- White paper: `docs/hierarchical-inference-architecture.tex`
- Checklist: `docs/hierarchical-inference-checklist.md`
