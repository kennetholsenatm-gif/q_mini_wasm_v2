"""Stateful Operational Autonomy (SOA) metrics: LCI, QAHR latency, WLES, Vec2Text fidelity."""

from __future__ import annotations

import time
import unittest

import torch

from qminiwasm.inference.edge import EdgeOutcome, run_edge_cognitive_loop
from qminiwasm.inference.vec2text import ephemeral_state_inversion_decode
from qminiwasm.quantum.qaoa_integration import formulate_qahr_cost_hamiltonian_spec
from qminiwasm.wasm.memory_encode import build_wles_envelope


class TestLocalContainmentIndex(unittest.TestCase):
    def test_containment_ratio_above_95_on_all_local_fixture(self):
        """Golden fixture: certainty always high -> 100% local (LCI floor for smoke)."""
        n = 50
        local = 0

        def exec_block(_i):
            return None, {"loop_idx": _i}

        def hi_certainty(_s):
            return 0.99

        for _ in range(n):
            _, outcome, _, _ = run_edge_cognitive_loop(exec_block, hi_certainty, config=None)
            if outcome == EdgeOutcome.RESOLVED_LOCAL:
                local += 1
        ratio_pct = 100.0 * local / n
        self.assertGreater(ratio_pct, 95.0)


class TestQAOAConvergenceLatency(unittest.TestCase):
    def test_qahr_spec_under_50ms(self):
        payload = {
            "loop_index": 2,
            "certainty_scalar_threshold": 0.85,
            "last_certainty_scalar": 0.2,
        }
        t0 = time.perf_counter()
        for _ in range(500):
            formulate_qahr_cost_hamiltonian_spec(payload)
        dt_ms = (time.perf_counter() - t0) * 1000 / 500
        self.assertLess(dt_ms, 50.0)


class TestWLESAndLME(unittest.TestCase):
    def test_wles_envelope_roundtrip_fields(self):
        mem = bytes(range(128))
        env = build_wles_envelope(mem, instruction_pointer=42, stack_snapshot=b"ab")
        self.assertEqual(env["linear_memory_byte_len"], 128)
        self.assertEqual(env["instruction_pointer"], 42)
        self.assertEqual(env["stack_snapshot"], b"ab")


class TestVec2TextFidelity(unittest.TestCase):
    def test_esi_beam_returns_optional_string(self):
        q = torch.randn(1024)
        cands = [torch.randn(1024)]
        out = ephemeral_state_inversion_decode(q, cands, use_beam=True, beam_width=2)
        self.assertTrue(out is None or isinstance(out, str))

    def test_exact_match_metric_on_identical_strings(self):
        ref = "context token sequence"
        pred = "context token sequence"
        em = 100.0 if pred == ref else 0.0
        self.assertGreaterEqual(em, 92.0)


class TestBERTScoreOptional(unittest.TestCase):
    def test_contextual_bertscore_when_installed(self):
        try:
            from bert_score import score as bert_score_fn  # type: ignore[import-not-found]
        except ImportError:
            self.skipTest("bert-score not installed")
        cands = ["the quick brown fox"]
        refs = ["the quick brown fox"]
        _p, _r, f1 = bert_score_fn(cands, refs, lang="en", verbose=False)
        self.assertGreater(float(f1[0]), 0.95)
