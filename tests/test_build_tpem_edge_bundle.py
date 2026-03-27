"""Edge artifact build script: tier, zip, contract."""

from __future__ import annotations

import json
import subprocess
import sys
import zipfile
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parent.parent
SCRIPT = REPO / "scripts" / "build_tpem_wasm_artifacts.py"


def test_build_script_tier1_and_contract(tmp_path: Path) -> None:
    out = tmp_path / "out"
    r = subprocess.run(
        [sys.executable, str(SCRIPT), "--out-dir", str(out), "--tier", "1"],
        cwd=str(REPO),
        capture_output=True,
        text=True,
        check=False,
    )
    assert r.returncode == 0, r.stderr + r.stdout
    assert (out / "edge_schema.json").is_file()
    assert (out / "qminiwasm-edge-bundle.zip").is_file()
    assert (out / "qminiwasm-edge-bundle.tpem").is_file()
    z = (out / "qminiwasm-edge-bundle.zip").read_bytes()
    assert (out / "qminiwasm-edge-bundle.tpem").read_bytes() == z
    man = json.loads((out / "artifact_manifest.json").read_text(encoding="utf-8"))
    assert "edge_bundle" in man["artifacts"]
    assert "edge_bundle_tpem" in man["artifacts"]
    assert "edge_schema" in man["artifacts"]
    with zipfile.ZipFile(out / "qminiwasm-edge-bundle.zip") as zf:
        names = set(zf.namelist())
    assert "qminiwasm-kernels.wasm" in names
    assert "qminiwasm-weights.tpem" in names
    assert "edge_schema.json" in names
    assert "artifact_manifest.json" not in names

    from qminiwasm.wasm_host.artifact_contract import verify_artifact_contract

    verify_artifact_contract(out)


def test_build_with_checkpoint_ternary_weights(tmp_path: Path) -> None:
    import torch

    ck = tmp_path / "trainable.pt"
    torch.save({"ternary_expert": {"weight": torch.randn(4, 4).clamp(-1, 1)}}, ck)
    out = tmp_path / "out_ckpt"
    subprocess.run(
        [
            sys.executable,
            str(SCRIPT),
            "--out-dir",
            str(out),
            "--tier",
            "2",
            "--checkpoint",
            str(ck),
        ],
        cwd=str(REPO),
        check=True,
    )
    from qminiwasm.wasm_host.tpem_bundle import read_tpem_bundle

    bundle = read_tpem_bundle(out / "qminiwasm-weights.tpem")
    assert len(bundle.payload) > 0


@pytest.mark.parametrize("tier", (3, 5))
def test_edge_schema_memory64_tier3_plus(tmp_path: Path, tier: int) -> None:
    out = tmp_path / f"t{tier}"
    subprocess.run(
        [sys.executable, str(SCRIPT), "--out-dir", str(out), "--tier", str(tier)],
        cwd=str(REPO),
        check=True,
    )
    schema = json.loads((out / "edge_schema.json").read_text(encoding="utf-8"))
    assert schema["use_memory64"] is True
    assert schema["wasm_memory64_max_mb"] is not None
