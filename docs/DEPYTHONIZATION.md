# Depythonization (C++ / Go only)

This repository is moving to a **Go + C++ operator and developer surface**. Interpreter-based tooling and docs that implied a parallel “Python product path” are **being deleted or ported**—there is **no** commitment to keep a Python install or package as part of the golden path.

## Principles

- **Training:** Training WUI + `qmw-grpc-train` + C++ `TrainingEngineService` only.
- **Inference (target):** Go serve + **C++ tensor RPC** (HTTP infer today returns **501** until bridged).
- **Routing / fleet:** Native [`cpp/router`](../cpp/router), [`cpp/escalation`](../cpp/escalation), [`cpp/wasm` hooks](../cpp/wasm) — not an interpreter loop.

## Inventory (remove or replace)

| Area | Path / artifact | Target |
|------|-----------------|--------|
| Package | [`pyproject.toml`](../pyproject.toml), [`setup.py`](../setup.py) | Remove when CI no longer needs them |
| Library | [`qminiwasm/`](../qminiwasm/) | Port needed contracts to Go/C++; delete the rest |
| Tests | [`tests/`](../tests/) `test_*.py` | `go test ./...`, `qminiwasm_cpp_tests`, or delete |
| Scripts | [`scripts/`](../scripts/) `*.py` | Go or shell replacements |
| Sidecar | [`sidecar/quantum/`](../sidecar/quantum/) | Optional native or standalone binary contract |
| Serverless | [`serverless/handler.py`](../serverless/handler.py) | Go handler or containerized native binary |
| CI | Workflows invoking `pip` / `pytest` | Native / Go jobs only |

Update this table as directories disappear.

## Docs

- Root [README](../README.md) and [JOURNEY_OF_A_VECTOR](architecture/JOURNEY_OF_A_VECTOR.md) must **not** cite `pip`, `qminiwasm`, or interpreter training as primary.
- Pages that still mention Python should be **rewritten or archived** until grep-clean.

## Verification

```bash
# From repo root: should trend to zero matches under docs/ and README
rg -i "pip install|qminiwasm\\.model|python -m qminiwasm" docs README.md wiki
```
