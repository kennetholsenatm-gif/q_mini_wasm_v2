"""Hardware Module

This module implements hardware-specific components for the Q-Mini-WASM architecture, including:
- SYCL/Intel hardware mapping stubs
- Unified device selection: CUDA, Intel ARC (XPU), and CPU
- Driver-level memory paging stubs
- Hardware acceleration interfaces

Key Components:
- SYCLHardware: SYCL hardware mapping interface
- get_device, get_device_name: Device dispatcher (CUDA / XPU / CPU)
- AcceleratorType: Literal type for accelerator choice
"""

from .sycl_stubs import SYCLHardware
from .device import AcceleratorType, get_device, get_device_name

__all__ = ["SYCLHardware", "get_device", "get_device_name", "AcceleratorType"]
