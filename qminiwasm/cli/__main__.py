"""``python -m qminiwasm.cli`` — thin wrapper over :mod:`qminiwasm.engine` for training."""

from __future__ import annotations

import runpy
import sys


def _usage() -> str:
    return (
        "usage: python -m qminiwasm.cli train [--config PATH] [--log-level LEVEL]\n"
        "\n"
        "  train   Same options as ``python -m qminiwasm.engine``.\n"
        "\n"
        "WASM runtime code lives under ``qminiwasm.wasm_host``.\n"
    )


if __name__ == "__main__":
    argv = sys.argv[1:]
    if not argv or argv[0] in ("-h", "--help"):
        print(_usage(), end="" if argv else "\n")
        sys.exit(0 if argv else 1)
    if argv[0] == "train":
        sys.argv = [sys.argv[0], *argv[1:]]
        runpy.run_module("qminiwasm.engine.__main__", run_name="__main__", alter_sys=True)
    else:
        sys.stderr.write(_usage())
        sys.stderr.write(f"error: unknown command {argv[0]!r}\n")
        sys.exit(2)
