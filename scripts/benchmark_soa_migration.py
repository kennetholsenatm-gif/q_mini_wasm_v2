#!/usr/bin/env python3
"""Benchmark State Migration Latency (SML): WLES envelope build + optional zlib delta stub.

Run: python scripts/benchmark_soa_migration.py
"""

from __future__ import annotations

import argparse
import time
import zlib

from qminiwasm.enclave.memory_encode import build_wles_envelope


def main() -> None:
    p = argparse.ArgumentParser(description="SOA SML benchmark (WLES packaging)")
    p.add_argument("--bytes", type=int, default=262_144, help="linear memory size (default 256KiB)")
    p.add_argument("--compress", action="store_true", help="zlib-compress linear_memory blob")
    args = p.parse_args()
    mem = bytes(args.bytes)
    t0 = time.perf_counter()
    env = build_wles_envelope(mem)
    t1 = time.perf_counter()
    comp_len = None
    if args.compress:
        c0 = time.perf_counter()
        compressed = zlib.compress(env["linear_memory"], level=6)
        c1 = time.perf_counter()
        print(f"zlib_compress_ms: {(c1 - c0) * 1000:.3f} compressed_len: {len(compressed)}")
    print(f"wles_envelope_ms: {(t1 - t0) * 1000:.3f} linear_bytes: {env['linear_memory_byte_len']}")


if __name__ == "__main__":
    main()
