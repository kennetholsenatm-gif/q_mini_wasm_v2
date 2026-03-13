"""Hardware Module

This module implements hardware-specific components for the Q-Mini-WASM architecture, including:
- SYCL/Intel hardware mapping stubs
- Intel ARC (XPU) device selection for AI training (no CUDA)
- Driver-level memory paging stubs
- Hardware acceleration interfaces

Key Components:
- SYCLHardware: SYCL hardware mapping interface
- get_device, get_device_name: Intel ARC/XPU-first device selection
- MemoryPager: Driver-level memory paging interface
- HardwareStubs: Hardware-specific stubs for development
"""

from .sycl_stubs import SYCLHardware
from .device import get_device, get_device_name

__all__ = ["SYCLHardware", "get_device", "get_device_name"]