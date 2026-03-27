from __future__ import annotations

import tempfile
import time
from pathlib import Path

from qminiwasm.wasm_host.tpem_bundle import read_tpem_bundle, write_tpem_bundle
from qminiwasm.wasm_host.trit_pack import pack_ternary_list


def test_bench_tpem_bundle_read_write():
    payload = pack_ternary_list(([1, 0, -1, 1, -1] * 200_000))
    with tempfile.TemporaryDirectory() as td:
        p = Path(td) / "bench.tpem"
        t0 = time.perf_counter()
        write_tpem_bundle(p, payload)
        t1 = time.perf_counter()
        out = read_tpem_bundle(p)
        t2 = time.perf_counter()
    assert out.payload == payload
    print(
        f"bench_tpem_io bytes={len(payload)} "
        f"write_ms={(t1 - t0) * 1000:.2f} read_ms={(t2 - t1) * 1000:.2f}"
    )
