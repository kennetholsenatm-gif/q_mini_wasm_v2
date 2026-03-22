"""PTQTP, Tequila forward, LoTA merge."""

import unittest

import torch

from qminiwasm.layers.lota import LoRALinearSide, merge_lora_into_linear_weight
from qminiwasm.layers.ptqtp import decompose_ptqtp, ptqtp_linear_forward, ptqtp_reconstruction_mse
from qminiwasm.layers.ternary import TernaryWASMExpert


class TestPTQTP(unittest.TestCase):
    def test_reconstruction_mse_decreases_with_planes(self):
        g = torch.Generator().manual_seed(0)
        W = torch.randn(12, 16, generator=g)
        m1, _ = ptqtp_reconstruction_mse(W, num_planes=1)
        m2, _ = ptqtp_reconstruction_mse(W, num_planes=2)
        self.assertLessEqual(m2, m1 + 1e-5)

    def test_forward_matches_explicit(self):
        g = torch.Generator().manual_seed(1)
        W = torch.randn(4, 6, generator=g)
        d = decompose_ptqtp(W, num_planes=2)
        x = torch.randn(2, 6, generator=g)
        y = ptqtp_linear_forward(x, d.to(x.device, x.dtype))
        W_hat = torch.zeros_like(W)
        for T, s in zip(d.planes, d.scales):
            W_hat = W_hat + s.unsqueeze(1).to(W_hat.dtype) * T.to(W_hat.dtype)
        y_ref = x @ W_hat.T
        self.assertTrue(torch.allclose(y, y_ref, atol=1e-4, rtol=1e-3))


class TestTequila(unittest.TestCase):
    def test_forward_runs_with_deadzone(self):
        layer = TernaryWASMExpert(8, 5, tequila_deadzone=0.25)
        x = torch.randn(3, 8)
        y = layer(x)
        self.assertEqual(y.shape, (3, 5))


class TestLoTAMerge(unittest.TestCase):
    def test_merge_zeros_b(self):
        base = torch.nn.Parameter(torch.randn(4, 3))
        lora = LoRALinearSide(3, 4, rank=2)
        with torch.no_grad():
            lora.lora_b.weight.copy_(torch.randn_like(lora.lora_b.weight))
        before = base.data.clone()
        with torch.no_grad():
            delta = lora.lora_b.weight @ lora.lora_a.weight
        merge_lora_into_linear_weight(base, lora)
        self.assertTrue(torch.allclose(base, before + delta, atol=1e-6))
        self.assertTrue(torch.all(lora.lora_b.weight == 0))
