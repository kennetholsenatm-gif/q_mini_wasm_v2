from __future__ import annotations

import torch

from qminiwasm.hardware import device as device_mod


def test_get_device_sycl_uses_xpu_when_available(monkeypatch):
    monkeypatch.setattr(device_mod, "_xpu_available", lambda: True)
    d = device_mod.get_device(accelerator="sycl", device_index=0)
    assert isinstance(d, torch.device)
    assert d.type == "xpu"


def test_get_device_sycl_falls_back_to_cpu(monkeypatch):
    monkeypatch.setattr(device_mod, "_xpu_available", lambda: False)
    d = device_mod.get_device(accelerator="sycl", device_index=0)
    assert isinstance(d, torch.device)
    assert d.type == "cpu"


def test_get_device_xpu_uses_xpu_when_runtime_available(monkeypatch):
    monkeypatch.setattr(device_mod, "_xpu_runtime_status", lambda: (True, "ok"))
    d = device_mod.get_device(accelerator="xpu", device_index=0)
    assert isinstance(d, torch.device)
    assert d.type == "xpu"


def test_get_device_xpu_falls_back_to_cpu_when_runtime_unavailable(monkeypatch):
    monkeypatch.setattr(device_mod, "_xpu_runtime_status", lambda: (False, "missing runtime"))
    d = device_mod.get_device(accelerator="xpu", device_index=0)
    assert isinstance(d, torch.device)
    assert d.type == "cpu"
