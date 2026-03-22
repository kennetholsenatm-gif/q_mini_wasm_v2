"""Synthetic MOPD teacher/student hooks for the cascade RL phase.

See docs/CASCADE_AND_MOPD.md for how these are used in run_training_loop.
"""

from __future__ import annotations

import torch

from .cascade_rl import CascadeHiddenFn


def build_noise_state_mopd_fns(
    noise_std: float = 0.05,
) -> tuple[CascadeHiddenFn, CascadeHiddenFn]:
    """Return (student_fn, teacher_fn) for MOPD feature matching on MDP state.

    Student maps state ``s`` to ``{"emb": s.unsqueeze(0)}`` (gradients flow).
    Teacher maps ``s`` to ``{"emb": (s + noise).unsqueeze(0)}`` with ``noise ~ N(0, noise_std^2)``, detached in the training step's teacher path via MOPDLoss (teacher tensors are stop-grad inside the loss).
    """

    std = float(noise_std)

    def student_h(s: torch.Tensor) -> dict[str, torch.Tensor]:
        return {"emb": s.unsqueeze(0)}

    def teacher_h(s: torch.Tensor) -> dict[str, torch.Tensor]:
        return {"emb": (s + std * torch.randn_like(s)).unsqueeze(0)}

    return student_h, teacher_h
