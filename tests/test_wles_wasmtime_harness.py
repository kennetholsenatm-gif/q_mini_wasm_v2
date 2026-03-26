"""WLES Wasmtime harness: linear memory snapshot and restore (Roadmap milestone 1)."""

from __future__ import annotations

import pytest

from qminiwasm.wasm_host.memory_encode import WLES_PAYLOAD_VERSION
from qminiwasm.wasm_host.wles_wasmtime_harness import (
    call_export_i32,
    instantiate_minimal_wat,
    snapshot_wles_envelope_from_instance,
    wles_roundtrip_linear_memory_same_instance,
)
from qminiwasm.wasm_host.wles_wasmtime_harness import write_linear_memory

_WAT_POKE = r"""
(module
  (memory (export "memory") 1)
  (func (export "poke") (param i32 i32)
    (i32.store8 (local.get 0) (local.get 1))
  )
)
"""


@pytest.fixture(scope="module")
def wasmtime_available() -> None:
    pytest.importorskip("wasmtime")


def test_wles_envelope_fields_after_poke(wasmtime_available) -> None:
    store, _m, inst = instantiate_minimal_wat(_WAT_POKE)
    call_export_i32(store, inst, "poke", [200, 77])
    env = snapshot_wles_envelope_from_instance(store, inst, instruction_pointer=0)
    assert env["wles_payload_version"] == WLES_PAYLOAD_VERSION
    assert env["linear_memory_byte_len"] >= 201
    assert env["linear_memory"][200] == 77


def test_wles_roundtrip_same_instance_preserves_bytes(wasmtime_available) -> None:
    env = wles_roundtrip_linear_memory_same_instance(
        _WAT_POKE,
        run_export="poke",
        run_args=[64, 33],
        ip_after=None,
    )
    assert env["linear_memory"][64] == 33


def test_wles_restore_fresh_instance_overwrites_memory(wasmtime_available) -> None:
    store_a, _m_a, inst_a = instantiate_minimal_wat(_WAT_POKE)
    call_export_i32(store_a, inst_a, "poke", [50, 9])
    env = snapshot_wles_envelope_from_instance(store_a, inst_a)

    store_b, _m_b, inst_b = instantiate_minimal_wat(_WAT_POKE)
    call_export_i32(store_b, inst_b, "poke", [50, 1])
    assert snapshot_wles_envelope_from_instance(store_b, inst_b)["linear_memory"][50] == 1

    write_linear_memory(inst_b, store_b, env["linear_memory"])
    restored = snapshot_wles_envelope_from_instance(store_b, inst_b)["linear_memory"]
    assert restored[50] == 9
    assert restored == env["linear_memory"]
