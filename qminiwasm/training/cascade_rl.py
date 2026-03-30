"""Cascade RL training step: toy MDP + optional MOPD feature loss (stub-friendly).

This module does not replace :func:`run_training_loop`; it provides building blocks
for on-policy cascade experiments and unit tests.
"""

from __future__ import annotations

from dataclasses import dataclass
import ctypes
import os
from typing import Any, Callable, Protocol

import torch
import torch.nn as nn
import torch.nn.functional as F

from ..rl.cascade_cispo import CascadeCISPO
from ..rl.cascade_grpo import CascadeGRPO
from ..native_bridge import load_native_lib
from qminiwasm.runtime_modes import native_rl_runtime_enabled, resolve_impl_mode
from .distillation import MOPDLoss


def _cascade_rl_impl_mode() -> str:
    return resolve_impl_mode("QMINIWASM_CASCADE_RL_IMPL", "auto")


class CascadePolicyFn(Protocol):
    """Policy: state vector -> logits over discrete actions."""

    def __call__(self, state: torch.Tensor) -> torch.Tensor: ...


class CascadeHiddenFn(Protocol):
    """Hidden dict for MOPD; keyed tensors [1, D] or [D]."""

    def __call__(self, state: torch.Tensor) -> dict[str, torch.Tensor]: ...


def hidden_digest_for_cascade(
    hidden_mean_1d: torch.Tensor,
    *,
    state_dim: int,
    d_model: int = 4096,
    device: torch.device | None = None,
) -> torch.Tensor:
    """Map mean hidden vector ``[d_model]`` to cascade state ``[state_dim]`` (truncate or zero-pad)."""
    h = hidden_mean_1d.flatten()[:d_model].to(dtype=torch.float32)
    take = min(int(state_dim), h.numel())
    out = h[:take].clone()
    if int(state_dim) > take:
        out = F.pad(out, (0, int(state_dim) - take))
    if device is not None:
        out = out.to(device)
    return out


@dataclass
class ToyRoutingEnv:
    """Minimal MDP for smoke tests: noisy transition, reward depends on action and state."""

    state_dim: int = 8
    num_actions: int = 4
    max_steps: int = 16
    device: torch.device | None = None
    initial_state: torch.Tensor | None = None

    def __post_init__(self) -> None:
        self._step = 0
        dev = self.device or torch.device("cpu")
        if self.initial_state is not None:
            self._s = self.initial_state.to(dev).flatten()[: self.state_dim].clone()
            if self._s.numel() < self.state_dim:
                self._s = F.pad(self._s, (0, self.state_dim - self._s.numel()))
        else:
            self._s = torch.randn(self.state_dim, device=dev)

    def reset(self) -> torch.Tensor:
        self._step = 0
        dev = self._s.device
        if self.initial_state is not None:
            raw = self.initial_state.to(dev).flatten()[: self.state_dim]
            self._s = raw.clone()
            if self._s.numel() < self.state_dim:
                self._s = F.pad(self._s, (0, self.state_dim - self._s.numel()))
        else:
            self._s = torch.randn(self.state_dim, device=dev)
        return self._s.clone()

    def step(self, action: int) -> tuple[torch.Tensor, float, bool, dict[str, Any]]:
        self._step += 1
        r = float(-0.01 * action + 0.1 * self._s.sum().item() / max(1, self.state_dim))
        self._s = self._s + 0.05 * torch.randn_like(self._s)
        done = self._step >= self.max_steps
        return self._s.clone(), r, done, {}


def _policy_device(policy: Any) -> torch.device:
    if isinstance(policy, nn.Module):
        return next(policy.parameters()).device
    return torch.device("cpu")


