"""Artifact contract verifier for deterministic edge artifacts."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path
from typing import Any

from .tpem_bundle import read_tpem_bundle

_HEX64_RE = re.compile(r"^[0-9a-f]{64}$")


def _read_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def _validate_manifest_shape(manifest: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    artifacts = manifest.get("artifacts")
    if not isinstance(artifacts, dict):
        return ["manifest.artifacts must be an object"]
    for k in ("kernels_wasm", "weights_tpem"):
        if k not in artifacts or not isinstance(artifacts[k], dict):
            errors.append(f"manifest.artifacts.{k} must be an object")
    if errors:
        return errors
    kernels = artifacts["kernels_wasm"]
    weights = artifacts["weights_tpem"]
    for k in ("path", "sha256", "bytes"):
        if k not in kernels:
            errors.append(f"manifest.artifacts.kernels_wasm.{k} is required")
    for k in (
        "path",
        "payload_sha256",
        "payload_bytes",
        "bundle_format_version",
        "pack_encoding_version",
    ):
        if k not in weights:
            errors.append(f"manifest.artifacts.weights_tpem.{k} is required")
    edge_schema = artifacts.get("edge_schema")
    if edge_schema is not None:
        if not isinstance(edge_schema, dict):
            errors.append("manifest.artifacts.edge_schema must be an object")
        else:
            for k in ("path", "sha256", "bytes"):
                if k not in edge_schema:
                    errors.append(f"manifest.artifacts.edge_schema.{k} is required")
    edge_bundle = artifacts.get("edge_bundle")
    if edge_bundle is not None:
        if not isinstance(edge_bundle, dict):
            errors.append("manifest.artifacts.edge_bundle must be an object")
        else:
            for k in ("path", "sha256", "bytes"):
                if k not in edge_bundle:
                    errors.append(f"manifest.artifacts.edge_bundle.{k} is required")
    edge_bundle_tpem = artifacts.get("edge_bundle_tpem")
    if edge_bundle_tpem is not None:
        if not isinstance(edge_bundle_tpem, dict):
            errors.append("manifest.artifacts.edge_bundle_tpem must be an object")
        else:
            for k in ("path", "sha256", "bytes"):
                if k not in edge_bundle_tpem:
                    errors.append(f"manifest.artifacts.edge_bundle_tpem.{k} is required")
    if "sha256" in kernels and not _HEX64_RE.match(str(kernels["sha256"])):
        errors.append("manifest.artifacts.kernels_wasm.sha256 must be 64 lowercase hex chars")
    if "payload_sha256" in weights and not _HEX64_RE.match(str(weights["payload_sha256"])):
        errors.append(
            "manifest.artifacts.weights_tpem.payload_sha256 must be 64 lowercase hex chars"
        )
    if isinstance(edge_schema, dict) and "sha256" in edge_schema:
        if not _HEX64_RE.match(str(edge_schema["sha256"])):
            errors.append("manifest.artifacts.edge_schema.sha256 must be 64 lowercase hex chars")
    if isinstance(edge_bundle, dict) and "sha256" in edge_bundle:
        if not _HEX64_RE.match(str(edge_bundle["sha256"])):
            errors.append("manifest.artifacts.edge_bundle.sha256 must be 64 lowercase hex chars")
    if isinstance(edge_bundle_tpem, dict) and "sha256" in edge_bundle_tpem:
        if not _HEX64_RE.match(str(edge_bundle_tpem["sha256"])):
            errors.append(
                "manifest.artifacts.edge_bundle_tpem.sha256 must be 64 lowercase hex chars"
            )
    return errors


def verify_artifact_contract(
    artifacts_dir: str | Path,
    *,
    manifest_name: str = "artifact_manifest.json",
) -> None:
    base = Path(artifacts_dir)
    manifest_path = base / manifest_name
    if not manifest_path.is_file():
        raise ValueError(f"manifest not found: {manifest_path}")
    manifest = _read_json(manifest_path)
    shape_errors = _validate_manifest_shape(manifest)
    if shape_errors:
        raise ValueError("; ".join(shape_errors))

    artifacts = manifest["artifacts"]
    kernels_meta = artifacts["kernels_wasm"]
    kernels_path = base / str(kernels_meta["path"])
    if not kernels_path.is_file():
        raise ValueError(f"kernels wasm missing: {kernels_path}")
    kernels_bytes = kernels_path.read_bytes()
    if len(kernels_bytes) != int(kernels_meta["bytes"]):
        raise ValueError(
            f"kernels wasm byte mismatch: expected {kernels_meta['bytes']} got {len(kernels_bytes)}"
        )
    kernels_sha = hashlib.sha256(kernels_bytes).hexdigest()
    if kernels_sha != str(kernels_meta["sha256"]):
        raise ValueError("kernels wasm sha256 mismatch")

    tpem_meta = artifacts["weights_tpem"]
    tpem_path = base / str(tpem_meta["path"])
    if not tpem_path.is_file():
        raise ValueError(f"weights tpem missing: {tpem_path}")
    bundle = read_tpem_bundle(tpem_path)
    if len(bundle.payload) != int(tpem_meta["payload_bytes"]):
        raise ValueError(
            f"tpem payload byte mismatch: expected {tpem_meta['payload_bytes']} got {len(bundle.payload)}"
        )
    if bundle.payload_sha256.hex() != str(tpem_meta["payload_sha256"]):
        raise ValueError("tpem payload sha256 mismatch")
    if int(bundle.bundle_format_version) != int(tpem_meta["bundle_format_version"]):
        raise ValueError("tpem bundle_format_version mismatch")
    if int(bundle.pack_encoding_version) != int(tpem_meta["pack_encoding_version"]):
        raise ValueError("tpem pack_encoding_version mismatch")

    es_meta = artifacts.get("edge_schema")
    if isinstance(es_meta, dict):
        es_path = base / str(es_meta["path"])
        if not es_path.is_file():
            raise ValueError(f"edge_schema missing: {es_path}")
        es_bytes = es_path.read_bytes()
        if len(es_bytes) != int(es_meta["bytes"]):
            raise ValueError(
                f"edge_schema byte mismatch: expected {es_meta['bytes']} got {len(es_bytes)}"
            )
        es_sha = hashlib.sha256(es_bytes).hexdigest()
        if es_sha != str(es_meta["sha256"]):
            raise ValueError("edge_schema sha256 mismatch")

    eb_meta = artifacts.get("edge_bundle")
    eb_sha = ""
    if isinstance(eb_meta, dict):
        eb_path = base / str(eb_meta["path"])
        if not eb_path.is_file():
            raise ValueError(f"edge_bundle missing: {eb_path}")
        eb_bytes = eb_path.read_bytes()
        if len(eb_bytes) != int(eb_meta["bytes"]):
            raise ValueError(
                f"edge_bundle byte mismatch: expected {eb_meta['bytes']} got {len(eb_bytes)}"
            )
        eb_sha = hashlib.sha256(eb_bytes).hexdigest()
        if eb_sha != str(eb_meta["sha256"]):
            raise ValueError("edge_bundle sha256 mismatch")

    ebt_meta = artifacts.get("edge_bundle_tpem")
    if isinstance(ebt_meta, dict):
        ebt_path = base / str(ebt_meta["path"])
        if not ebt_path.is_file():
            raise ValueError(f"edge_bundle_tpem missing: {ebt_path}")
        ebt_bytes = ebt_path.read_bytes()
        if len(ebt_bytes) != int(ebt_meta["bytes"]):
            raise ValueError(
                f"edge_bundle_tpem byte mismatch: expected {ebt_meta['bytes']} got {len(ebt_bytes)}"
            )
        ebt_sha = hashlib.sha256(ebt_bytes).hexdigest()
        if ebt_sha != str(ebt_meta["sha256"]):
            raise ValueError("edge_bundle_tpem sha256 mismatch")
        if eb_sha and ebt_sha != eb_sha:
            raise ValueError("edge_bundle_tpem must match edge_bundle checksums")


def main() -> int:
    ap = argparse.ArgumentParser(description="Verify qminiwasm artifact contract.")
    sub = ap.add_subparsers(dest="command", required=True)
    verify = sub.add_parser(
        "verify", help="Verify artifact manifest, checksums, and TPEM contract."
    )
    verify.add_argument(
        "artifacts_dir",
        type=Path,
        nargs="?",
        default=Path("dist/edge-artifacts"),
        help="Directory containing artifact_manifest.json and artifact files",
    )
    args = ap.parse_args()
    if args.command == "verify":
        try:
            verify_artifact_contract(args.artifacts_dir)
            print(f"OK artifact-contract {Path(args.artifacts_dir).resolve()}")
            return 0
        except Exception as exc:
            print(f"ARTIFACT_CONTRACT_FAIL: {exc}", file=sys.stderr)
            return 1
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
