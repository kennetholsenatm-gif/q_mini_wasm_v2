from __future__ import annotations

import json
from pathlib import Path

import pytest

from qminiwasm.wasm_host.artifact_contract import verify_artifact_contract
from qminiwasm.wasm_host.tpem_bundle import write_tpem_bundle


def _write_fixture_bundle(out_dir: Path) -> dict:
    wasm_path = out_dir / "qminiwasm-kernels.wasm"
    wasm_bytes = b"\x00asm\x01\x00\x00\x00"
    wasm_path.write_bytes(wasm_bytes)

    payload = b"\x01\x02\x03\x04\x05"
    tpem_path = out_dir / "qminiwasm-weights.tpem"
    write_tpem_bundle(tpem_path, payload)

    import hashlib

    return {
        "artifacts": {
            "kernels_wasm": {
                "path": wasm_path.name,
                "sha256": hashlib.sha256(wasm_bytes).hexdigest(),
                "bytes": len(wasm_bytes),
            },
            "weights_tpem": {
                "path": tpem_path.name,
                "payload_sha256": hashlib.sha256(payload).hexdigest(),
                "payload_bytes": len(payload),
                "bundle_format_version": 1,
                "pack_encoding_version": 2,
            },
        }
    }


def test_verify_artifact_contract_ok(tmp_path: Path):
    manifest = _write_fixture_bundle(tmp_path)
    (tmp_path / "artifact_manifest.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8"
    )
    verify_artifact_contract(tmp_path)


def test_verify_artifact_contract_payload_checksum_mismatch(tmp_path: Path):
    manifest = _write_fixture_bundle(tmp_path)
    manifest["artifacts"]["weights_tpem"]["payload_sha256"] = "0" * 64
    (tmp_path / "artifact_manifest.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8"
    )
    with pytest.raises(ValueError, match="tpem payload sha256 mismatch"):
        verify_artifact_contract(tmp_path)


def test_verify_artifact_contract_header_mismatch(tmp_path: Path):
    manifest = _write_fixture_bundle(tmp_path)
    manifest["artifacts"]["weights_tpem"]["bundle_format_version"] = 999
    (tmp_path / "artifact_manifest.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8"
    )
    with pytest.raises(ValueError, match="bundle_format_version mismatch"):
        verify_artifact_contract(tmp_path)
