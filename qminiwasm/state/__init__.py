"""Legacy alias: WLES / delta helpers live in :mod:`qminiwasm.wasm_host.delta_compression`."""

from qminiwasm.wasm_host.delta_compression import compress_deltas, delta_payload_struct

__all__ = ["compress_deltas", "delta_payload_struct"]
