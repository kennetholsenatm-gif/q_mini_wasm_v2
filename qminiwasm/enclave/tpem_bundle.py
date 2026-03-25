"""Shim for :mod:`qminiwasm.wasm_host.tpem_bundle`."""

from __future__ import annotations

from qminiwasm.wasm_host.tpem_bundle import *  # noqa: F403

if __name__ == "__main__":
    from qminiwasm.wasm_host.tpem_bundle import main

    main()
