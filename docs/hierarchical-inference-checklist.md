# Hierarchical Inference Implementation Checklist

Phases and file paths for maintainers (aligned with the linear implementation plan).

## Phase 1 — Tier 1: Edge cognitive loop and escalation

- [x] **1.1** Cognitive Looping + N-loop halting  
  - `qminiwasm/config.py`: `HierarchicalConfig`, `N_max_loops`, `T_conf`  
  - `qminiwasm/inference/edge.py`: `EdgeOutcome`, `run_edge_cognitive_loop()`, `default_certainty_heuristic`  
  - `qminiwasm/model.py`: `run_edge_inference()`

- [x] **1.2** Epoch-based interruption and real `capture_deltas`  
  - `qminiwasm/wasm/engine.py`: `capture_deltas(instance)` (linear_memory, stack_snapshot, instruction_pointer); `execute()` calls it after run

- [x] **1.3** Escalation trigger and payload interface  
  - `qminiwasm/inference/escalation.py`: `prepare_escalation_payload(captured_deltas, config)`  
  - `qminiwasm/model.py`: sets `last_state["escalation_payload"]` when outcome is `ESCALATE_TO_CLOUD`

## Phase 2 — Tier 2: State migration and cloud ingestion

- [x] **2.1** Delta compression  
  - `qminiwasm/state/delta_compression.py`: `compress_deltas()`, `delta_payload_struct`  
  - `qminiwasm/state/__init__.py`: exports

- [x] **2.2** State migration interconnect  
  - `qminiwasm/quantum/interconnect.py`: `StateMigrationInterconnect`, `accept(payload)` → (addr, value) list

- [x] **2.3** HullKVCache ingest (addr, value)  
  - `qminiwasm/layers/attention.py`: `TropicalAttention.ingest_deltas()`, `clear_delta_buffer()`, forward uses delta prefix

- [x] **2.4** Re-hydration and resume  
  - `qminiwasm/model.py`: `inference_from_escalation(payload, continuation_hidden_states)`, `state_migration`, `tropical_attention`

## Phase 3 — Tier 3: Hardening and optional quantum

- [x] **3.1** QUBO/Ising explicit  
  - `qminiwasm/quantum/qubo.py`: `qubo_hamiltonian()`, `qubo_to_ising()`, `build_affinity_from_compressed()`

- [x] **3.2** Grover/RC Oracle stub  
  - `qminiwasm/quantum/ternary_optimizer.py`: `grover_ternary_optimizer_stub()`, `GroverTernaryOptimizer`

- [x] **3.3** SYCL ternary packing  
  - `qminiwasm/hardware/sycl_stubs.py`: `pack_ternary_weights()` (5 trits/byte), `unpack_ternary_weights()`

## Phase 4 — Integration and population

- [x] **4.1** End-to-end entry point  
  - `qminiwasm/model.py`: `run_hierarchical(wasm_code, func_name, args, continuation_hidden_states=..., config=...)`

- [x] **4.2** Config and constants  
  - `qminiwasm/config.py`: `HierarchicalConfig.from_env()`, env vars documented in architecture doc

- [x] **4.3** Data pipeline and tests  
  - `qminiwasm/data/pipeline.py`: `load_wasm_traces(num_traces)`  
  - `tests/test_hierarchical_inference.py`: Tier 1 loop halt, delta compression, HullKV ingest, escalation payload, run_hierarchical (with wasmtime skip when missing)

- [x] **4.4** Docs  
  - `docs/hierarchical-inference-architecture.md`: white paper → module map  
  - `docs/hierarchical-inference-checklist.md`: this file
