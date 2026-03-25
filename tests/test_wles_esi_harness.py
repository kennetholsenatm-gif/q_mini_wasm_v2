"""WLES envelope + CGE payload + ESI validation smoke tests (Tier A harness)."""

from __future__ import annotations

import json
import unittest
from datetime import datetime, timezone

from qminiwasm.config import HierarchicalConfig
from qminiwasm.cognitive.escalation import prepare_escalation_payload
from qminiwasm.cognitive.vec2text import validate_reconstructed_text
from qminiwasm.enclave.memory_encode import WLES_PAYLOAD_VERSION, build_wles_envelope


class TestWlesEsiHarness(unittest.TestCase):
    def test_wles_envelope_invariants(self):
        mem = bytes(range(256))
        env = build_wles_envelope(mem, stack_snapshot=b"\x00\x01", instruction_pointer=42)
        self.assertEqual(env["wles_payload_version"], WLES_PAYLOAD_VERSION)
        self.assertEqual(env["linear_memory_byte_len"], len(mem))
        self.assertEqual(env["format_version"], 1)
        self.assertEqual(env["linear_memory"], mem)

    def test_escalation_payload_carries_cge_metadata(self):
        st = {
            "loop_idx": 2,
            "execution_state": {
                "linear_memory": b"wasm-linear-slice",
                "stack_snapshot": None,
                "instruction_pointer": 8,
            },
            "last_certainty": 0.41,
        }
        cfg = HierarchicalConfig(T_conf=0.85)
        p = prepare_escalation_payload(st, cfg)
        self.assertEqual(p["format_version"], cfg.delta_format_version)
        self.assertIn("certainty_scalar_threshold", p)
        self.assertIn("T_conf", p)
        self.assertEqual(p["last_certainty_scalar"], 0.41)
        self.assertEqual(p["linear_memory"], b"wasm-linear-slice")

    def test_esi_schema_valid_reconstruction(self):
        text = json.dumps(
            {
                "state_id": "wles-harness-1",
                "timestamp": datetime.now(timezone.utc).isoformat(),
                "execution_state": {
                    "memory": {"slot": 1},
                    "stack": [],
                    "return_value": "ok",
                },
            }
        )
        ok, err = validate_reconstructed_text(text)
        self.assertTrue(ok, err)


if __name__ == "__main__":
    unittest.main()
