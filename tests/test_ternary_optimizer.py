"""Tests for ternary weight optimizer (classical combinatorial search and STE fallback)."""

import os
import sys
import unittest
import torch

# Import only the ternary_optimizer module to avoid pulling in qiskit via router
_REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
if _REPO_ROOT not in sys.path:
    sys.path.insert(0, _REPO_ROOT)
_ternary_path = os.path.join(_REPO_ROOT, "qminiwasm", "quantum", "ternary_optimizer.py")
import importlib.util

_spec = importlib.util.spec_from_file_location("ternary_optimizer", _ternary_path)
_ternary_mod = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_ternary_mod)

grover_ternary_optimizer_stub = _ternary_mod.grover_ternary_optimizer_stub
ternary_optimizer_classical = _ternary_mod.ternary_optimizer_classical
GroverTernaryOptimizer = _ternary_mod.GroverTernaryOptimizer


class TestTernaryOptimizer(unittest.TestCase):
    """Test ternary optimizer API and behavior."""

    def test_stub_returns_ternary_shape_and_values(self):
        """grover_ternary_optimizer_stub returns same shape and values in {-1, 0, 1}."""
        weight = torch.randn(4, 8)
        out = grover_ternary_optimizer_stub(weight, num_iterations=1)
        self.assertEqual(out.shape, weight.shape)
        self.assertTrue(torch.all((out >= -1) & (out <= 1)))
        self.assertTrue(torch.all(out.eq(-1) | out.eq(0) | out.eq(1)))

    def test_classical_no_loss_returns_ste(self):
        """ternary_optimizer_classical with target_loss_fn=None returns STE-style ternary."""
        weight = torch.randn(3, 6)
        out = ternary_optimizer_classical(weight, num_iterations=5, target_loss_fn=None)
        expected = grover_ternary_optimizer_stub(weight, 1)
        self.assertEqual(out.shape, expected.shape)
        self.assertTrue(torch.allclose(out, expected))

    def test_grover_optimizer_no_loss_returns_ste(self):
        """GroverTernaryOptimizer.forward(weight, None) returns STE-style ternary."""
        opt = GroverTernaryOptimizer(num_iterations=3)
        weight = torch.randn(2, 4)
        out = opt(weight, target_loss_fn=None)
        expected = grover_ternary_optimizer_stub(weight, 3)
        self.assertTrue(torch.allclose(out, expected))

    def test_classical_with_loss_uses_search(self):
        """When target_loss_fn is provided, classical search can improve over STE."""
        weight = torch.randn(2, 4)
        # Loss = L2 distance to target config; target favors zeros
        target = torch.zeros_like(weight)

        def loss_fn(w):
            return (w - target).pow(2).sum()

        ste_out = ternary_optimizer_classical(weight, 0, None)
        opt_out = ternary_optimizer_classical(weight, 20, target_loss_fn=loss_fn)
        ste_loss = loss_fn(ste_out).item()
        opt_loss = loss_fn(opt_out).item()
        # Optimizer should do at least as well as STE (often better)
        self.assertLessEqual(opt_loss, ste_loss + 1e-5)
        self.assertEqual(opt_out.shape, weight.shape)
        self.assertTrue(torch.all((opt_out >= -1) & (opt_out <= 1)))

    def test_grover_optimizer_with_loss_callable(self):
        """GroverTernaryOptimizer accepts a callable target_loss_fn."""
        opt = GroverTernaryOptimizer(num_iterations=5)
        weight = torch.randn(2, 4)
        target = torch.ones_like(weight)
        loss_fn = lambda w: (w - target).pow(2).sum()
        out = opt(weight, target_loss_fn=loss_fn)
        self.assertEqual(out.shape, weight.shape)
        self.assertTrue(torch.all((out >= -1) & (out <= 1)))
