"""SYCL Hardware Interface

This module provides a unified interface for SYCL hardware operations.
It uses a real SYCL backend when available, falling back to stubs when not.
"""

import logging
import os
from typing import List, Optional

from qminiwasm.wasm.trit_pack import pack_ternary_list, unpack_ternary_list

# Try to import the real SYCL implementation
try:
    from qminiwasm.hardware.sycl import SYCLHardware as RealSYCLHardware

    SYCL_BACKEND = os.getenv("SYCL_BACKEND", "sycl").lower()
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
            self._backend = RealSYCLHardware()
            self.logger.info("Initialized SYCLHardware with real backend")
        else:
            self.logger.info("Initialized SYCLHardware with stubs")
            self._backend = None

    def execute_vector_engine(self, kernel: str, data: List[float]) -> List[float]:
        """Execute kernel on Vector Engine (XVE)."""
        if self._backend:
            return self._backend.execute_vector_engine(kernel, data)
        self.logger.info("Executing %s on Vector Engine (XVE) [stub]", kernel)
        return data

    def execute_matrix_engine(
        self, matrix: List[List[float]], weights: List[List[float]]
    ) -> List[List[float]]:
        """Execute matrix operations on Matrix Engine (XMX)."""
        if self._backend:
            return self._backend.execute_matrix_engine(matrix, weights)
        self.logger.info("Executing matrix operations on Matrix Engine (XMX) [stub]")
        return [[sum(a * b for a, b in zip(row, col)) for col in zip(*weights)] for row in matrix]

    def pack_ternary_weights(self, weights: List[int]) -> bytes:
        """Pack ternary weights (MSB-first groups; see ``qminiwasm.wasm.trit_pack``)."""
        if self._backend:
            return self._backend.pack_ternary_weights(weights)
        out = pack_ternary_list(weights)
        self.logger.debug("Packed %d trits into %d bytes", len(weights), len(out))
        return out

    def unpack_ternary_weights(
        self, packed: bytes, num_weights: Optional[int] = None
    ) -> List[int]:
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
        self.logger.info("Executing driver-level memory paging [stub]")
