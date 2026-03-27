#!/usr/bin/env python3
"""Build edge artifacts: trit kernels (.wasm) + TPEM sidecar (.tpem) + optional zip bundle.

Outputs under ``--out-dir``:
  - ``qminiwasm-kernels.wasm`` — wat2wasm (+ optional ``wasm-opt`` for tier 1, default ``-O3``)
  - ``qminiwasm-weights.tpem`` — packed trits (checkpoint or deterministic synthetic)
  - ``edge_schema.json`` — tier / Memory64 policy hints
  - ``artifact_manifest.json`` — checksums including ``edge_bundle`` (zip) and ``edge_bundle_tpem`` (same bytes, ``.tpem`` name)
  - ``qminiwasm-edge-bundle.zip`` — wasm + tpem + edge_schema (no manifest inside; avoids hash cycle)
  - ``qminiwasm-edge-bundle.tpem`` — duplicate of the zip for operator-facing “TPEM bundle” downloads
"""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))


BUNDLE_ZIP_NAME = "qminiwasm-edge-bundle.zip"
BUNDLE_TPEM_NAME = "qminiwasm-edge-bundle.tpem"
EDGE_SCHEMA_NAME = "edge_schema.json"
_WASM_OPT_LEVELS = frozenset({"O0", "O1", "O2", "O3", "Os", "Oz"})


def _maybe_wasm_opt_tier1(wasm_bytes: bytes, tier: int, opt_level: str | None) -> bytes:
    """Tier 1: run ``wasm-opt -<level>`` when available; otherwise return input bytes."""
    if tier != 1:
        return wasm_bytes
    level = (opt_level or "O3").strip()
    if level not in _WASM_OPT_LEVELS:
        print(f"invalid wasm-opt level {level!r}; skipping", file=sys.stderr)
        return wasm_bytes
    if not shutil.which("wasm-opt"):
        print("wasm-opt not on PATH; skipping tier-1 optimization", file=sys.stderr)
        return wasm_bytes
    with tempfile.TemporaryDirectory(prefix="qmw_wasm_opt_") as td:
        raw = Path(td) / "in.wasm"
        out = Path(td) / "out.wasm"
        raw.write_bytes(wasm_bytes)
        flag = "-" + level
        try:
            subprocess.run(
                ["wasm-opt", flag, str(raw), "-o", str(out)],
                check=True,
                capture_output=True,
                text=True,
            )
        except (subprocess.CalledProcessError, FileNotFoundError) as e:
            print(f"wasm-opt failed ({e}); using unoptimized wasm", file=sys.stderr)
            return wasm_bytes
        return out.read_bytes()


def _load_packed_weights_from_checkpoint(checkpoint: Path) -> bytes:
    import torch

    from qminiwasm.wasm_host.trit_pack import pack_ternary_tensor

    blob = torch.load(str(checkpoint), map_location="cpu", weights_only=False)
    if not isinstance(blob, dict):
        raise ValueError(f"checkpoint must be a dict trainable TPEM, got {type(blob).__name__}")
    te = blob.get("ternary_expert")
    if not isinstance(te, dict):
        raise ValueError("checkpoint missing ternary_expert state_dict")
    w = te.get("weight")
    if w is None:
        raise ValueError("checkpoint ternary_expert missing 'weight'")
    if not isinstance(w, torch.Tensor):
        w = torch.as_tensor(w, dtype=torch.float32)
    packed = pack_ternary_tensor(w)
    return bytes(int(x) for x in packed.cpu().flatten().tolist())


def _edge_schema(tier: int) -> dict:
    from qminiwasm.config import ENCLAVE_TIER_PRESETS, normalize_enclave_tier_value

    name = normalize_enclave_tier_value(tier)
    if name is None:
        raise ValueError(f"invalid tier {tier}")
    preset = ENCLAVE_TIER_PRESETS[name]
    tier_num = int(preset.tier_number)
    use_m64 = bool(preset.memory64_required) or tier >= 3
    m64_mb = float(preset.default_memory64_max_mb) if preset.default_memory64_max_mb is not None else None
    if not use_m64:
        m64_mb = None
    return {
        "enclave_tier": name,
        "enclave_tier_number": tier_num,
        "use_memory64": use_m64,
        "wasm_memory64_max_mb": m64_mb,
        "kernel_wasm_features": ["wasm32"],
        "notes": (
            "Trit kernel module is wasm32 (i32 linear memory). use_memory64 / "
            "wasm_memory64_max_mb describe host runtime policy; a dedicated "
            "memory64 kernel module is not shipped in this build."
        ),
    }


def _write_payload_zip(out: Path, paths: list[tuple[Path, str]]) -> tuple[str, int]:
    zpath = out / BUNDLE_ZIP_NAME
    with zipfile.ZipFile(zpath, "w", compression=zipfile.ZIP_DEFLATED) as zf:
        for fs_path, arc in paths:
            zf.write(fs_path, arcname=arc)
    raw = zpath.read_bytes()
    return hashlib.sha256(raw).hexdigest(), len(raw)


