"""Tests for hierarchical inference: Tier 1 loop, delta compression, HullKV, run_hierarchical."""

import unittest

import torch

from qminiwasm.config import HierarchicalConfig
from qminiwasm.inference.edge import EdgeOutcome, run_edge_cognitive_loop
from qminiwasm.inference.escalation import prepare_escalation_payload
from qminiwasm.state.delta_compression import compress_deltas, delta_payload_struct
from qminiwasm.layers.attention import TropicalAttention
from qminiwasm.quantum.interconnect import StateMigrationInterconnect
from qminiwasm.data.pipeline import DataPipeline


class TestTier1CognitiveLoop(unittest.TestCase):
    """Tier 1: cognitive loop halts at N or T_conf."""

    def test_loop_halts_at_n(self):
        """Loop runs exactly N_max_loops when certainty never reaches T_conf."""
        cfg = HierarchicalConfig(N_max_loops=3, T_conf=0.99)

        def execute_one_block(loop_idx: int):
            return loop_idx, {"loop_idx": loop_idx}

        def low_certainty(state):
            return 0.1

        result, outcome, num_loops, last_state = run_edge_cognitive_loop(
            execute_one_block, low_certainty, config=cfg
        )
        self.assertEqual(outcome, EdgeOutcome.ESCALATE_TO_CLOUD)
        self.assertEqual(num_loops, 3)
        self.assertEqual(last_state["loop_idx"], 2)

    def test_loop_halts_at_confidence(self):
        """Loop stops when certainty >= T_conf."""
        cfg = HierarchicalConfig(N_max_loops=10, T_conf=0.8)

        def execute_one_block(loop_idx: int):
            return loop_idx, {"loop_idx": loop_idx}

        def certainty_second_step(state):
            return 0.9 if state.get("loop_idx", -1) >= 1 else 0.2

        result, outcome, num_loops, last_state = run_edge_cognitive_loop(
            execute_one_block, certainty_second_step, config=cfg
        )
        self.assertEqual(outcome, EdgeOutcome.RESOLVED_LOCAL)
        self.assertEqual(num_loops, 2)


class TestDeltaCompression(unittest.TestCase):
    """Tier 2: delta compression size and round-trip."""

    def test_compress_deltas_size_bounds(self):
        """Compressed deltas have length and checksum."""
        current = bytes(64)
        baseline = bytes(64)
        payload = compress_deltas(current, baseline, format_version=1)
        self.assertIsInstance(payload, delta_payload_struct)
        self.assertEqual(payload.format_version, 1)
        self.assertGreaterEqual(payload.length, 0)
        self.assertEqual(len(payload.checksum), 64)

    def test_compress_deltas_detects_changes(self):
        """Deltas when current differs from baseline."""
        baseline = bytes([0] * 32)
        current = bytearray(baseline)
        current[8:16] = b"changed"
        payload = compress_deltas(bytes(current), baseline, chunk_size=8)
        self.assertGreater(len(payload.deltas), 0)


class TestHullKVIngest(unittest.TestCase):
    """Tier 2: HullKV ingestion of (addr, value) deltas."""

    def test_ingest_deltas_and_forward(self):
        """Ingest (addr, value) pairs then forward pass uses them."""
        att = TropicalAttention(d_model=8, num_heads=2)
        pairs = [(0, b"\x01\x02"), (8, b"\x03\x04")]
        att.ingest_deltas(pairs)
        hidden = torch.randn(2, 4, 8)
        out = att(hidden)
        self.assertEqual(out.shape, (2, 4, 8))
        att.clear_delta_buffer()

    def test_clear_delta_buffer(self):
        """clear_delta_buffer resets internal buffers."""
        att = TropicalAttention(d_model=8, num_heads=2)
        att.ingest_deltas([(0, b"\x00")])
        att.clear_delta_buffer()
        self.assertEqual(att._delta_k.size(0), 0)


