"""Wasmtime helpers for **WASM Linear Execution Snapshots (WLES)** harnessing.

Round-trip linear memory through :func:`qminiwasm.wasm_host.memory_encode.build_wles_envelope`
without requiring the full :class:`~qminiwasm.wasm_host.engine.WasmEngine` training path.
"""

from __future__ import annotations

from typing import Any, Dict, List, Optional, Tuple

from .memory_encode import build_wles_envelope


def _read_linear_memory(instance: Any, store: Any) -> bytes:
    try:
        mem = instance.exports(store)["memory"]  # type: ignore[index]
    except KeyError:
        return b""
    n = mem.data_len(store)
    if n <= 0:
        return b""
    return bytes(mem.read(store, 0, n))


def write_linear_memory(instance: Any, store: Any, blob: bytes) -> None:
    mem = instance.exports(store)["memory"]  # type: ignore[index]
    if len(blob) > mem.data_len(store):
        raise ValueError("WLES linear_memory exceeds instance memory size")
    mem.write(store, blob, 0)


def instantiate_minimal_wat(wat: str) -> Tuple[Any, Any, Any]:
    """Compile WAT and return ``(store, module, instance)`` for the default engine."""
    import wasmtime

    wasm = wasmtime.wat2wasm(wat)
    store = wasmtime.Store()
    module = wasmtime.Module(store.engine, wasm)
    instance = wasmtime.Instance(store, module, [])
    return store, module, instance


def snapshot_wles_envelope_from_instance(
    store: Any,
    instance: Any,
    *,
    stack_snapshot: Optional[bytes] = None,
    instruction_pointer: Optional[int] = None,
) -> Dict[str, Any]:
    """Capture exported linear memory into a WLES envelope dict."""
    mem = _read_linear_memory(instance, store)
    return build_wles_envelope(
        mem,
        stack_snapshot=stack_snapshot,
        instruction_pointer=instruction_pointer,
    )


def call_export_i32(
    store: Any, instance: Any, name: str, args: List[int]
) -> Tuple[Optional[int], Any]:
    """Invoke an exported function; returns ``(first_i32_result_or_none, full_result)``."""
    fn = instance.exports(store)[name]  # type: ignore[index]
    res = fn(store, *args)
    if isinstance(res, int):
        return res, res
    if isinstance(res, tuple) and res and isinstance(res[0], int):
        return res[0], res
    return None, res


def wles_roundtrip_linear_memory_same_instance(
    wat: str,
    *,
    run_export: str,
    run_args: List[int],
    ip_after: Optional[int] = None,
) -> Dict[str, Any]:
    """Run guest, build WLES envelope, restore bytes into the same memory; return envelope.

    Asserts byte-identity of linear memory after restore (guest state reset semantics).
    """
    store, _mod, inst = instantiate_minimal_wat(wat)
    call_export_i32(store, inst, run_export, run_args)
    env = snapshot_wles_envelope_from_instance(store, inst, instruction_pointer=ip_after)
    blob = env["linear_memory"]
    write_linear_memory(inst, store, blob)
    env2 = snapshot_wles_envelope_from_instance(store, inst, instruction_pointer=ip_after)
    if env2["linear_memory"] != blob:
        raise AssertionError("WLES restore did not preserve linear_memory bytes")
    return env
