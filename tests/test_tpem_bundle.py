"""Round-trip tests for TPEM on-disk bundle format."""

from __future__ import annotations

import os
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from qminiwasm.runtime_modes import reset_runtime_config_cache, tpem_native_bundle_enabled
from qminiwasm.wasm_host.trit_pack import PACK_ENCODING_VERSION, pack_ternary_list
from qminiwasm.wasm_host.tpem_bundle import read_tpem_bundle, write_tpem_bundle


class TestTpemBundle(unittest.TestCase):
    def test_roundtrip_temp_file(self):
        payload = pack_ternary_list([1, 0, -1, 1, 1, 0, 0, -1, 1])
        with tempfile.TemporaryDirectory() as td:
            p = Path(td) / "roundtrip.tpem"
            write_tpem_bundle(p, payload)
            b = read_tpem_bundle(p)
            self.assertEqual(b.payload, payload)
            self.assertEqual(b.pack_encoding_version, PACK_ENCODING_VERSION)

    def test_verify_rejects_truncation(self):
        with tempfile.TemporaryDirectory() as td:
            p = Path(td) / "bad.tpem"
            write_tpem_bundle(p, b"abc")
            raw = p.read_bytes()
            p.write_bytes(raw[:20])
            with self.assertRaises(ValueError):
                read_tpem_bundle(p)

    def test_roundtrip_accepts_memoryview_payload(self):
        payload = memoryview(pack_ternary_list([1, -1, 0, 1, 0, -1, 1, 1, 0]))
        with tempfile.TemporaryDirectory() as td:
            p = Path(td) / "mv.tpem"
            write_tpem_bundle(p, payload)
            b = read_tpem_bundle(p)
            self.assertEqual(b.payload, payload.tobytes())

    def test_native_toggle_disabled_false_values(self):
        for v in ("0", "false", "no", "off"):
            with self.subTest(v=v):
                with patch.dict("os.environ", {"QMINIWASM_TPEM_NATIVE_BUNDLE": v}):
                    reset_runtime_config_cache()
                    try:
                        self.assertFalse(tpem_native_bundle_enabled())
                    finally:
                        reset_runtime_config_cache()

    def test_native_toggle_enabled_when_unset_and_no_toml_override(self):
        """Env unset + no runtime.toml impl entry => enabled (legacy default)."""
        env_copy = {k: v for k, v in os.environ.items() if k != "QMINIWASM_TPEM_NATIVE_BUNDLE"}
        with patch("qminiwasm.runtime_modes._load_runtime_toml", return_value={}):
            reset_runtime_config_cache()
            try:
                with patch.dict(os.environ, env_copy, clear=True):
                    self.assertTrue(tpem_native_bundle_enabled())
            finally:
                reset_runtime_config_cache()

    def test_native_toggle_enabled_explicit_env_values(self):
        for v in ("1", "true", "yes", "on", "auto", "unexpected"):
            with self.subTest(v=v):
                with patch.dict("os.environ", {"QMINIWASM_TPEM_NATIVE_BUNDLE": v}):
                    reset_runtime_config_cache()
                    try:
                        self.assertTrue(tpem_native_bundle_enabled())
                    finally:
                        reset_runtime_config_cache()


if __name__ == "__main__":
    unittest.main()
