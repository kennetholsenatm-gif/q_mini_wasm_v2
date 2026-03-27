from __future__ import annotations

import os
import time

from qminiwasm.wasm_host.memory_encode import encode_linear_memory


def test_bench_memory_encode():
    data = bytes((i % 256 for i in range(4088)))
    n = 2000
    t0 = time.perf_counter()
    for i in range(n):
        _ = encode_linear_memory(data, result_i32=i, first_arg=i // 3)
    t1 = time.perf_counter()
    us_per_call = (t1 - t0) * 1e6 / n
    impl = os.getenv("QMINIWASM_MEMORY_ENCODE_IMPL", "auto")
    print(f"bench_memory_encode impl={impl} n={n} us_per_call={us_per_call:.2f}")
