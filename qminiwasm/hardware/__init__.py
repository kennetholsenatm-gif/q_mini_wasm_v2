"""Hardware Module

This module implements hardware-specific components for the Q-Mini-WASM architecture, including:
- SYCL/Intel hardware mapping stubs
- Driver-level memory paging stubs
- Hardware acceleration interfaces

Key Components:
- SYCLHardware: SYCL hardware mapping interface
- MemoryPager: Driver-level memory paging interface
- HardwareStubs: Hardware-specific stubs for development
"""

from .sycl_stubs import SYCLHardware

__all__ = ["SYCLHardware"]