"""Emit corpus/scratch.wasm (bare wasm32, memory + exported run). Run from repo root or this directory."""

from __future__ import annotations

from pathlib import Path

import wasmtime

WAT = r"""
(module
  (memory 1)
  (func $run (param i32 i32) (result i32)
    (i32.store (i32.const 16) (i32.add (local.get 0) (local.get 1)))
    (return (i32.add (local.get 0) (local.get 1)))
  )
  (export "memory" (memory 0))
  (export "run" (func $run))
)
"""


def main() -> None:
    here = Path(__file__).resolve().parent
    out = here / "scratch.wasm"
    out.write_bytes(wasmtime.wat2wasm(WAT))
    print(f"Wrote {out}")


if __name__ == "__main__":
    main()
