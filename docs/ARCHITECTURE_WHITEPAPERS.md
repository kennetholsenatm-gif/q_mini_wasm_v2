# Whitepaper architecture index

This index links the repository to the three architecture whitepapers and records implementation status. Canonical **Unified Training Matrix** phase names for TOML and logs: `sft`, `progressive_quant`, `mopd`, `router_cispo` (see `[[training_phases]]` in training TOML and [configs/training/schema.toml](training/schema.toml)).

## Documents

| Document | Path |
|----------|------|
| Ternary Quantum AI Edge Research | [Ternary Quantum AI Edge Research.md](Ternary%20Quantum%20AI%20Edge%20Research.md) |
| Advanced Architecture Synthesis | [QMINIWASM_ Advanced Architecture Synthesis.md](QMINIWASM_%20Advanced%20Architecture%20Synthesis.md) |
| Ternary Clifford Optimization | [QMINIWASM_ Ternary Clifford Optimization.md](QMINIWASM_%20Ternary%20Clifford%20Optimization.md) |

## Ternary Clifford Optimization (pillar traceability)

| Paper pillar | Repo mapping | Status |
|--------------|--------------|--------|
| Ternary stabilizer / Pauli frame | [qminiwasm/quantum/clifford/](../qminiwasm/quantum/clifford/) (tableau stub); [qminiwasm/wasm_host/trit_pack.py](../qminiwasm/wasm_host/trit_pack.py) for 5-trit packing as a natural wire format for serialized tableaux (TPEM / WASM host narrative) | Partial (Python stub; serialization not wired) |
| Clifford clustering / T-depth compiler | [qminiwasm/fabric/router.py](../qminiwasm/fabric/router.py) (routing / QAOA path only); no ZX / TODD DAG | Not started |
| CISPO router for WASM vs QPU cost asymmetry | [qminiwasm/rl/cascade_cispo.py](../qminiwasm/rl/cascade_cispo.py), [qminiwasm/training/cascade_rl.py](../qminiwasm/training/cascade_rl.py), `[[training_phases]]` / MOPD; native `TelemetryEvent.cascade_policy_optimizer` (gRPC + WUI) for segment visibility | Implemented (toy MDP); production router graph TBD |
| Magic state / stabilizer–QPU handoff | — | Not started |

## Traceability matrix

| Topic | Primary modules | Status |
|-------|-----------------|--------|
| 1.58-bit ternary experts / STE | [qminiwasm/layers/ternary.py](../qminiwasm/layers/ternary.py) | Implemented |
| 5-trit TPEM packing / WASM host | [qminiwasm/wasm_host/trit_pack.py](../qminiwasm/wasm_host/trit_pack.py), [qminiwasm/wasm_host/engine.py](../qminiwasm/wasm_host/engine.py) | Partial (packing; zero-copy JS boundary is incremental) |
| Tropical geometry / max-plus attention | [qminiwasm/layers/attention.py](../qminiwasm/layers/attention.py) | Implemented (HullKV / tropical attention) |
| BlochSphere / fidelity attention (classical) | [qminiwasm/layers/bloch_attention.py](../qminiwasm/layers/bloch_attention.py) | Implemented (Mode A; QPU compute-uncompute is future work) |
| MoE / QAOA routing | [qminiwasm/fabric/router.py](../qminiwasm/fabric/router.py) | Partial |
| Cascade RL (toy MDP) | [qminiwasm/training/cascade_rl.py](../qminiwasm/training/cascade_rl.py) | Implemented |
| GRPO-style group baseline | [qminiwasm/rl/cascade_grpo.py](../qminiwasm/rl/cascade_grpo.py) | Implemented |
| CISPO (clipped IS weight + detach) | [qminiwasm/rl/cascade_cispo.py](../qminiwasm/rl/cascade_cispo.py) | Implemented |
| MOPD feature loss | [qminiwasm/training/distillation.py](../qminiwasm/training/distillation.py) | Implemented |
| Unified Training Matrix phases | [qminiwasm/training/phases.py](../qminiwasm/training/phases.py), [qminiwasm/training/loop.py](../qminiwasm/training/loop.py), `[[training_phases]]` TOML | Implemented |
| Native gRPC / LibTorch training config | [proto/training_engine.proto](../proto/training_engine.proto), [training-wui/trainingconfig](../training-wui/trainingconfig), [cpp/training/src/grpc/training_engine_service.cpp](../cpp/training/src/grpc/training_engine_service.cpp), [cpp/training/src/training_engine.cpp](../cpp/training/src/training_engine.cpp), [cpp/training/src/libtorch_ternary_trainer.cpp](../cpp/training/src/libtorch_ternary_trainer.cpp) | Partial (`attention_backend=bloch` still fails fast). Unified-matrix RL-only phases (`supervised=false`, `cascade_rl=true`): native toy MDP + `CascadeToyPolicy` with **GRPO** or **CISPO** ([`CascadeGRPO`](../qminiwasm/rl/cascade_grpo.py) / [`CascadeCISPO`](../qminiwasm/rl/cascade_cispo.py) parity); `cascade_rl=true` with `supervised=true` still uses MSE `train_step` on TPEM (no joint SFT+RL in one step yet) |
| Native telemetry: `training_phase` / `cascade_policy_optimizer` | [proto/training_engine.proto](../proto/training_engine.proto) `TelemetryEvent` fields 29–30; [training-wui/telemetry_grpc_payload.go](../training-wui/telemetry_grpc_payload.go) WebSocket `metric` JSON; C++ [cpp/training/src/training_engine.cpp](../cpp/training/src/training_engine.cpp) `emit` + [cpp/training/src/training_phases.cpp](../cpp/training/src/training_phases.cpp) when `[[training_phases]]` is non-empty and `cascade_curriculum_loop` is off | Implemented (canonical phase names from TOML; `training_phase` left empty during native cascade-only loop; root `cascade_policy_optimizer` still emitted) |
| Unified matrix epoch to phase (C++) | [cpp/training/src/training_phases.cpp](../cpp/training/src/training_phases.cpp) `phase_at_global_epoch`, `effective_cascade_policy` | Implemented |
| Progressive quantization hook | Per-phase `tequila_deadzone` in `[[training_phases]]` | Implemented |
| Compute-uncompute / QPU fidelity attention | — | Not started (document in Bloch module docstring) |

## Configuration

- Training tables: [configs/training/schema.toml](training/schema.toml), validated by [qminiwasm/engine/training_schema.py](../qminiwasm/engine/training_schema.py).
- Policy: prefer TOML over new `QMINIWASM_*` knobs; see [CONFIGURATION_POLICY.md](CONFIGURATION_POLICY.md).
- [QMINIWASM_ Advanced Architecture Synthesis.md](QMINIWASM_%20Advanced%20Architecture%20Synthesis.md) curriculum and CISPO narrative (e.g. §2.3–2.4) maps to the same `[[training_phases]]` names and to native `training_phase` / `cascade_policy_optimizer` telemetry when using the gRPC LibTorch engine.
