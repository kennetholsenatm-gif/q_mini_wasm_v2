"""Compatibility shim: canonical implementation is :mod:`qminiwasm.wasm_host`.

Import :mod:`qminiwasm.wasm_host` in new code; this package re-exports the same API.
"""

from __future__ import annotations

from qminiwasm.wasm_host import *  # noqa: F403
from qminiwasm.wasm_host import __all__ as __all__
