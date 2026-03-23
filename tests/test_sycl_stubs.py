from __future__ import annotations

import logging

from qminiwasm.hardware import sycl_stubs


class _FakeInactiveBackend:
    def is_backend_active(self) -> bool:
        return False


class _FakeActiveBackend:
    def is_backend_active(self) -> bool:
        return True

    def execute_vector_engine(self, kernel: str, data: list[float]) -> list[float]:
        return data

    def execute_matrix_engine(
        self, matrix: list[list[float]], weights: list[list[float]]
    ) -> list[list[float]]:
        return matrix

    def pack_ternary_weights(self, weights: list[int]) -> bytes:
        return b""

    def unpack_ternary_weights(self, packed: bytes, num_weights: int | None = None) -> list[int]:
        return []

    def driver_memory_paging(self, memory: list[float], size: int) -> None:
        return None


def test_sycl_stubs_reports_runtime_unavailable(monkeypatch, caplog):
    monkeypatch.setattr(sycl_stubs, "SYCL_AVAILABLE", True)
    monkeypatch.setattr(sycl_stubs, "RealSYCLHardware", _FakeInactiveBackend)
    caplog.set_level(logging.INFO)
    hw = sycl_stubs.SYCLHardware()
    assert hw._backend is None
    assert "runtime unavailable at runtime" in caplog.text


def test_sycl_stubs_reports_active_backend(monkeypatch, caplog):
    monkeypatch.setattr(sycl_stubs, "SYCL_AVAILABLE", True)
    monkeypatch.setattr(sycl_stubs, "RealSYCLHardware", _FakeActiveBackend)
    caplog.set_level(logging.INFO)
    hw = sycl_stubs.SYCLHardware()
    assert hw._backend is not None
    assert "active backend" in caplog.text