class TestHierarchicalFlow(unittest.TestCase):
    """End-to-end: escalation payload and mock hierarchical flow."""

    def test_prepare_escalation_payload(self):
        """prepare_escalation_payload returns dict with format_version and state."""
        captured = {
            "loop_idx": 5,
            "execution_state": {
                "linear_memory": b"\x00\x01",
                "stack_snapshot": None,
                "instruction_pointer": 0,
            },
        }
        payload = prepare_escalation_payload(captured)
        self.assertEqual(payload["format_version"], 1)
        self.assertEqual(payload["loop_index"], 5)
        self.assertEqual(payload["linear_memory"], b"\x00\x01")

    def test_state_migration_accept(self):
        """StateMigrationInterconnect.accept returns (addr, value) list."""
        inter = StateMigrationInterconnect()
        payload = {"linear_memory": b"\x01\x02\x03\x04\x05\x06\x07\x08"}
        deltas = inter.accept(payload)
        self.assertGreater(len(deltas), 0)
        self.assertTrue(
            all(isinstance(p[0], int) and isinstance(p[1], (bytes, type(None))) for p in deltas)
        )

    def test_data_pipeline_load_wasm_traces(self):
        """Data pipeline load_wasm_traces returns list of trace dicts."""
        pipe = DataPipeline()
        traces = pipe.load_wasm_traces(num_traces=3)
        self.assertEqual(len(traces), 3)
        for t in traces:
            self.assertIn("linear_memory", t)
            self.assertIn("stack_snapshot", t)


class TestFullHierarchicalPath(unittest.TestCase):
    """Integration test: edge -> state export -> router/QAOA -> ternary (full path)."""

    def test_full_pipeline_edge_to_cloud_inference(self):
        """Run edge cognitive loop -> escalation -> state migration -> hybrid inference (router + ternary)."""
        try:
            from qminiwasm import QMiniWASM
        except ImportError as e:
            self.skipTest(f"QMiniWASM import failed: {e}")
        model = QMiniWASM()
        cfg = HierarchicalConfig(N_max_loops=2, T_conf=0.99)

        # 1) Edge-style loop (no WASM): execute_one_block returns state; certainty stays low -> escalate
        def execute_one_block(loop_idx: int):
            return loop_idx, {"loop_idx": loop_idx, "linear_memory": bytes(64)}

        def low_certainty(state):
            return 0.1

        result, outcome, num_loops, last_state = run_edge_cognitive_loop(
            execute_one_block, low_certainty, config=cfg
        )
        self.assertEqual(outcome, EdgeOutcome.ESCALATE_TO_CLOUD)
        self.assertEqual(num_loops, 2)

        # 2) State export: escalation payload
        payload = prepare_escalation_payload(last_state)
        self.assertIn("format_version", payload)
        self.assertIn("linear_memory", payload)

        # 3) State migration accept -> deltas
        inter = StateMigrationInterconnect()
        deltas = inter.accept(payload)
        self.assertGreater(len(deltas), 0)

        # 4) Ingest into HullKV and run cloud path (router + ternary)
        if hasattr(model, "tropical_attention") and model.tropical_attention is not None:
            model.tropical_attention.ingest_deltas(deltas, device=model.device)
        continuation = torch.zeros(1, 4096, device=model.device)
        out = model.hybrid_inference(continuation)
        self.assertEqual(out.shape[0], 1)
        self.assertEqual(out.shape[1], 4096)


class TestRunHierarchical(unittest.TestCase):
    """run_hierarchical entry point (requires wasmtime for full flow)."""

    def test_run_hierarchical_escalation_path(self):
        """When outcome is ESCALATE, escalation_payload is set and cloud path runs."""
        try:
            import wasmtime
            from qminiwasm import QMiniWASM
        except ImportError:
            self.skipTest("wasmtime not installed")
        wat = (
            "(module (func $add (param i32 i32) (result i32) "
            "local.get 0 local.get 1 i32.add) "
            '(export "add" (func $add)))'
        )
        wasm_code = wasmtime.wat2wasm(wat)
        model = QMiniWASM()
        cfg = HierarchicalConfig(N_max_loops=2, T_conf=0.99)
        result, outcome, num_loops, last_state = model.run_hierarchical(
            wasm_code,
            "add",
            [2, 3],
            continuation_hidden_states=torch.zeros(1, 4096),
            config=cfg,
        )
        self.assertEqual(outcome, EdgeOutcome.ESCALATE_TO_CLOUD)
        self.assertIn("escalation_payload", last_state)
        self.assertEqual(num_loops, 2)
        out = model.inference_from_escalation(
            last_state["escalation_payload"], torch.zeros(1, 4096)
        )
        self.assertEqual(out.shape, (1, 4096))


if __name__ == "__main__":
    unittest.main()
