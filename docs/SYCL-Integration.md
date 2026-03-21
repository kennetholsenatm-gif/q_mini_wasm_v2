# SYCL Hardware Integration Design

This document describes how a future C++/DPCPP SYCL extension would plug into the Q-Mini-WASM hardware layer. The current implementation uses Python stubs in [qminiwasm/hardware/sycl_stubs.py](../qminiwasm/hardware/sycl_stubs.py); the same interface can be backed by a native extension when SYCL runtimes are available.

## Backend selection

The package supports an optional backend switch via environment variable:

- **`SYCL_BACKEND=sycl`** — Prefer a native SYCL backend (e.g. `qminiwasm.hardware.sycl_native`). If that module is not installed or fails to load, fall back to the Python stubs.
- **Unset or `SYCL_BACKEND=stubs`** — Use the built-in Python stubs only (default).

See [qminiwasm/hardware/__init__.py](../qminiwasm/hardware/__init__.py) for the import logic.

## Interface contract

Any SYCL backend (stubs or native) must provide a class compatible with `SYCLHardware`:

| Method | Signature | Purpose |
|--------|-----------|----------|
| `execute_vector_engine` | `(self, kernel: str, data: List[float]) -> List[float]` | Run logic-heavy / SIMD workload on Vector Engine (XVE). |
| `execute_matrix_engine` | `(self, matrix: List[List[float]], weights: List[List[float]]) -> List[List[float]]` | Run dense matrix / DPAS ops on Matrix Engine (XMX). |
| `pack_ternary_weights` | `(self, weights: List[int]) -> bytes` | Pack ternary weights: 5 trits per byte (values -1→0, 0→1, 1→2). |
| `unpack_ternary_weights` | `(self, packed: bytes) -> List[int]` | Unpack bytes to ternary list (-1, 0, 1). |
| `driver_memory_paging` | `(self, memory: List[float], size: int) -> None` | Driver-level memory paging / SVM (stub can no-op). |

The stubs already implement `pack_ternary_weights` and `unpack_ternary_weights` in Python; a native backend may delegate these to C++ for speed or keep the same 5-trits-per-byte format for compatibility.

## Packing format (5 trits/byte)

- Ternary values in Python: **-1, 0, 1**.
- Encoded as trits **0, 1, 2** (e.g. -1→0, 0→1, 1→2).
- Five trits per byte: trit at position `j` (0–4) contributes `trit * 3^j` to the byte value (0–242).
- Byte order: first byte encodes trits 0–4, next byte trits 5–9, etc. Unused trits in the last byte are zero-padded when packing; unpacking produces exactly `ceil(len(weights)/5)*5` trits (caller may slice to original length if needed).

This format is fixed so that Python stubs and a future C++ extension can exchange packed buffers without conversion.

## Future C++ extension (ABI)

A native backend can be implemented as:

1. **Python extension module** (e.g. `sycl_native` or `sycl_backend`) built with pybind11 or ctypes, exposing a class that implements the table above. The extension would link against oneAPI/DPCPP (or Intel oneAPI runtime) and implement:
   - `execute_vector_engine`: Submit SYCL kernel to a queue targeting XVE (e.g. ESIMD or appropriate subgroup size).
   - `execute_matrix_engine`: Use joint-matrix / DPAS APIs for XMX.
   - `driver_memory_paging`: Optional; integrate with SVM or explicit buffer management.
   - `pack_ternary_weights` / `unpack_ternary_weights`: Can reuse the same 5-trits-per-byte layout for interoperability with Python.

2. **Entry point**: The hardware layer will try `from qminiwasm.hardware.sycl_native import SYCLHardware` when `SYCL_BACKEND=sycl`. The module name can be overridden later (e.g. via another env var) if multiple backends exist.

3. **Build**: C++ project can live under `qminiwasm/hardware/sycl/` (or a separate repo) with CMake/Meson, building a shared library loadable as a Python extension. Dependencies: oneAPI DPCPP (or Intel oneAPI Base Toolkit), pybind11.

4. **Fallback**: If the extension is not built or fails to load (e.g. missing SYCL runtime), the package falls back to the existing stubs so that training and inference still run without SYCL.

## References

- [qminiwasm/hardware/sycl_stubs.py](../qminiwasm/hardware/sycl_stubs.py) — Current stub implementation and packing format.
- White paper: SYCL execution hierarchy, XVE/XMX, C for Metal (CM), ternary weight packing.
