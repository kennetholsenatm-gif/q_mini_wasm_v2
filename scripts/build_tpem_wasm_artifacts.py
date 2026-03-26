#!/usr/bin/env python3
"""Build deterministic CI/release artifacts: trit kernels (.wasm) + TPEM sidecar (.tpem).

Outputs under ``--out-dir``:
  - ``qminiwasm-kernels.wasm`` — from :func:`qminiwasm.wasm_host.trit_wasm_runtime.compile_trit_kernels_wasm`
  - ``qminiwasm-weights.tpem`` — packed trits + :func:`qminiwasm.wasm_host.tpem_bundle.write_tpem_bundle`
  - ``artifact_manifest.json`` — format versions and SHA-256 of the TPEM payload
"""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))


def main() -> int:
    ap = argparse.ArgumentParser(description="Build qminiwasm .wasm + .tpem artifacts")
    ap.add_argument(
        "--out-dir",
        type=Path,
        default=REPO_ROOT / "dist" / "edge-artifacts",
        help="Output directory (created if missing)",
    )
    args = ap.parse_args()
    out: Path = args.out_dir.resolve()
    out.mkdir(parents=True, exist_ok=True)

    from qminiwasm.wasm_host.trit_pack import PACK_ENCODING_VERSION, pack_ternary_list
    from qminiwasm.wasm_host.trit_wasm_runtime import compile_trit_kernels_wasm
    from qminiwasm.wasm_host.artifact_contract import verify_artifact_contract
    from qminiwasm.wasm_host.tpem_bundle import BUNDLE_FORMAT_VERSION, write_tpem_bundle

    wasm_path = out / "qminiwasm-kernels.wasm"
    wasm_bytes = compile_trit_kernels_wasm()
    wasm_path.write_bytes(wasm_bytes)
    wasm_digest = hashlib.sha256(wasm_bytes).hexdigest()

    # Deterministic canonical trit payload (repeat a fixed 5-trit block; multiple bytes).
    canonical_block = [-1, 0, 1, 1, 0]
    trits = (canonical_block * 127)[:635]  # 127 bytes at 5 trits/byte
    payload = pack_ternary_list(trits)
    tpem_path = out / "qminiwasm-weights.tpem"
    write_tpem_bundle(tpem_path, payload)
    payload_sha256 = hashlib.sha256(payload).hexdigest()

    manifest = {
        "artifacts": {
            "kernels_wasm": {
                "path": wasm_path.name,
                "sha256": wasm_digest,
                "bytes": len(wasm_bytes),
            },
            "weights_tpem": {
                "path": tpem_path.name,
                "payload_sha256": payload_sha256,
                "payload_bytes": len(payload),
                "bundle_format_version": BUNDLE_FORMAT_VERSION,
                "pack_encoding_version": PACK_ENCODING_VERSION,
            },
        }
    }
    manifest_path = out / "artifact_manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    verify_artifact_contract(out)

    print(f"Wrote {wasm_path} ({len(wasm_bytes)} bytes, sha256={wasm_digest})")
    print(f"Wrote {tpem_path} (payload {len(payload)} bytes, sha256={payload_sha256})")
    print(f"Wrote {manifest_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
