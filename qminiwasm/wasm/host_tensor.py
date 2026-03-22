"""Host tensor callbacks for wasmtime (primary path before WasmEdge WASI-NN).

WASM modules that ``import`` a host function (e.g. ``env.host_ternary_dot``) can
delegate packed ternary × activation work to PyTorch on the host.

Typical ABI (contract between guest C/WAT and this linker):

- Guest passes linear-memory byte offsets and lengths.
- Host reads bytes via :class:`wasmtime.Memory`, runs ``torch`` matmul or a SYCL
  extension, writes results back.

Use :class:`wasmtime.Linker` ``define`` to bind functions; instantiate the guest
module with that linker instead of empty imports.
"""

from __future__ import annotations

from typing import Any, Callable

from qminiwasm.hardware.native_ternary import dot_u8_i8


def pytorch_ternary_digit_dot_from_memory(
    mem: Any,
    store: Any,
    n: int,
    w_off: int,
    a_off: int,
    offset_per_lane: int = 1,
) -> int:
    """Reference host: unsigned digits ``{0,1,2}`` at ``w_off``, signed int8 at ``a_off``."""
    n = int(n)
    if n <= 0:
        return 0
    raw_w = bytes(mem.read(store, int(w_off), n))
    raw_a = bytes(mem.read(store, int(a_off), n))
    return dot_u8_i8(raw_w, raw_a, int(offset_per_lane))


def make_host_ternary_dot_func(
    store: Any,
    memory_export_name: str = "memory",
) -> Callable[..., Any]:
    """Build a wasmtime closure that captures nothing; resolves memory from caller instance."""

    def host_ternary_dot(caller: Any, n: int, w_off: int, a_off: int, off_lane: int) -> int:
        mem = caller.get(memory_export_name)
        return pytorch_ternary_digit_dot_from_memory(mem, store, n, w_off, a_off, int(off_lane))

    return host_ternary_dot
