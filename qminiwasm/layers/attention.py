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

from typing import List

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

        if seq_len == 0:
            # Degenerate case: nothing to attend over.
            return hidden_states

        # Project to Queries, Keys, Values.
        q = self.q_proj(hidden_states)  # (B, T, H * 2)
        k = self.k_proj(hidden_states)  # (B, T, H * 2)
        v = self.v_proj(hidden_states)  # (B, T, H * d_value)

        # Reshape to (B, H, T, dim).
        q = q.view(batch_size, seq_len, self.num_heads, self.d_head).transpose(1, 2)
        k = k.view(batch_size, seq_len, self.num_heads, self.d_head).transpose(1, 2)
        v = v.view(batch_size, seq_len, self.num_heads, self.d_value).transpose(1, 2)

        # Output buffer: (B, H, T, d_value)
        out = torch.empty_like(v)

        # For each batch and head, maintain a streaming upper hull of Key indices.
        for b in range(batch_size):
            for h in range(self.num_heads):
                # Upper hull represented as a list of token indices into the sequence.
                hull: List[int] = []
                k_seq = k[b, h]  # (T, 2)
                v_seq = v[b, h]  # (T, d_value)
                q_seq = q[b, h]  # (T, 2)

                for t in range(seq_len):
                    # 1) Update convex hull with new point at index t.
                    hull = self._update_upper_hull(hull, k_seq, t)

                    # 2) Find hull vertex maximizing tropical score with current Query.
                    best_index = self._argmax_tropical_on_hull(q_seq[t], k_seq, hull)

                    # 3) Retrieve corresponding Value vector from boundary point.
                    out[b, h, t] = v_seq[best_index]

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
