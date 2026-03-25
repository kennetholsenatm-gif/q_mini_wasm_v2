from __future__ import annotations

import numpy as np
import torch

from qminiwasm.fabric.mesh_qubo import prune_topology_from_edge_scores
from qminiwasm.fabric.qaoa_integration import NeuralQAOA, QAOAConfig


def test_prune_topology_mapping_reversible():
    scores = torch.tensor([0.9, 0.1, 0.8, 0.2, 0.7], dtype=torch.float32)
    pruned = prune_topology_from_edge_scores(scores, threshold=0.5, min_edges=2)
    assert pruned.kept_edge_indices == [0, 2, 4]
    assert pruned.new_to_old_edge == [0, 2, 4]
    assert pruned.old_to_new_edge[0] == 0
    assert pruned.old_to_new_edge[2] == 1
    assert pruned.old_to_new_edge[4] == 2
    assert pruned.old_to_new_edge[1] == -1


def test_neural_qaoa_cpp_cluster_first_runs():
    cfg = QAOAConfig(
        num_layers=2,
        execution_mode="cpp_cluster_first",
        use_neural_prediction=False,
        qaoa_prune_enabled=True,
        qaoa_prune_threshold=0.05,
    )
    nq = 6
    m = NeuralQAOA(nq, cfg)
    w = torch.randn(nq)
    out = m.quantum_circuit(w, m.gamma, m.beta)
    assert out.shape == (nq,)
    assert torch.isfinite(out).all()


def test_neural_qaoa_warm_start_cache_hit_updates_params():
    cfg = QAOAConfig(num_layers=2, execution_mode="cpp_cluster_first", use_neural_prediction=False)
    nq = 5
    m = NeuralQAOA(nq, cfg)
    w = torch.ones(nq)
    with torch.no_grad():
        m.gamma.fill_(0.123)
        m.beta.fill_(0.456)
    _ = m.forward(w)
    with torch.no_grad():
        m.gamma.zero_()
        m.beta.zero_()
    m._warm_start_angles_if_available(w)
    assert torch.allclose(m.gamma, torch.full_like(m.gamma, 0.123))
    assert torch.allclose(m.beta, torch.full_like(m.beta, 0.456))
