from __future__ import annotations

import os

_IMPL_VALUES = frozenset({"auto", "python", "native"})

_OPTIMIZED_AUTO_DEFAULTS = {
    "QMINIWASM_TERNARY_IMPL": "auto",
    "QMINIWASM_TRIT_PACK_IMPL": "auto",
    "QMINIWASM_MEMORY_ENCODE_IMPL": "auto",
    "QMINIWASM_WASM_EXEC_IMPL": "auto",
    "QMINIWASM_CASCADE_RL_IMPL": "auto",
    # Benchmarked as slower in current environment; default to Python path.
    "QMINIWASM_TPEM_NATIVE_BUNDLE": "0",
}


def resolve_impl_mode(env_name: str, default: str = "auto") -> str:
    mode = os.getenv(env_name, default).strip().lower()
    if mode in _IMPL_VALUES:
        return mode
    return default if default in _IMPL_VALUES else "auto"


def apply_optimized_auto_defaults() -> None:
    for k, v in _OPTIMIZED_AUTO_DEFAULTS.items():
        os.environ.setdefault(k, v)


def strict_native_enabled() -> bool:
    return os.getenv("QMINIWASM_NATIVE_STRICT", "").strip().lower() in {
        "1",
        "true",
        "yes",
        "on",
    }
