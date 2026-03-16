"""Hardware Module

This module implements hardware-specific components for the Q-Mini-WASM architecture, including:
- SYCL/Intel hardware mapping stubs
- Unified device selection: CUDA, Intel ARC (XPU), and CPU
- Driver-level memory paging stubs
- Hardware acceleration interfaces

Key Components:
- SYCLHardware: SYCL hardware mapping interface (stubs or native when SYCL_BACKEND=sycl)
- get_device, get_device_name: Device dispatcher (CUDA / XPU / CPU)
- AcceleratorType: Literal type for accelerator choice

Backend selection: set SYCL_BACKEND=sycl to prefer a native SYCL extension; otherwise stubs are
used.
See docs/SYCL-Integration.md for the integration contract.
"""

import os

if os.environ.get("SYCL_BACKEND") == "sycl":
    try:
        from .sycl_native import SYCLHardware  # type: ignore[import-not-found]
    except ImportError:
        from .sycl_stubs import SYCLHardware
else:
    from .sycl_stubs import SYCLHardware
from .device import AcceleratorType, get_device, get_device_name

__all__ = ["SYCLHardware", "get_device", "get_device_name", "AcceleratorType"]
