"""Tropical Attention Layer

This module implements the TropicalAttention class (Pillar 3) which replaces
standard softmax attention with a geometric HullKVCache operating in max-plus
algebra. The attention heads used for WASM execution are restricted to a
strictly two-dimensional subspace (d_head = 2) and perform deterministic
convex-hull queries in O(log N) time.

Key properties:
- 2D subspace projection for Keys and Queries (d_head = 2)
- Max-plus (tropical) scoring: addition becomes max, multiplication becomes +
- Streaming Monotone-Chain (Andrew's) upper-hull maintenance
- O(log N) binary/ternary search over hull vertices to locate extreme tangent
- Value retrieval from the hull boundary, with interior points logically evicted

The implementation follows the mathematical formulation in the Q-Mini-WASM
white paper (Pillar 3: The Geometric HullKVCache).
"""

from __future__ import annotations

from typing import List, Optional, Tuple

import torch
import torch.nn as nn


class TropicalAttention(nn.Module):
    """Multi-head Tropical Attention with a geometric HullKVCache.

    This module operates on a sequence of hidden states and produces an output
    of the same dimensionality. Internally, it projects the input to:

    - 2D per-head Queries and Keys (d_head = 2)
    - High-dimensional per-head Values

    For each batch element and head, it maintains an upper convex hull over the
    2D Key projections using a streaming Monotone-Chain update. At each time
    step, it:

    1. Inserts the new Key point into the hull and pops hull points whose
       cross-product orientation becomes non-convex (cross >= 0).
    2. Executes an O(log H) ternary-search-style procedure over the hull
       vertices to find the point that maximizes the tropical score with the
       current Query.
    3. Retrieves the corresponding high-dimensional Value vector from that
       boundary point as the attention output for this head and token.

    Args:
        d_model: Input and output feature dimension.
        num_heads: Number of tropical attention heads.
        d_value: Per-head value dimension. If None, defaults to d_model // num_heads.
        bias: Whether to include bias terms in the projection layers.
        device: Optional torch.device for parameter initialization.
        dtype: Optional torch.dtype for parameter initialization.
    """

    _delta_k: torch.Tensor
    _delta_v: torch.Tensor

    def __init__(
        self,
        d_model: int,
        num_heads: int,
        d_value: int | None = None,
        bias: bool = True,
        device: torch.device | None = None,
        dtype: torch.dtype | None = None,
    ) -> None:
        super().__init__()

        if num_heads <= 0:
            raise ValueError("num_heads must be positive.")

        factory_kwargs = {"device": device, "dtype": dtype}

        # Restrict Keys and Queries to a strictly 2D subspace per head.
        d_head = 2
        self.d_model = d_model
        self.num_heads = num_heads
        self.d_head = d_head

        if d_value is None:
            if d_model % num_heads != 0:
                raise ValueError("d_model must be divisible by num_heads when d_value is None.")
            d_value = d_model // num_heads
        self.d_value = d_value

        # Linear projections into tropical attention space.
        self.q_proj = nn.Linear(d_model, num_heads * d_head, bias=bias, **factory_kwargs)
        self.k_proj = nn.Linear(d_model, num_heads * d_head, bias=bias, **factory_kwargs)
        self.v_proj = nn.Linear(d_model, num_heads * d_value, bias=bias, **factory_kwargs)

        # Output projection back to d_model.
        self.out_proj = nn.Linear(num_heads * d_value, d_model, bias=bias, **factory_kwargs)

        # Optional: (addr, value) deltas ingested from Tier 2 state migration (HullKV).
        self.register_buffer("_delta_k", torch.zeros(0, num_heads, 2, device=device, dtype=dtype))
        self.register_buffer(
            "_delta_v", torch.zeros(0, num_heads, d_value, device=device, dtype=dtype)
        )

    def ingest_deltas(
        self,
        addr_value_pairs: List[Tuple[int, Optional[bytes]]],
        device: Optional[torch.device] = None,
    ) -> None:
        """Ingest (address, value) delta pairs into HullKVCache for exact state retrieval.

        Projects each (addr, value) onto a 2D plane as Key and a d_value-sized Value
        vector, then appends to the internal delta buffer. On the next forward(),
        these points are used as prefix sequence so the convex hull includes them.

        Args:
            addr_value_pairs: List of (address, value) from StateMigrationInterconnect
                or compress_deltas. value is bytes or None.
            device: Device for new tensors; uses module device if None.
        """
        if not addr_value_pairs:
            return
        dev = device or next(self.parameters()).device
        dtype = next(self.parameters()).dtype
        scale = 1.0 / (2**20)
        keys: List[List[float]] = []
        vals: List[List[float]] = []
        for addr, val in addr_value_pairs:
            # Project (addr, value) to 2D key in [0,1] for hull stability
            v_int = 0
            if val is not None and len(val) > 0:
                for i, b in enumerate(val[:4]):
                    v_int += int(b) << (i * 8)
            k0 = (addr * scale) % 1.0
            k1 = (v_int * scale) % 1.0
            keys.append([k0, k1])
            # Value: pad to d_value (first dims from bytes, rest zero)
            v_vec = [0.0] * self.d_value
            if val is not None:
                for i in range(min(len(val), self.d_value)):
                    v_vec[i] = float(val[i]) / 255.0
            vals.append(v_vec)
        n = len(keys)
        K = torch.tensor(keys, dtype=dtype, device=dev)
        V = torch.tensor(vals, dtype=dtype, device=dev)
        K = K.unsqueeze(1).expand(n, self.num_heads, 2)
        V = V.unsqueeze(1).expand(n, self.num_heads, self.d_value)
        if self._delta_k.numel() == 0:
            self._delta_k = K
            self._delta_v = V
        else:
            self._delta_k = torch.cat([self._delta_k.to(dev), K], dim=0)
            self._delta_v = torch.cat([self._delta_v.to(dev), V], dim=0)

    def clear_delta_buffer(self) -> None:
        """Clear ingested delta buffer (e.g. after re-hydration and resume)."""
        dev = self._delta_k.device
        dtype = self._delta_k.dtype
        self._delta_k = torch.zeros(0, self.num_heads, 2, device=dev, dtype=dtype)
        self._delta_v = torch.zeros(0, self.num_heads, self.d_value, device=dev, dtype=dtype)

    def forward(self, hidden_states: torch.Tensor) -> torch.Tensor:
        """Forward pass of TropicalAttention.

        Args:
            hidden_states: Input tensor of shape (batch_size, seq_len, d_model).

        Returns:
            Output tensor of shape (batch_size, seq_len, d_model).
        """
        if hidden_states.dim() != 3:
            raise ValueError("hidden_states must be of shape (batch_size, seq_len, d_model).")

        batch_size, seq_len, d_model = hidden_states.shape
        if d_model != self.d_model:
            raise ValueError(
                f"Expected hidden_states feature dimension {self.d_model}, " f"but got {d_model}."
            )

        if seq_len == 0 and self._delta_k.numel() == 0:
            return hidden_states

        # Optional prefix from ingested (addr, value) deltas (Tier 2 state migration).
        delta_len = self._delta_k.size(0)
        d_k: Optional[torch.Tensor]
        d_v: Optional[torch.Tensor]
        if delta_len > 0:
            # Prepend delta keys/values as prefix to the sequence for hull.
            # Keys: (D, H, 2) -> (B, H, D, 2); Values: (D, H, d_value) -> (B, H, D, d_value)
            d_k = self._delta_k.unsqueeze(0).expand(batch_size, -1, -1, -1)
            d_v = self._delta_v.unsqueeze(0).expand(batch_size, -1, -1, -1)
        else:
            d_k = None
            d_v = None

        # Project to Queries, Keys, Values.
        q = self.q_proj(hidden_states)  # (B, T, H * 2)
        k = self.k_proj(hidden_states)  # (B, T, H * 2)
        v = self.v_proj(hidden_states)  # (B, T, H * d_value)

        # Reshape to (B, H, T, dim).
        q = q.view(batch_size, seq_len, self.num_heads, self.d_head).transpose(1, 2)
        k = k.view(batch_size, seq_len, self.num_heads, self.d_head).transpose(1, 2)
        v = v.view(batch_size, seq_len, self.num_heads, self.d_value).transpose(1, 2)

        if d_k is not None and d_v is not None:
            total_len = delta_len + seq_len
            k_full = torch.cat([d_k, k], dim=2)
            v_full = torch.cat([d_v, v], dim=2)
            q_full = torch.cat(
                [
                    torch.zeros(
                        batch_size, self.num_heads, delta_len, 2, device=q.device, dtype=q.dtype
                    ),
                    q,
                ],
                dim=2,
            )
        else:
            total_len = seq_len
            k_full = k
            v_full = v
            q_full = q

        # Output buffer: (B, H, T, d_value) — only over original seq_len for output.
        out = torch.empty(
            batch_size, self.num_heads, seq_len, self.d_value, device=v.device, dtype=v.dtype
        )

        # For each batch and head, maintain a streaming upper hull of Key indices.
        for b in range(batch_size):
            for h in range(self.num_heads):
                hull: List[int] = []
                k_seq = k_full[b, h]
                v_seq = v_full[b, h]
                q_seq = q_full[b, h]

                for t in range(delta_len):
                    hull = self._update_upper_hull(hull, k_seq, t)
                for t in range(delta_len, total_len):
                    hull = self._update_upper_hull(hull, k_seq, t)
                    best_index = self._argmax_tropical_on_hull(q_seq[t], k_seq, hull)
                    out[b, h, t - delta_len] = v_seq[best_index]

        # Merge heads and project back to d_model.
        out = out.transpose(1, 2).contiguous()  # (B, T, H, d_value)
        out = out.view(batch_size, seq_len, self.num_heads * self.d_value)
        out = self.out_proj(out)
        return out

    @staticmethod
    def _cross(p1: torch.Tensor, p2: torch.Tensor, p3: torch.Tensor) -> torch.Tensor:
        """Compute cross product for three 2D points (Monotone-Chain orientation test).

        Cross = (x2 - x1) * (y3 - y1) - (y2 - y1) * (x3 - x1)
        """
        x1, y1 = p1[0], p1[1]
        x2, y2 = p2[0], p2[1]
        x3, y3 = p3[0], p3[1]
        return (x2 - x1) * (y3 - y1) - (y2 - y1) * (x3 - x1)

    @classmethod
    def _update_upper_hull(
        cls,
        hull: List[int],
        k_seq: torch.Tensor,
        new_index: int,
    ) -> List[int]:
        """Streaming Monotone-Chain upper-hull update.

        Args:
            hull: Current list of hull vertex indices (in boundary order).
            k_seq: Tensor of Keys for a single (batch, head), shape (T, 2).
            new_index: Index of the new point to insert into the hull.

        Returns:
            Updated hull with non-convex interior points popped.
        """
        while len(hull) >= 2:
            p_k2 = k_seq[hull[-2]]
            p_k1 = k_seq[hull[-1]]
            p_k = k_seq[new_index]
            cross = cls._cross(p_k2, p_k1, p_k)
            # Pop P_{k-1} whenever cross >= 0 (non-convex inward orientation).
            if cross.item() >= 0.0:
                hull.pop()
            else:
                break

        hull.append(new_index)
        return hull

    @staticmethod
    def _tropical_score(q_vec: torch.Tensor, k_vec: torch.Tensor) -> torch.Tensor:
        """Compute max-plus (tropical) score between 2D Query and Key.

        In the max-plus semiring, multiplication becomes addition and addition
        becomes max. For 2D vectors Q = (q_x, q_y), K = (k_x, k_y), the score is:

            score(Q, K) = max(q_x + k_x, q_y + k_y)
        """
        s1 = q_vec[0] + k_vec[0]
        s2 = q_vec[1] + k_vec[1]
        return torch.maximum(s1, s2)

    @classmethod
    def _argmax_tropical_on_hull(
        cls,
        q_vec: torch.Tensor,
        k_seq: torch.Tensor,
        hull: List[int],
    ) -> int:
        """Locate the hull vertex that maximizes the tropical score with Query.

        This method performs an O(log H) search over the sorted hull vertices.
        It uses a discrete ternary-search-style strategy on the unimodal score
        profile induced by the convex hull in tropical projective space.

        Args:
            q_vec: Query vector for a single (batch, head, token), shape (2,).
            k_seq: Key sequence for a single (batch, head), shape (T, 2).
            hull: List of vertex indices describing the current upper hull.

        Returns:
            Index (into the original sequence) of the hull vertex that maximizes
            the tropical score with q_vec.
        """
        hull_len = len(hull)
        if hull_len == 0:
            raise RuntimeError("Hull must contain at least one vertex.")
        if hull_len == 1:
            return hull[0]

        def score_at_pos(pos: int) -> float:
            idx = hull[pos]
            return cls._tropical_score(q_vec, k_seq[idx]).item()

        left = 0
        right = hull_len - 1

        # Ternary-search-style loop: O(log H) evaluations.
        while right - left > 3:
            third = (right - left) // 3
            m1 = left + third
            m2 = right - third
            s1 = score_at_pos(m1)
            s2 = score_at_pos(m2)
            if s1 < s2:
                left = m1 + 1
            else:
                right = m2 - 1

        # Final linear scan over the narrowed interval [left, right].
        best_pos = left
        best_score = score_at_pos(left)
        for pos in range(left + 1, right + 1):
            s = score_at_pos(pos)
            if s > best_score:
                best_score = s
                best_pos = pos

        return hull[best_pos]
