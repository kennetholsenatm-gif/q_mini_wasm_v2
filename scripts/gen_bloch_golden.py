#!/usr/bin/env python3
"""Emit cpp/training/test/data/bloch_golden.json for native Bloch attention parity tests.

Run from repo root after editing shapes if needed:
  python scripts/gen_bloch_golden.py
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

import torch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from qminiwasm.layers.bloch_attention import BlochSphereAttention  # noqa: E402


def main() -> None:
    d_model = 8
    num_heads = 2
    d_value = 4
    b, t = 1, 3
    torch.manual_seed(12345)
    m = BlochSphereAttention(d_model, num_heads, d_value=d_value, bias=True)
    x = torch.randn(b, t, d_model)
    with torch.no_grad():
        y = m(x)

    def lin_dict(tag: str, layer: torch.nn.Linear) -> dict:
        return {
            f"{tag}_weight": layer.weight.detach().flatten().tolist(),
            f"{tag}_bias": layer.bias.detach().flatten().tolist(),
        }

    out = {
        "d_model": d_model,
        "num_heads": num_heads,
        "d_value": d_value,
        "B": b,
        "T": t,
        "x": x.flatten().tolist(),
        "y": y.flatten().tolist(),
    }
    out.update(lin_dict("q_proj", m.q_proj))
    out.update(lin_dict("k_proj", m.k_proj))
    out.update(lin_dict("v_proj", m.v_proj))
    out.update(lin_dict("out_proj", m.out_proj))

    dest = ROOT / "cpp" / "training" / "test" / "data" / "bloch_golden.json"
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_text(json.dumps(out), encoding="utf-8")
    print("wrote", dest)


if __name__ == "__main__":
    main()
