"""Optional pybind11 ternary dot; always runs Python fallback parity."""

import unittest

from qminiwasm.hardware import native_ternary


class TestNativeTernary(unittest.TestCase):
    def test_dot_matches_python_reference(self):
        w = bytes([0, 1, 2, 1])
        a = bytes((int(x) & 0xFF) for x in [10, -3, 4, 5])
        off = 1
        y = native_ternary.dot_u8_i8(w, a, off)
        ref = sum(int(w[i]) * int.from_bytes([a[i]], "little", signed=True) - off for i in range(len(w)))
        self.assertEqual(y, ref)
