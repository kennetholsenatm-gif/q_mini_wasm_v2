"""Shared optional native runtime loader."""

from __future__ import annotations

import ctypes
from functools import lru_cache
from typing import Optional


@lru_cache(maxsize=1)
def load_native_lib() -> Optional[ctypes.CDLL]:
    names = (
        "qminiwasm_native.dll",
        "libqminiwasm_native.so",
        "libqminiwasm_native.dylib",
    )
    for name in names:
        try:
            return ctypes.CDLL(name)
        except OSError:
            continue
    return None
