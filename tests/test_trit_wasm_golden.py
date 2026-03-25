"""Golden tests: WASM trit kernels vs Python ``trit_pack``."""

import unittest

from qminiwasm.enclave.trit_pack import pack_ternary_list
from qminiwasm.enclave.trit_wasm_runtime import TritKernelInstance


class TestTritWasmGolden(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls._inst = TritKernelInstance.instantiate()

    def setUp(self):
        if self._inst is None:
            self.skipTest("wasmtime trit kernel instantiation failed")

    def test_pack5_matches_python(self):
        w = [1, -1, 0, 1, 0]
        py_b = pack_ternary_list(w)[0]
        wa_b = self._inst.pack5_msb(w)
        self.assertEqual(py_b, wa_b)

    def test_dot_offset_matches_ternary(self):
        # Per lane: digit * activation - offset_per_lane (constant bias per lane).
        w_u8 = bytes([0, 1, 2, 1])
        a = bytes((int(x) & 0xFF) for x in [10, -3, 4, 5])
        wasm_sum = self._inst.dot_u8_scalar(w_u8, a, offset_per_lane=1)

        def _s8(b: int) -> int:
            return int(b) - 256 if int(b) >= 128 else int(b)

        ref = sum(int(w) * _s8(a[i]) - 1 for i, w in enumerate(w_u8))
        self.assertEqual(wasm_sum, ref)

    def test_dispatch_binop(self):
        self.assertEqual(self._inst.dispatch_binop(0, 3, 5), 8)
        self.assertEqual(self._inst.dispatch_binop(1, 3, 5), -2)
        self.assertEqual(self._inst.dispatch_binop(2, 3, 5), 6)
