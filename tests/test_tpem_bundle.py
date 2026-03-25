"""Round-trip tests for TPEM on-disk bundle format."""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from qminiwasm.enclave.trit_pack import PACK_ENCODING_VERSION, pack_ternary_list
from qminiwasm.enclave.tpem_bundle import read_tpem_bundle, write_tpem_bundle


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


if __name__ == "__main__":
    unittest.main()
