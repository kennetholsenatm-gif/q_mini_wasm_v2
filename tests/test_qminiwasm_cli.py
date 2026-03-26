"""Smoke tests for ``python -m qminiwasm.cli`` (delegates training to ``qminiwasm.engine``)."""

from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path

from qminiwasm.wasm_host.tpem_bundle import write_tpem_bundle


def test_cli_help_exits_zero():
    r = subprocess.run(
        [sys.executable, "-m", "qminiwasm.cli", "--help"],
        capture_output=True,
        text=True,
        check=False,
    )
    assert r.returncode == 0
    assert "train" in r.stdout


def test_cli_no_subcommand_exits_nonzero():
    r = subprocess.run(
        [sys.executable, "-m", "qminiwasm.cli"],
        capture_output=True,
        text=True,
        check=False,
    )
    assert r.returncode != 0
    assert "train" in r.stdout or "train" in r.stderr


def _write_artifacts_and_graph_manifest(tmp_path: Path) -> Path:
    artifacts_dir = tmp_path / "artifacts"
    artifacts_dir.mkdir(parents=True, exist_ok=True)
    wasm_path = artifacts_dir / "qminiwasm-kernels.wasm"
    wasm_bytes = b"\x00asm\x01\x00\x00\x00"
    wasm_path.write_bytes(wasm_bytes)
    tpem_payload = b"\x01\x02\x03\x04"
    tpem_path = artifacts_dir / "qminiwasm-weights.tpem"
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
    (artifacts_dir / "artifact_manifest.json").write_text(
        json.dumps(artifact_manifest, indent=2) + "\n", encoding="utf-8"
    )
    graph_manifest = {
        "version": "1",
        "graph_id": "graph-cli",
        "node_id": "node-cli",
        "artifacts_dir": str(artifacts_dir),
        "inputs": ["in"],
        "outputs": ["out"],
        "backend_constraints": {"accelerator": "cpu", "ternary_required": True},
        "routing_policy": {"policy_ref": "policy/cli", "require_encryption": True},
        "control_plane": {"grpc_target": "127.0.0.1:50051", "timeout_seconds": 5},
    }
    graph_manifest_path = tmp_path / "graph_manifest.json"
    graph_manifest_path.write_text(json.dumps(graph_manifest, indent=2) + "\n", encoding="utf-8")
    return graph_manifest_path


def test_cli_graph_validate_exits_zero(tmp_path: Path):
    graph_manifest_path = _write_artifacts_and_graph_manifest(tmp_path)
    r = subprocess.run(
        [sys.executable, "-m", "qminiwasm.cli", "graph", "validate", str(graph_manifest_path)],
        capture_output=True,
        text=True,
        check=False,
    )
    assert r.returncode == 0
    assert "OK graph validate" in r.stdout


def test_cli_graph_apply_exits_zero(tmp_path: Path):
    graph_manifest_path = _write_artifacts_and_graph_manifest(tmp_path)
    r = subprocess.run(
        [sys.executable, "-m", "qminiwasm.cli", "graph", "apply", str(graph_manifest_path)],
        capture_output=True,
        text=True,
        check=False,
    )
    assert r.returncode == 0
    assert "OK graph apply" in r.stdout
