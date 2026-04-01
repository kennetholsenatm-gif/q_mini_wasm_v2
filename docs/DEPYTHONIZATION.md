# Depythonization (C++ / Go only)

This repository is moving to a **Go + C++ operator and developer surface**. Interpreter-based tooling and docs that implied a parallel “Python product path” are **being deleted or ported**—there is **no** commitment to keep a Python install or package as part of the golden path.

## Principles

- **Training:** Training WUI + `qmw-grpc-train` + C++ `TrainingEngineService` only.
- **Inference (target):** Go serve + **C++ tensor RPC** (HTTP infer today returns **501** until bridged).
- **Routing / fleet:** Native [`cpp/router`](../cpp/router), [`cpp/escalation`](../cpp/escalation), [`cpp/wasm` hooks](../cpp/wasm) — not an interpreter loop.

## Inventory (remove or replace)

| Area | Path / artifact | Target | Status |
|------|-----------------|--------|--------|
| Package | [`pyproject.toml`](../pyproject.toml), [`setup.py`](../setup.py) | Remove when CI no longer needs them | Pending |
| Library | [`qminiwasm/`](../qminiwasm/) | Port needed contracts to Go/C++; delete the rest | In Progress |
| Tests | [`tests/`](../tests/) `test_*.py` | `go test ./...`, `qminiwasm_cpp_tests`, or delete | Pending |
| Scripts | [`scripts/`](../scripts/) `*.py` | Go or shell replacements | Pending |
| Sidecar | [`sidecar/quantum/`](../sidecar/quantum/) | Optional native or standalone binary contract | Pending |
| Serverless | [`serverless/handler.py`](../serverless/handler.py) | Go handler or containerized native binary | Pending |
| CI | Workflows invoking `pip` / `pytest` | Native / Go jobs only | Pending |

### Completed Depythonization

| Module | Status | Notes |
|--------|--------|-------|
| `qminiwasm/wasm_host/trit_pack.py` | ✅ Done | Updated to use `qminiwasm_cpp_native` with fallback to `_native_ternary` |
| `qminiwasm/wasm_host/memory_encode.py` | ✅ Done | Updated to use `qminiwasm_cpp_native` with fallback to `_native_ternary` |
| `qminiwasm/native_bridge.py` | ✅ Done | Updated to check for `qminiwasm_cpp_native` first, then fallback to `_native_ternary` |

### Native C++ (No Python)

The `qminiwasm_cpp_native` module provides **legacy-only** native implementations:
- `pack_ternary_msb` / `unpack_ternary_msb` - Ternary weight packing (MSB-first)
- `encode_linear_memory_u8` - WASM linear memory encoding
- `matvec_best` - SIMD-accelerated matrix-vector operations
- `cpu_has_avx512f` / `cpu_has_avx512vnni` - CPU feature detection
- `abi_version` - Native ABI version

### Qutrit Training Service (Native C++)

The `QutritTrainingService` provides gRPC interface for Qutrit Clifford training:
- Proto: [`proto/qutrit_training.proto`](../proto/qutrit_training.proto)
- C++ Service: [`cpp/quantum/qutrit_training_service.hpp`](../cpp/quantum/qutrit_training_service.hpp)
- Go Client: [`training-wui/qutrittraining/client.go`](../training-wui/qutrittraining/client.go)
- Build: `scripts/start-qutrit-training-service.ps1` or `scripts/build-qutrit-training-service.ps1`

The service implements the three-phase Qutrit Clifford training paradigm:
1. **Phase 1: Quantum Superposition** - Initialize parameters in superposition
2. **Phase 2: Entanglement-Based Optimization** - Apply CZ₃ gates for parameter correlations
3. **Phase 3: Stabilizer Tableau Engine** - Discrete algebraic phase updates

RPC Methods:
- `InitializeSuperposition` - Create tableau with parameters in superposition
- `ApplyHadamardLayer` - Apply H₃ gates to create superposition
- `LatticeCollapse` - Projective measurement for weight finalization
- `ApplyControlledZ` - Create parameter correlations via CZ₃
- `PushNoisePhase` - Push noise for error tracking
- `ExtractErrorSyndrome` - Extract error syndrome for noise handling
- `UpdatePhaseTableau` - Discrete algebraic phase updates
- `GetTrainingMetrics` - Monitor training progress
- `GetTableauState` - Inspect tableau state

**Qutrit Clifford modules** have been moved to **gRPC service** (no Python bindings):
- Proto: [`proto/qutrit_clifford.proto`](../proto/qutrit_clifford.proto)
- C++ Service: [`cpp/quantum/qutrit_clifford_service.hpp`](../cpp/quantum/qutrit_clifford_service.hpp)
- Go Client: [`training-wui/qutrit/client.go`](../training-wui/qutrit/client.go)

**Qutrit Training Engine** (new paradigm based on Clifford mechanics):
- Proto: [`proto/qutrit_training.proto`](../proto/qutrit_training.proto)
- C++ Service: [`cpp/quantum/qutrit_training_service.hpp`](../cpp/quantum/qutrit_training_service.hpp)
- Go Client: [`training-wui/qutrittraining/client.go`](../training-wui/qutrittraining/client.go)
- Research: [`docs/research/Qutrit Clifford Training for QMINIWASM.md`](../docs/research/Qutrit Clifford Training for QMINIWASM.md)

Update this table as directories disappear.

## Docs

- Root [README](../README.md) and [JOURNEY_OF_A_VECTOR](architecture/JOURNEY_OF_A_VECTOR.md) must **not** cite `pip`, `qminiwasm`, or interpreter training as primary.
- Pages that still mention Python should be **rewritten or archived** until grep-clean.

## Verification

```bash
# From repo root: should trend to zero matches under docs/ and README
rg -i "pip install|qminiwasm\\.model|python -m qminiwasm" docs README.md wiki
```