def main() -> int:
    ap = argparse.ArgumentParser(description="Build qminiwasm .wasm + .tpem edge artifacts")
    ap.add_argument(
        "--out-dir",
        type=Path,
        default=REPO_ROOT / "dist" / "edge-artifacts",
        help="Output directory (created if missing)",
    )
    ap.add_argument(
        "--tier",
        type=int,
        default=2,
        help="Enclave tier 1-5 (default 2 meso; tier 1 runs wasm-opt)",
    )
    ap.add_argument(
        "--wasm-opt-level",
        type=str,
        default="O3",
        metavar="LEVEL",
        help="wasm-opt pass for tier 1 only (O0–O4, Os, Oz). Default O3 (aggressive speed opt).",
    )
    ap.add_argument(
        "--checkpoint",
        type=Path,
        default=None,
        help="Trainable TPEM .pt path; if omitted, use deterministic synthetic weights (CI)",
    )
    args = ap.parse_args()

    if args.tier < 1 or args.tier > 5:
        print("--tier must be 1-5", file=sys.stderr)
        return 2

    out: Path = args.out_dir.resolve()
    out.mkdir(parents=True, exist_ok=True)

    from qminiwasm.wasm_host.artifact_contract import verify_artifact_contract
    from qminiwasm.wasm_host.trit_pack import PACK_ENCODING_VERSION, pack_ternary_list
    from qminiwasm.wasm_host.trit_wasm_runtime import compile_trit_kernels_wasm
    from qminiwasm.wasm_host.artifact_contract import verify_artifact_contract
    from qminiwasm.wasm_host.tpem_bundle import BUNDLE_FORMAT_VERSION, write_tpem_bundle

    wasm_path = out / "qminiwasm-kernels.wasm"
    wasm_bytes = compile_trit_kernels_wasm()
    wasm_bytes = _maybe_wasm_opt_tier1(wasm_bytes, int(args.tier), str(args.wasm_opt_level or "O3"))
    wasm_path.write_bytes(wasm_bytes)
    wasm_digest = hashlib.sha256(wasm_bytes).hexdigest()

    if args.checkpoint is not None:
        ck = args.checkpoint.resolve()
        if not ck.is_file():
            print(f"checkpoint not found: {ck}", file=sys.stderr)
            return 1
        try:
            payload = _load_packed_weights_from_checkpoint(ck)
        except Exception as e:
            print(f"failed to load checkpoint weights: {e}", file=sys.stderr)
            return 1
    else:
        canonical_block = [-1, 0, 1, 1, 0]
        trits = (canonical_block * 127)[:635]
        payload = pack_ternary_list(trits)

    tpem_path = out / "qminiwasm-weights.tpem"
    write_tpem_bundle(tpem_path, payload)
    payload_sha256 = hashlib.sha256(payload).hexdigest()

    schema_obj = _edge_schema(int(args.tier))
    schema_path = out / EDGE_SCHEMA_NAME
    schema_path.write_text(json.dumps(schema_obj, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    schema_bytes = schema_path.read_bytes()
    schema_digest = hashlib.sha256(schema_bytes).hexdigest()
    schema_len = len(schema_bytes)

    zip_digest, zip_len = _write_payload_zip(
        out,
        [
            (wasm_path, wasm_path.name),
            (tpem_path, tpem_path.name),
            (schema_path, schema_path.name),
        ],
    )

    bundle_tpem_path = out / BUNDLE_TPEM_NAME
    shutil.copyfile(out / BUNDLE_ZIP_NAME, bundle_tpem_path)

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
            "edge_schema": {
                "path": schema_path.name,
                "sha256": schema_digest,
                "bytes": schema_len,
            },
            "edge_bundle": {
                "path": BUNDLE_ZIP_NAME,
                "sha256": zip_digest,
                "bytes": zip_len,
            },
            "edge_bundle_tpem": {
                "path": BUNDLE_TPEM_NAME,
                "sha256": zip_digest,
                "bytes": zip_len,
            },
        }
    }
    manifest_path = out / "artifact_manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    verify_artifact_contract(out)

    verify_artifact_contract(out)

    print(f"Wrote {wasm_path} ({len(wasm_bytes)} bytes, sha256={wasm_digest})")
    print(f"Wrote {tpem_path} (payload {len(payload)} bytes, sha256={payload_sha256})")
    print(f"Wrote {schema_path}")
    print(f"Wrote {manifest_path}")
    print(f"Wrote {out / BUNDLE_ZIP_NAME} (sha256={zip_digest})")
    print(f"Wrote {bundle_tpem_path} (same payload as zip, sha256={zip_digest})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
