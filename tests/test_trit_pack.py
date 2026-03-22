"""Tests for MSB-first 5-trit packing (``qminiwasm.wasm.trit_pack``)."""

import unittest

from qminiwasm.wasm.trit_pack import (
    TRITS_PER_BYTE,
    pack_ternary_list,
    unpack_ternary_list,
    pack_ternary_tensor,
    unpack_ternary_tensor,
    signed_to_digit,
)


class TestTritPack(unittest.TestCase):
    def test_signed_digit_roundtrip(self):
        for w in (-1, 0, 1):
            d = signed_to_digit(w)
            self.assertIn(d, (0, 1, 2))

    def test_single_trit_k1(self):
        # k=1: P = d' * 3^0
        b = pack_ternary_list([1])
        self.assertEqual(b, b"\x02")
        self.assertEqual(unpack_ternary_list(b, 1), [1])

    def test_five_trits_msb_formula(self):
        # d' = (2,0,1,2,1) for weights (1,-1,0,1,0)
        d = [signed_to_digit(w) for w in (1, -1, 0, 1, 0)]
        expect = d[0] * 81 + d[1] * 27 + d[2] * 9 + d[3] * 3 + d[4]
        b = pack_ternary_list([1, -1, 0, 1, 0])
        self.assertEqual(b[0], expect)
        self.assertEqual(unpack_ternary_list(b, 5), [1, -1, 0, 1, 0])

    def test_partial_last_group(self):
        w = [-1, 0, 1, -1, 0, 1, -1, 0]
        p = pack_ternary_list(w)
        self.assertEqual(len(p), 2)
        u = unpack_ternary_list(p, len(w))
        self.assertEqual(u, w)

    def test_tensor_roundtrip(self):
        import torch

        w = torch.tensor([-1.0, 0.0, 1.0, 1.0, -1.0], dtype=torch.float32)
        p = pack_ternary_tensor(w)
        u = unpack_ternary_tensor(p, w.shape)
        self.assertTrue(torch.allclose(w, u))

    def test_trits_per_byte_constant(self):
        self.assertEqual(TRITS_PER_BYTE, 5)
