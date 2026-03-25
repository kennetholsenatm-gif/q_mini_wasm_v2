"""Mesh QUBO smoke tests."""

import unittest

import torch

from qminiwasm.fabric.mesh_qubo import mesh_edge_selection_qubo


class TestMeshQubo(unittest.TestCase):
    def test_shapes(self):
        c = torch.tensor([0.1, 0.5, 0.2], dtype=torch.float64)
        ql, qq = mesh_edge_selection_qubo(c, num_select=1, lambda_cardinality=10.0)
        self.assertEqual(ql.shape, (3,))
        self.assertEqual(qq.shape, (3, 3))