def cascade_rl_train_step(
    policy_logits_fn: CascadePolicyFn,
    optimizer: torch.optim.Optimizer,
    grpo: CascadeGRPO | None = None,
    *,
    cispo: CascadeCISPO | None = None,
    group_size: int = 4,
    env_factory: Callable[[], ToyRoutingEnv] | None = None,
    mopd: MOPDLoss | None = None,
    student_hidden_fn: CascadeHiddenFn | None = None,
    teacher_hidden_fn: CascadeHiddenFn | None = None,
    lambda_grpo: float = 1.0,
    lambda_mopd: float = 1.0,
) -> dict[str, float]:
    """One optimization step: roll out ``group_size`` trajectories, GRPO or CISPO + optional MOPD.

    When MOPD is enabled, aligns **final-state** hiddens per trajectory (batched mean for loss).

    Pass exactly one of ``grpo`` or ``cispo``. CISPO requires Python rollouts (stores actions and
    recomputes log-probs with grad); native rollout is disabled when ``cispo`` is set.
    """
    if (grpo is None) == (cispo is None):
        raise ValueError("cascade_rl_train_step: pass exactly one of grpo or cispo")
    norm_adv = grpo.cfg.normalize_advantage if grpo is not None else cispo.cfg.normalize_advantage
    if group_size < 2 and norm_adv:
        raise ValueError("group_size >= 2 required when normalize_advantage is True")

    dev = _policy_device(policy_logits_fn)

    impl_mode = _cascade_rl_impl_mode()
    use_native_rollout = native_rl_runtime_enabled()
    if impl_mode == "python":
        use_native_rollout = False
    if impl_mode == "native":
        use_native_rollout = True
    if cispo is not None:
        use_native_rollout = False
    if use_native_rollout and env_factory is None:
        lib = load_native_lib()
        if lib is not None and hasattr(lib, "qmw_rl_rollout_returns"):
            try:
                fn = lib.qmw_rl_rollout_returns
                fn.argtypes = [
                    ctypes.c_uint64,
                    ctypes.c_size_t,
                    ctypes.c_size_t,
                    ctypes.c_size_t,
                    ctypes.c_size_t,
                    ctypes.POINTER(ctypes.c_float),
                    ctypes.POINTER(ctypes.c_float),
                ]
                fn.restype = None
                state_dim = 8
                max_steps = 16
                returns_buf = (ctypes.c_float * group_size)()
                states_buf = (ctypes.c_float * (group_size * state_dim))()
                fn(
                    ctypes.c_uint64(int(os.getenv("SEED", "0"))),
                    ctypes.c_size_t(group_size),
                    ctypes.c_size_t(max_steps),
                    ctypes.c_size_t(state_dim),
                    ctypes.c_size_t(4),
                    ctypes.cast(returns_buf, ctypes.POINTER(ctypes.c_float)),
                    ctypes.cast(states_buf, ctypes.POINTER(ctypes.c_float)),
                )
                # Keep gradient-safe assembly in Torch by evaluating policy on emitted final states.
                final_states = torch.tensor(
                    [states_buf[i] for i in range(group_size * state_dim)],
                    device=dev,
                    dtype=torch.float32,
                ).reshape(group_size, state_dim)
                logprob_parts: list[torch.Tensor] = []
                for i in range(group_size):
                    logits = policy_logits_fn(final_states[i])
                    logp = torch.log_softmax(logits, dim=-1).mean() * float(max_steps)
                    logprob_parts.append(logp)
                logprob_tensor = torch.stack(logprob_parts)
                returns_tensor = torch.tensor(
                    [returns_buf[i] for i in range(group_size)],
                    device=dev,
                    dtype=logprob_tensor.dtype,
                )
                assert grpo is not None
                l_grpo, m_grpo = grpo(logprob_tensor, returns_tensor)
                loss = lambda_grpo * l_grpo
                metrics = {k: float(v.detach()) for k, v in m_grpo.items()}
                optimizer.zero_grad(set_to_none=True)
                loss.backward()
                optimizer.step()
                metrics["loss"] = float(loss.detach())
                metrics["native_rollout"] = 1.0
                return metrics
            except Exception:
                if impl_mode == "native":
                    raise RuntimeError(
                        "QMINIWASM_CASCADE_RL_IMPL=native requested but native rollout failed"
                    )
                pass

    logprob_sums: list[torch.Tensor] = []
    logprob_sums_old: list[torch.Tensor] = []
    returns: list[torch.Tensor] = []
    stud_rows: dict[str, list[torch.Tensor]] = {}
    teach_rows: dict[str, list[torch.Tensor]] = {}

    def make_env() -> ToyRoutingEnv:
        if env_factory is not None:
            return env_factory()
        return ToyRoutingEnv(device=dev)

    for _ in range(group_size):
        env = make_env()
        s = env.reset()
        if s.device != dev:
            s = s.to(dev)
        total_r = 0.0
        traj_log_probs: list[torch.Tensor] = []
        traj_states: list[torch.Tensor] = []
        traj_actions: list[torch.Tensor] = []
        traj_dtype: torch.dtype | None = None
        done = False
        while not done:
            logits = policy_logits_fn(s)
            dist = torch.distributions.Categorical(logits=logits)
            if cispo is not None:
                with torch.no_grad():
                    a = dist.sample()
                    old_lp = dist.log_prob(a)
                traj_dtype = old_lp.dtype
                traj_states.append(s.detach().clone())
                traj_actions.append(a.detach().clone())
                traj_log_probs.append(old_lp)
            else:
                a = dist.sample()
                lp = dist.log_prob(a)
                traj_dtype = lp.dtype
                traj_log_probs.append(lp)
            s_next, r, done, _ = env.step(int(a.item()))
            total_r += r
            s = s_next.to(dev) if s_next.device != dev else s_next

        if cispo is not None:
            new_parts: list[torch.Tensor] = []
            for st_t, ac_t in zip(traj_states, traj_actions):
                logits_n = policy_logits_fn(st_t)
                dist_n = torch.distributions.Categorical(logits=logits_n)
                new_parts.append(dist_n.log_prob(ac_t))
            logprob_sums.append(torch.stack(new_parts).sum())
            logprob_sums_old.append(torch.stack(traj_log_probs).sum().detach())
        else:
            logprob_sums.append(torch.stack(traj_log_probs).sum())
        rd = traj_dtype if traj_dtype is not None else torch.float32
        returns.append(torch.tensor(total_r, device=dev, dtype=rd))

        if mopd is not None and student_hidden_fn is not None and teacher_hidden_fn is not None:
            with torch.no_grad():
                th = teacher_hidden_fn(s)
            st = student_hidden_fn(s)
            for k, v in st.items():
                stud_rows.setdefault(k, []).append(v.reshape(1, -1))
            for k, v in th.items():
                teach_rows.setdefault(k, []).append(v.reshape(1, -1).detach())

    logprob_tensor = torch.stack(logprob_sums)
    returns_tensor = torch.stack(returns)
    if cispo is not None:
        old_tensor = torch.stack(logprob_sums_old)
        l_pi, m_pi = cispo(logprob_tensor, old_tensor, returns_tensor)
        loss = lambda_grpo * l_pi
        metrics = {k: float(v.detach()) for k, v in m_pi.items()}
    else:
        assert grpo is not None
        l_grpo, m_grpo = grpo(logprob_tensor, returns_tensor)
        loss = lambda_grpo * l_grpo
        metrics = {k: float(v.detach()) for k, v in m_grpo.items()}

    if mopd is not None and stud_rows and teach_rows:
        stud = {k: torch.cat(v, dim=0).mean(dim=0, keepdim=True) for k, v in stud_rows.items()}
        teach = {k: torch.cat(v, dim=0).mean(dim=0, keepdim=True) for k, v in teach_rows.items()}
        l_mopd, m_mopd = mopd(None, None, stud, teach)
        loss = loss + lambda_mopd * l_mopd
        for k, v in m_mopd.items():
            metrics[k] = float(v.detach())

    optimizer.zero_grad(set_to_none=True)
    loss.backward()
    optimizer.step()
    metrics["loss"] = float(loss.detach())
    return metrics


