"""SYCL Hardware Interface

This module provides a unified interface for SYCL hardware operations.
It uses a real SYCL backend when available, falling back to stubs when not.
"""

import logging
from typing import List, Optional

from qminiwasm.runtime_modes import strict_sycl_helper, sycl_backend_name
from qminiwasm.wasm_host.trit_pack import pack_ternary_list, unpack_ternary_list

# Try to import the real SYCL implementation
try:
    from qminiwasm.hardware.sycl_hardware import SYCLHardware as RealSYCLHardware

    SYCL_BACKEND = sycl_backend_name()
    if SYCL_BACKEND == "sycl":
        SYCL_AVAILABLE = True
        logger = logging.getLogger(__name__)
        logger.info("Using real SYCL backend")
    else:
        SYCL_AVAILABLE = False
        logger = logging.getLogger(__name__)
        logger.info("Using SYCL stubs (SYCL_BACKEND=%s)", SYCL_BACKEND)
except ImportError:
    SYCL_AVAILABLE = False
    logger = logging.getLogger(__name__)
    logger.info("SYCL backend not available, using stubs")


class SYCLHardware:
    """SYCLHardware: unified interface for SYCL hardware operations.

    Provides real SYCL implementation when available, falling back to stubs.
    """

    def __init__(self):
        """Initialize the SYCLHardware interface."""
        self.logger = logging.getLogger(__name__)
        self.logger.setLevel(logging.INFO)

        if SYCL_AVAILABLE:
            backend = RealSYCLHardware()
            is_active = getattr(backend, "is_backend_active", None)
            backend_active = (
                bool(is_active())
                if callable(is_active)
                else (getattr(backend, "device", None) is not None)
            )
            if backend_active:
                self._backend = backend
                self.logger.info("Initialized SYCLHardware with active backend")
            else:
                self._backend = None
                self.logger.info(
                    "Initialized SYCLHardware with stubs (SYCL runtime unavailable at runtime)"
                )
        else:
            self.logger.info("Initialized SYCLHardware with stubs")
            self._backend = None
        self._strict_helper = strict_sycl_helper()
        self._fallback_warned = False
        if self._strict_helper and not self.is_backend_active():
            st = self.backend_status()
            raise RuntimeError(
                "Strict SYCL helper mode enabled but backend is inactive "
                f"(reason={st.get('fallback_reason')}, active={st.get('active')})"
            )

    def is_backend_active(self) -> bool:
        return self._backend is not None

    def backend_status(self) -> dict:
        if self._backend and hasattr(self._backend, "backend_status"):
            try:
                st = self._backend.backend_status()
                st["strict_helper"] = bool(self._strict_helper)
                return st
            except Exception as e:
                self.logger.debug("SYCL backend_status() raised %s; using stub status", e)
        return {
            "active": False,
            "device_name": "none",
            "dpctl_device_count": 0,
            "fallback_reason": "sycl_backend_disabled_or_unavailable",
            "backend": "stub",
            "strict_helper": bool(self._strict_helper),
        }

    def _warn_stub_once(self, op: str) -> None:
        if self._fallback_warned:
            return
        self._fallback_warned = True
        st = self.backend_status()
        self.logger.warning(
            "SYCL stub fallback active during %s (reason=%s).",
            op,
            st.get("fallback_reason"),
        )

    def execute_vector_engine(self, kernel: str, data: List[float]) -> List[float]:
        """Execute kernel on Vector Engine (XVE)."""
        if self._backend:
            return self._backend.execute_vector_engine(kernel, data)
        self._warn_stub_once("execute_vector_engine")
        self.logger.info("Executing %s on Vector Engine (XVE) [stub]", kernel)
        return data

    def execute_matrix_engine(
        self, matrix: List[List[float]], weights: List[List[float]]
    ) -> List[List[float]]:
        """Execute matrix operations on Matrix Engine (XMX)."""
        if self._backend:
            return self._backend.execute_matrix_engine(matrix, weights)
        self._warn_stub_once("execute_matrix_engine")
        self.logger.info("Executing matrix operations on Matrix Engine (XMX) [stub]")
        return [[sum(a * b for a, b in zip(row, col)) for col in zip(*weights)] for row in matrix]

    def pack_ternary_weights(self, weights: List[int]) -> bytes:
        """Pack ternary weights (MSB-first groups; see ``qminiwasm.wasm_host.trit_pack``)."""
        if self._backend:
            return self._backend.pack_ternary_weights(weights)
        out = pack_ternary_list(weights)
        self.logger.debug("Packed %d trits into %d bytes", len(weights), len(out))
        return out

    def unpack_ternary_weights(self, packed: bytes, num_weights: Optional[int] = None) -> List[int]:
        """Unpack bytes to ternary weights (-1, 0, 1)."""
        if self._backend:
            return self._backend.unpack_ternary_weights(packed, num_weights)
        if num_weights is None:
            num_weights = len(packed) * 5
        return unpack_ternary_list(packed, num_weights)

    def driver_memory_paging(self, memory: List[float], size: int) -> None:
        """Implement driver-level memory paging."""
        if self._backend:
            return self._backend.driver_memory_paging(memory, size)
        self._warn_stub_once("driver_memory_paging")
        self.logger.info("Executing driver-level memory paging [stub]")
