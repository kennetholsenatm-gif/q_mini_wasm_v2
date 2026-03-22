#!/usr/bin/env python3
"""Print torch version and whether torch.xpu is available (Intel GPU for this build)."""

from __future__ import annotations

import sys


def main() -> int:
    try:
        import torch
    except ImportError:
        print("torch is not installed", file=sys.stderr)
        return 1

    print("torch.__version__:", torch.__version__)
    has_xpu = hasattr(torch, "xpu")
    print("torch has xpu module:", has_xpu)
    if not has_xpu:
        print("This PyTorch build has no XPU support. Install from https://download.pytorch.org/whl/xpu")
        return 2

    ok = bool(torch.xpu.is_available())
    print("torch.xpu.is_available():", ok)
    if ok:
        try:
            n = torch.xpu.device_count()
            print("torch.xpu.device_count():", n)
            for i in range(n):
                print(f"  [{i}]", torch.xpu.get_device_name(i))
        except Exception as e:
            print("Could not enumerate devices:", e)
    else:
        print("XPU not available: update Intel GPU drivers and ensure XPU wheels are installed (not CPU-only torch).")
        return 3
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
