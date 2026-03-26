# Ternary-Packed Memory Enclave (TPEM) on-disk artifact

This document defines the **sidecar binary** format for static packed trits shipped alongside a `.wasm` module. It does **not** change the mathematical packing inside the payload; that layout is defined in code as `PACK_ENCODING_VERSION` in [`qminiwasm/enclave/trit_pack.py`](../qminiwasm/enclave/trit_pack.py) (currently **2**, MSB-first, five signed trits per byte).

## Role

| Artifact | Purpose |
|----------|---------|
| `.wasm` | Logic / exports (mesh curriculum, inference glue, etc.) |
| `.tpem` (or agreed extension) | Immutable **packed weight blob** loaded into WASM **linear memory** as part of **Enclave Footprint (EF)** accounting |

Load order and runtime caps are governed by [`configs/serve/default.toml`](../configs/serve/default.toml) and [`HierarchicalConfig`](../qminiwasm/config.py) (`enclave_footprint_mb`, `wasm_memory64_max_mb`, `max_linear_memory_pages`, etc.).

## File layout (little-endian)

All integers are **little-endian**. Total file size = **56 + N** bytes where **N** = payload length.

| Offset | Size | Field |
|--------|------|--------|
| 0 | 8 | **Magic** ASCII `QMWTPEM1` (0x51 0x4D 0x57 0x54 0x50 0x45 0x4D 0x31) |
| 8 | 4 | **bundle_format_version** (`uint32`) — container version; currently **1** |
| 12 | 4 | **pack_encoding_version** (`uint32`) — must match `trit_pack.PACK_ENCODING_VERSION` for the payload |
| 16 | 8 | **payload_byte_length** (`uint64`) — **N** |
| 24 | 32 | **payload_sha256** — SHA-256 digest over the **N** payload bytes |
| 56 | N | **payload** — packed bytes (e.g. output of `pack_ternary_list` / `pack_ternary_tensor` pipeline) |

## Validation

Readers **must**:

1. Verify the magic.
2. Check `payload_byte_length` matches the remaining file size.
3. Recompute SHA-256 over the payload and compare to `payload_sha256`.
4. Refuse load if `pack_encoding_version` is unknown to the reader.

## Artifact Contract Gate

Enclave-as-Code groundwork adds a manifest contract gate for edge artifacts:

- Schema: `configs/artifacts/artifact_contract.schema.json`
- Verifier: `python -m qminiwasm.wasm_host.artifact_contract verify dist/edge-artifacts`
- Build integration: `scripts/build_tpem_wasm_artifacts.py` runs contract verification after writing artifacts.
- CI integration: `.github/workflows/ci.yml` runs contract verification as a fail-fast check.

## Reference implementation

Python: [`qminiwasm/enclave/tpem_bundle.py`](../qminiwasm/enclave/tpem_bundle.py) — `write_tpem_bundle`, `read_tpem_bundle`, CLI `python -m qminiwasm.enclave.tpem_bundle verify <path>`.

## Versioning

- Bump **bundle_format_version** if this header ever changes.
- Bump **pack_encoding_version** in `trit_pack.py` only when the **payload** bit layout changes; document migration in release notes.
