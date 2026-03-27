from __future__ import annotations

import time

import torch

from qminiwasm.data.pipeline import ESIStateRecoveryHull


def test_bench_state_cache_store_lookup():
    hull = ESIStateRecoveryHull()
    n = 500
    hs = [torch.randn(256) for _ in range(n)]
    ts = [torch.randn(256) for _ in range(n)]

    t0 = time.perf_counter()
    for h, t in zip(hs, ts):
        hull.store_valid_state(h, t)
    t1 = time.perf_counter()
    hits = 0
    for h in hs:
        if hull.retrieve_target_state(h) is not None:
            hits += 1
    t2 = time.perf_counter()

    assert hits == n
    print(
        f"bench_state_cache n={n} store_us={(t1 - t0) * 1e6 / n:.2f} "
        f"lookup_us={(t2 - t1) * 1e6 / n:.2f}"
    )
