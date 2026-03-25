"""Shim: state migration helpers live in :mod:`qminiwasm.enclave`."""

from qminiwasm.enclave.delta_compression import compress_deltas, delta_payload_struct

__all__ = ["compress_deltas", "delta_payload_struct"]
