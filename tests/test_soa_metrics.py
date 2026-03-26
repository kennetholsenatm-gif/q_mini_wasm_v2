"""Stateful Operational Autonomy (SOA): LME / SML smoke tests."""

from __future__ import annotations

import os
import time
import unittest

from qminiwasm.wasm_host.memory_encode import build_wles_envelope


class TestStateMigrationLatency(unittest.TestCase):
    """State Migration Latency (SML): package WLES envelope under soft CI bounds."""

    def test_wles_envelope_small_payload_ms(self):
        mem = bytes(64 * 1024)
        t0 = time.perf_counter()
        env = build_wles_envelope(mem)
        dt_ms = (time.perf_counter() - t0) * 1000.0
        self.assertEqual(env["linear_memory_byte_len"], len(mem))
        self.assertLess(dt_ms, 500.0)


class TestLinearMemoryEfficiency(unittest.TestCase):
    """Linear Memory Efficiency (LME): RSS sampling when psutil is available."""

    def test_rss_delta_smoke(self):
        try:
            import psutil
        except ImportError:
            self.skipTest("psutil not installed")
        proc = psutil.Process(os.getpid())
        _ = proc.memory_info().rss
        # Soft smoke: RSS is a positive finite integer
        self.assertGreater(proc.memory_info().rss, 0)