class TinyCascadePolicy(nn.Module):
    """Example policy for tests: MLP over state -> logits."""

    def __init__(self, state_dim: int, num_actions: int) -> None:
        super().__init__()
        self.state_dim = state_dim
        self.num_actions = num_actions
        self.net = nn.Sequential(
            nn.Linear(state_dim, 32),
            nn.Tanh(),
            nn.Linear(32, num_actions),
        )

    def forward(self, state: torch.Tensor) -> torch.Tensor:
        return self.net(state)


class CascadeRouter(nn.Module):
    """Learned map ``hidden[d_model] -> latent[state_dim] -> logits[num_actions]`` for cascade RL."""

    def __init__(
        self,
        *,
        d_model: int = 4096,
        state_dim: int = 8,
        num_actions: int = 4,
        hidden: int = 32,
    ) -> None:
        super().__init__()
        self.d_model = int(d_model)
        self.state_dim = int(state_dim)
        self.num_actions = int(num_actions)
        self.hidden = int(hidden)
        self.projector = nn.Linear(self.d_model, self.state_dim)
        self.body = nn.Sequential(
            nn.Linear(self.state_dim, self.hidden),
            nn.Tanh(),
            nn.Linear(self.hidden, self.num_actions),
        )

    def project_hidden(self, hidden_1d: torch.Tensor) -> torch.Tensor:
        """Map mean hidden ``[d_model]`` to latent state ``[state_dim]``."""
        dev = self.projector.weight.device
        dt = self.projector.weight.dtype
        h = hidden_1d.flatten()[: self.d_model].to(device=dev, dtype=dt)
        if h.shape[0] < self.d_model:
            h = F.pad(h, (0, self.d_model - h.shape[0]))
        return self.projector(h)

    def forward(self, state: torch.Tensor) -> torch.Tensor:
        """Logits from latent state ``[state_dim]`` (same contract as :class:`TinyCascadePolicy`)."""
        return self.body(state)
