"""Fog escalation policy and semantic payload."""

import unittest

import torch

from qminiwasm.config import HierarchicalConfig
from qminiwasm.cognitive.edge import EdgeOutcome, fog_escalation_triggered, run_edge_cognitive_loop
from qminiwasm.cognitive.escalation import prepare_escalation_payload
from qminiwasm.cognitive.semantic_abstraction import attach_semantic_blob_to_state


class TestFogEscalation(unittest.TestCase):
    def test_contradiction_triggers(self):
        cfg = HierarchicalConfig()
        self.assertFalse(fog_escalation_triggered({}, cfg))
        self.assertTrue(fog_escalation_triggered({"contradiction_detected": True}, cfg))

    def test_sequence_length_triggers(self):
        cfg = HierarchicalConfig(max_sequence_length_proxy=10)
        self.assertTrue(fog_escalation_triggered({"sequence_length_proxy": 11}, cfg))

    def test_semantic_blob_in_payload(self):
        st = {"loop_idx": 0, "execution_state": {}, "last_certainty": 0.1}
        attach_semantic_blob_to_state(st, device=torch.device("cpu"))
        p = prepare_escalation_payload(st)
        self.assertIn("semantic_blob", p)
        self.assertIsNotNone(p["semantic_blob"])
        self.assertIn("certainty_scalar_threshold", p)
        self.assertIn("last_certainty_scalar", p)
        self.assertEqual(p["last_certainty_scalar"], 0.1)

    def test_fog_outcome_in_loop(self):
        cfg = HierarchicalConfig(N_max_loops=5, max_sequence_length_proxy=1)

        def exec_block(_i):
            return 0, {"execution_state": {}, "sequence_length_proxy": 100}

        _, outcome, _, _ = run_edge_cognitive_loop(exec_block, lambda _s: 0.0, cfg)
        self.assertEqual(outcome, EdgeOutcome.ESCALATE_TO_FOG)

    def test_local_containment_index_smoke(self):
        """Local Containment Index (LCI): fraction RESOLVED_LOCAL on a golden loop fixture.

        Production target > 95% on representative workloads; smoke uses high-certainty heuristic.
        """
        cfg = HierarchicalConfig(N_max_loops=10, T_conf=0.5)
        n = 40
        resolved = 0
        for _ in range(n):

            def exec_block(_i):
                return 0, {"execution_state": {}}

            _, outcome, _, _ = run_edge_cognitive_loop(exec_block, lambda _s: 0.99, cfg)
            if outcome == EdgeOutcome.RESOLVED_LOCAL:
                resolved += 1
        lci_pct = 100.0 * resolved / float(n)
        self.assertGreaterEqual(lci_pct, 95.0)
