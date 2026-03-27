"""Enclave tier normalization, TPEM footprint estimate, and export meta on saves."""

from __future__ import annotations

from unittest.mock import patch

import pytest

from qminiwasm.config import normalize_enclave_tier_value
from qminiwasm.engine.training_schema import EnclaveSection
from qminiwasm.hardware.device import get_device
from qminiwasm.model import QMiniWASM
from qminiwasm.training.enclave_footprint import estimate_trainable_tpem_size_mb
from qminiwasm.training.loop import run_training_loop
from qminiwasm.tpem.trainable_tpem import build_trainable_tpem_payload


def test_normalize_enclave_tier_ints_and_strings() -> None:
    assert normalize_enclave_tier_value(1) == "micro"
    assert normalize_enclave_tier_value(3) == "macro"
    assert normalize_enclave_tier_value("5") == "enterprise_core"
    assert normalize_enclave_tier_value("MESO") == "meso"
    assert normalize_enclave_tier_value(None) is None
    assert normalize_enclave_tier_value("") is None


def test_normalize_enclave_tier_invalid() -> None:
    with pytest.raises(ValueError):
        normalize_enclave_tier_value(0)
    with pytest.raises(ValueError):
        normalize_enclave_tier_value("unknown_tier")
    with pytest.raises(ValueError):
        normalize_enclave_tier_value(True)


def test_enclave_section_pydantic_integer_tier() -> None:
    sec = EnclaveSection.model_validate({"enclave_tier": 2})
    assert sec.enclave_tier == "meso"


def test_estimate_footprint_hybrid_adapter_increases() -> None:
    device = get_device(accelerator="cpu", device_index=0)
    base = QMiniWASM(
        device=device,
        use_hybrid_adapter=False,
        lota_rank=0,
        use_cascade_router=False,
    )
    with_adapter = QMiniWASM(
        device=device,
        use_hybrid_adapter=True,
        hybrid_adapter_hidden=1024,
        lota_rank=0,
        use_cascade_router=False,
    )
    assert estimate_trainable_tpem_size_mb(with_adapter, None) > estimate_trainable_tpem_size_mb(
        base, None
    )


def test_build_trainable_tpem_payload_carries_meta() -> None:
    device = get_device(accelerator="cpu", device_index=0)
    model = QMiniWASM(device=device, use_hybrid_adapter=False, use_cascade_router=False)
    meta = {
        "enclave_tier": "macro",
        "use_memory64": True,
        "wasm_memory64_max_mb": 8192.0,
        "store_memory_limit_bytes": 12345,
    }
    payload = build_trainable_tpem_payload(model, meta=meta, cascade_policy=None)
    assert payload["meta"]["enclave_tier"] == "macro"
    assert payload["meta"]["use_memory64"] is True
    assert payload["meta"]["wasm_memory64_max_mb"] == 8192.0


def test_run_training_loop_merges_export_runtime_into_save_meta(tmp_path) -> None:
    latest = tmp_path / "latest.pt"
    export_runtime_policy = {
        "enclave_tier": "macro",
        "use_memory64": True,
        "wasm_memory64_max_mb": 8192.0,
        "store_memory_limit_bytes": 4096,
    }
    with patch("qminiwasm.training.loop.save_trainable_tpem_artifact") as mock_save:
        run_training_loop(
            epochs=1,
            batch_size=2,
            use_cascade_rl=False,
            checkpoint_latest_path=str(latest),
            enclave_tier="macro",
            export_runtime_policy=export_runtime_policy,
        )
    metas = [c.kwargs.get("meta") for c in mock_save.call_args_list if c.kwargs.get("meta")]
    assert metas
    assert any(m.get("wasm_memory64_max_mb") == 8192.0 for m in metas)
    assert any(m.get("use_memory64") is True for m in metas)
    assert any(m.get("store_memory_limit_bytes") == 4096 for m in metas)
