#!/usr/bin/env python3
"""Extract packed TPEM payload bytes from a trainable checkpoint .pt (for Go edge-artifacts builder).

Writes raw pack_ternary_tensor output (no .tpem header). Used by training-wui / qmw-build-edge-artifacts
when --checkpoint is set; full builds without a checkpoint use deterministic synthetic weights in Go.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))


def main() -> int:
    ap = argparse.ArgumentParser(description="Write packed ternary payload bytes from checkpoint")
    ap.add_argument("checkpoint", type=Path, help="Trainable TPEM .pt (dict with ternary_expert.weight)")
    ap.add_argument(
        "--output",
        "-o",
        type=Path,
        required=True,
        help="Path to write raw packed bytes",
    )
    args = ap.parse_args()
    ck = args.checkpoint.resolve()
    if not ck.is_file():
        print(f"checkpoint not found: {ck}", file=sys.stderr)
        return 1
    import torch

    from qminiwasm.wasm_host.trit_pack import pack_ternary_tensor

    blob = torch.load(str(ck), map_location="cpu", weights_only=False)
    if not isinstance(blob, dict):
        print(f"checkpoint must be a dict trainable TPEM, got {type(blob).__name__}", file=sys.stderr)
        return 1
    te = blob.get("ternary_expert")
    if not isinstance(te, dict):
        print("checkpoint missing ternary_expert state_dict", file=sys.stderr)
        return 1
    w = te.get("weight")
    if w is None:
        print("checkpoint ternary_expert missing 'weight'", file=sys.stderr)
        return 1
    if not isinstance(w, torch.Tensor):
        w = torch.as_tensor(w, dtype=torch.float32)
    packed = pack_ternary_tensor(w)
    raw = bytes(int(x) for x in packed.cpu().flatten().tolist())
    out = args.output.resolve()
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(raw)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
