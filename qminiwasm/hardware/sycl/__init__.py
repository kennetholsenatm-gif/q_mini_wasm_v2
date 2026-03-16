"""SYCL Hardware Implementation

This module provides a real SYCL implementation of the hardware mapping interface.
It uses Intel oneAPI SYCL to provide native hardware acceleration for vector and matrix operations.
"""

from .sycl_hardware import SYCLHardware

__all__ = ["SYCLHardware"]
