"""Shared optional native runtime loader."""

from __future__ import annotations

import ctypes
import importlib
import os
from functools import lru_cache
from pathlib import Path
from typing import Any, Optional


@lru_cache(maxsize=1)
def load_native_lib() -> Optional[ctypes.CDLL]:
    here = Path(__file__).resolve().parent.parent
    names = (
        "qminiwasm_native.dll",
        "libqminiwasm_native.so",
        "libqminiwasm_native.dylib",
    )
    candidates = list(names) + [str(here / n) for n in names]
    for name in candidates:
        try:
            return ctypes.CDLL(name)
        except OSError:
            continue
    return None


@lru_cache(maxsize=1)
def native_capabilities() -> dict[str, Any]:
    lib = load_native_lib()
    caps: dict[str, Any] = {
        "ctypes_lib_loaded": bool(lib),
        "abi_version": None,
        "symbols": {},
    }
    if lib is not None:
        for sym in (
            "qmw_native_abi_version",
            "qmw_wasmedge_execute",
            "qmw_rl_rollout_returns",
            "qmw_tpem_build_bundle",
            "qmw_lota_forward_f32",
            "qmw_tsign_update_f32",
        ):
            caps["symbols"][sym] = hasattr(lib, sym)
        if hasattr(lib, "qmw_native_abi_version"):
            try:
                fn = lib.qmw_native_abi_version
                fn.argtypes = []
                fn.restype = ctypes.c_int
                caps["abi_version"] = int(fn())
            except Exception:
                caps["abi_version"] = None
    try:
        mod = importlib.import_module("qminiwasm._native_ternary")
        caps["pybind_loaded"] = True
        caps["pybind_symbols"] = {
            "dot_u8_i8": hasattr(mod, "dot_u8_i8"),
            "pack_ternary_list": hasattr(mod, "pack_ternary_list"),
            "unpack_ternary_list": hasattr(mod, "unpack_ternary_list"),
            "encode_linear_memory_u8": hasattr(mod, "encode_linear_memory_u8"),
        }
    except Exception:
        caps["pybind_loaded"] = False
        caps["pybind_symbols"] = {}
    caps["strict_native"] = os.getenv("QMINIWASM_NATIVE_STRICT", "").strip().lower() in {
        "1",
        "true",
        "yes",
        "on",
    }
    return caps
