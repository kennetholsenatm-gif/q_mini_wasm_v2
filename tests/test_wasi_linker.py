"""WASI import detection and wasmtime Linker instantiation."""

from __future__ import annotations

import unittest

import wasmtime

from qminiwasm.wasm.wasi_link import (
    build_clang_wasm_compile_command,
    instantiate_wasmtime_module,
    wasm_module_needs_wasi,
)


_WASI_RANDOM_WAT = """
(module
  (import "wasi_snapshot_preview1" "random_get" (func (param i32 i32) (result i32)))
  (memory 1)
  (export "memory" (memory 0))
  (func (export "run") (param i32 i32) (result i32)
    (drop (call 0 (i32.const 0) (i32.const 8)))
    (i32.load (i32.const 0))
  )
)
"""

_BARE_ADD_WAT = """
(module
  (func $add (param i32 i32) (result i32)
    local.get 0
    local.get 1
    i32.add)
  (export "add" (func $add)))
"""


class TestWasiLinker(unittest.TestCase):
    def test_wasm_module_needs_wasi_true(self) -> None:
        raw = wasmtime.wat2wasm(_WASI_RANDOM_WAT)
        store = wasmtime.Store()
        module = wasmtime.Module(store.engine, raw)
        self.assertTrue(wasm_module_needs_wasi(module))

    def test_wasm_module_needs_wasi_false(self) -> None:
        raw = wasmtime.wat2wasm(_BARE_ADD_WAT)
        store = wasmtime.Store()
        module = wasmtime.Module(store.engine, raw)
        self.assertFalse(wasm_module_needs_wasi(module))

    def test_instantiate_wasi_random_get_run(self) -> None:
        raw = wasmtime.wat2wasm(_WASI_RANDOM_WAT)
        store = wasmtime.Store()
        module = wasmtime.Module(store.engine, raw)
        instance = instantiate_wasmtime_module(store, module)
        run = instance.exports(store)["run"]
        a = run(store, 0, 0)
        b = run(store, 0, 0)
        self.assertIsInstance(a, int)
        self.assertIsInstance(b, int)

    def test_build_clang_bare_argv(self) -> None:
        cmd = build_clang_wasm_compile_command("/tmp/a.c", "/tmp/o.wasm", link="bare")
        self.assertEqual(cmd[0], "clang")
        self.assertIn("-target", cmd)
        self.assertIn("wasm32", cmd)

    def test_build_clang_wasip1_requires_sdk(self) -> None:
        with self.assertRaises(ValueError):
            build_clang_wasm_compile_command(
                "/tmp/a.c",
                "/tmp/o.wasm",
                link="wasip1",
                wasi_sdk_path=None,
            )


if __name__ == "__main__":
    unittest.main()
