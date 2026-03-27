from __future__ import annotations

import random
import time

from qminiwasm.wasm_host.trit_pack import pack_ternary_list, unpack_ternary_list


def test_bench_pack_unpack_throughput():
    rng = random.Random(11)
    n = 200_000
    data = [rng.choice([-1, 0, 1]) for _ in range(n)]

    t0 = time.perf_counter()
    packed = pack_ternary_list(data)
    t1 = time.perf_counter()
    unpacked = unpack_ternary_list(packed, n)
    t2 = time.perf_counter()

    assert unpacked == data
    pack_ns_per_trit = (t1 - t0) * 1e9 / n
    unpack_ns_per_trit = (t2 - t1) * 1e9 / n
    print(
        f"bench_trit_pack n={n} pack_ns_per_trit={pack_ns_per_trit:.2f} "
        f"unpack_ns_per_trit={unpack_ns_per_trit:.2f}"
    )
