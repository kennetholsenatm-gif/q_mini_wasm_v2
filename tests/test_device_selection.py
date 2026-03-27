from __future__ import annotations

import pytest
import torch

from qminiwasm.hardware import device as device_mod


def test_get_device_sycl_uses_xpu_when_available(monkeypatch):
    monkeypatch.setattr(device_mod, "_xpu_runtime_status", lambda: (True, "ok"))
    monkeypatch.setattr(device_mod, "_xpu_device_name", lambda _i=0: "Intel(R) Arc(TM)")
    d = device_mod.get_device(accelerator="sycl", device_index=0)
    assert isinstance(d, torch.device)
    assert d.type == "xpu"


def test_get_device_sycl_falls_back_to_cpu(monkeypatch):
    monkeypatch.setattr(device_mod, "_xpu_runtime_status", lambda: (False, "missing runtime"))
    d = device_mod.get_device(accelerator="sycl", device_index=0)
    assert isinstance(d, torch.device)
    assert d.type == "cpu"


def test_get_device_xpu_uses_xpu_when_runtime_available(monkeypatch):
    monkeypatch.setattr(device_mod, "_xpu_runtime_status", lambda: (True, "ok"))
    monkeypatch.setattr(device_mod, "_xpu_device_name", lambda _i=0: "Intel(R) Arc(TM)")
    d = device_mod.get_device(accelerator="xpu", device_index=0)
    assert isinstance(d, torch.device)
    assert d.type == "xpu"


def test_get_device_xpu_falls_back_to_cpu_when_runtime_unavailable(monkeypatch):
    monkeypatch.setattr(device_mod, "_xpu_runtime_status", lambda: (False, "missing runtime"))
    d = device_mod.get_device(accelerator="xpu", device_index=0)
    assert isinstance(d, torch.device)
    assert d.type == "cpu"


def test_get_xpu_backend_status_experimental_for_iris(monkeypatch):
    monkeypatch.setattr(device_mod, "_xpu_runtime_status", lambda: (True, "ok"))
    monkeypatch.setattr(device_mod, "_xpu_device_name", lambda _i=0: "Intel(R) Iris(R) Xe Graphics")
    status = device_mod.get_xpu_backend_status(0)
    assert status["available"] is True
    assert status["support_class"] == "experimental"


def test_get_device_xpu_strict_mode_raises(monkeypatch):
    monkeypatch.setattr(device_mod, "_xpu_runtime_status", lambda: (False, "missing runtime"))
    monkeypatch.setenv("QMINIWASM_STRICT_XPU", "1")
    with pytest.raises(RuntimeError, match="strict mode"):
        device_mod.get_device(accelerator="xpu", device_index=0)


def test_resolve_backend_policy_includes_reason_code(monkeypatch):
    monkeypatch.setattr(device_mod, "_xpu_runtime_status", lambda: (False, "missing runtime"))
    pol = device_mod.resolve_backend_policy(accelerator="sycl", device_index=0)
    assert pol["selected_device"] == "cpu"
    assert pol["reason_code"] == "sycl_unavailable_fallback_cpu"
