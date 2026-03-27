# Edge bundle layout (`qminiwasm-edge-bundle.zip`)

The training-wui `POST /api/build_artifact` endpoint runs [`scripts/build_tpem_wasm_artifacts.py`](../scripts/build_tpem_wasm_artifacts.py), which writes a directory of artifacts plus a zip archive.

## Directory layout

| File | Role |
|------|------|
| `qminiwasm-kernels.wasm` | Trit helper kernel (wasm32 / i32 linear memory), from `corpus/trit_kernels.wat`. Tier 1 runs `wasm-opt -Oz` when `wasm-opt` is on `PATH`. |
| `qminiwasm-weights.tpem` | Packed ternary weights (`QMWTPEM1` bundle); from `--checkpoint` trainable TPEM or deterministic synthetic bytes (CI). |
| `edge_schema.json` | Tier policy: `enclave_tier`, `use_memory64`, `wasm_memory64_max_mb`, `kernel_wasm_features: ["wasm32"]`, and an explanatory note. **Memory64** values describe **host/runtime** expectations; the kernel module is still wasm32 until a dedicated memory64 WAT is added. |
| `artifact_manifest.json` | Checksums for wasm, tpem, schema, and the payload zip (see [`artifact_contract.schema.json`](../configs/artifacts/artifact_contract.schema.json)). |
| `qminiwasm-edge-bundle.zip` | **Payload-only** zip: `kernels.wasm` + `weights.tpem` + `edge_schema.json`. The manifest is **not** inside the zip so its `edge_bundle.sha256` can be verified without a circular hash. |

## Verification

```bash
python -m qminiwasm.wasm_host.artifact_contract verify dist/edge-artifacts
```

## Web UI download (`training-wui`)

Artifacts under `dist/edge-artifacts/` are **not** served by `/api/artifacts/download` (that route is limited to `artifacts/`). Use:

- **`GET /api/edge_artifacts/download?path=<repo-relative>`** where `path` must resolve under `dist/edge-artifacts/` (for example `dist/edge-artifacts/build-abc/qminiwasm-edge-bundle.zip`).

When the WUI runs with **`--token`**, authenticated browsers should pass the same credential as other API calls: header `X-QMW-WUI-Token`, **`Authorization: Bearer &lt;token&gt;`**, or query **`wui_token`**. Plain `<a href>` downloads in the UI append `wui_token` when the token is stored in session (see Mission / Training dashboard edge bundle links).

The **Deployment dashboard** shows sample **Wasmedge**, **Docker**, and **Incus** commands; treat them as templates and adjust image names, memory limits, and `/dev/dri` device paths for your host.
