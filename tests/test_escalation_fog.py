"""Fog escalation policy and semantic payload."""

import unittest

import torch

from qminiwasm.config import HierarchicalConfig
from qminiwasm.inference.edge import EdgeOutcome, fog_escalation_triggered, run_edge_cognitive_loop
from qminiwasm.inference.escalation import prepare_escalation_payload
from qminiwasm.inference.semantic_abstraction import attach_semantic_blob_to_state, build_semantic_blob


class TestFogEscalation(unittest.TestCase):
    def test_contradiction_triggers(self):
        cfg = HierarchicalConfig()
        self.assertFalse(fog_escalation_triggered({}, cfg))
        self.assertTrue(fog_escalation_triggered({"contradiction_detected": True}, cfg))

    def test_sequence_length_triggers(self):
        cfg = HierarchicalConfig(max_sequence_length_proxy=10)
        self.assertTrue(fog_escalation_triggered({"sequence_length_proxy": 11}, cfg))

    def test_semantic_blob_in_payload(self):
        st = {"loop_idx": 0, "execution_state": {}}
        attach_semantic_blob_to_state(st, device=torch.device("cpu"))
        p = prepare_escalation_payload(st)
        self.assertIn("semantic_blob", p)
        self.assertIsNotNone(p["semantic_blob"])

    def test_fog_outcome_in_loop(self):
        cfg = HierarchicalConfig(N_max_loops=5, max_sequence_length_proxy=1)

        def exec_block(_i):
            return 0, {"execution_state": {}, "sequence_length_proxy": 100}

        _, outcome, _, _ = run_edge_cognitive_loop(exec_block, lambda _s: 0.0, cfg)
        self.assertEqual(outcome, EdgeOutcome.ESCALATE_TO_FOG)
