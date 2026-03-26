from __future__ import annotations

import json
from pathlib import Path

import pytest

from qminiwasm.engine.graph_manifest import load_graph_manifest, validate_graph_manifest
from qminiwasm.wasm_host.tpem_bundle import write_tpem_bundle


def _write_artifacts(out_dir: Path) -> None:
    wasm_path = out_dir / "qminiwasm-kernels.wasm"
    wasm_bytes = b"\x00asm\x01\x00\x00\x00"
    wasm_path.write_bytes(wasm_bytes)
    tpem_payload = b"\x01\x02\x03\x04\x05"
    tpem_path = out_dir / "qminiwasm-weights.tpem"
    write_tpem_bundle(tpem_path, tpem_payload)

    import hashlib

    artifact_manifest = {
        "artifacts": {
            "kernels_wasm": {
                "path": wasm_path.name,
                "sha256": hashlib.sha256(wasm_bytes).hexdigest(),
                "bytes": len(wasm_bytes),
            },
            "weights_tpem": {
                "path": tpem_path.name,
                "payload_sha256": hashlib.sha256(tpem_payload).hexdigest(),
                "payload_bytes": len(tpem_payload),
                "bundle_format_version": 1,
                "pack_encoding_version": 2,
            },
        }
    }
    (out_dir / "artifact_manifest.json").write_text(
        json.dumps(artifact_manifest, indent=2) + "\n", encoding="utf-8"
    )


def _write_graph_manifest(path: Path, artifacts_dir: Path) -> None:
    doc = {
        "version": "1",
        "graph_id": "graph-alpha",
        "node_id": "node-a",
        "artifacts_dir": str(artifacts_dir),
        "inputs": ["ingress.topic"],
        "outputs": ["egress.topic"],
        "backend_constraints": {"accelerator": "cpu", "ternary_required": True},
        "routing_policy": {"policy_ref": "policy/v1", "require_encryption": True},
        "control_plane": {"grpc_target": "127.0.0.1:50051", "timeout_seconds": 5},
    }
    path.write_text(json.dumps(doc, indent=2) + "\n", encoding="utf-8")


def test_load_graph_manifest_ok(tmp_path: Path):
    artifacts_dir = tmp_path / "artifacts"
    artifacts_dir.mkdir(parents=True, exist_ok=True)
    _write_artifacts(artifacts_dir)
    manifest_path = tmp_path / "graph_manifest.json"
    _write_graph_manifest(manifest_path, artifacts_dir)
    got = load_graph_manifest(manifest_path)
    assert got.graph_id == "graph-alpha"
    assert got.node_id == "node-a"
    assert got.control_plane.grpc_target == "127.0.0.1:50051"


def test_validate_graph_manifest_also_verifies_artifacts(tmp_path: Path):
    artifacts_dir = tmp_path / "artifacts"
    artifacts_dir.mkdir(parents=True, exist_ok=True)
    _write_artifacts(artifacts_dir)
    manifest_path = tmp_path / "graph_manifest.json"
    _write_graph_manifest(manifest_path, artifacts_dir)
    got = validate_graph_manifest(manifest_path)
    assert got.routing_policy.require_encryption is True


def test_load_graph_manifest_rejects_unknown_field(tmp_path: Path):
    artifacts_dir = tmp_path / "artifacts"
    artifacts_dir.mkdir(parents=True, exist_ok=True)
    _write_artifacts(artifacts_dir)
    manifest_path = tmp_path / "graph_manifest.json"
    _write_graph_manifest(manifest_path, artifacts_dir)
    raw = json.loads(manifest_path.read_text(encoding="utf-8"))
    raw["unknown_field"] = "bad"
    manifest_path.write_text(json.dumps(raw, indent=2) + "\n", encoding="utf-8")
    with pytest.raises(ValueError, match="unknown top-level field"):
        load_graph_manifest(manifest_path)
