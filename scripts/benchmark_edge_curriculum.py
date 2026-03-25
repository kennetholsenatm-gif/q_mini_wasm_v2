"""Quick benchmark harness for edge curriculum presets.

When ``BENCHMARK_SOA=1``, also emits SOA smoke metrics: Local Containment Index (LCI),
QAHR Hamiltonian formulation latency, and WLES envelope size (Python path).
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import time
from pathlib import Path


def _soa_smoke_metrics() -> dict:
    from qminiwasm.cognitive.edge import EdgeOutcome, run_edge_cognitive_loop
    from qminiwasm.fabric.qaoa_integration import formulate_qahr_cost_hamiltonian_spec
    from qminiwasm.enclave.memory_encode import build_wles_envelope

    n = 30
    local = 0

    def exec_block(i):
        return i, {"loop_idx": i}

    def certainty(_s):
        return 0.96

    for _ in range(n):
        _, outcome, _, _ = run_edge_cognitive_loop(exec_block, certainty, config=None)
        if outcome == EdgeOutcome.RESOLVED_LOCAL:
            local += 1
    lci_pct = 100.0 * local / n

    payload = {"loop_index": 1, "certainty_scalar_threshold": 0.85, "last_certainty_scalar": 0.2}
    t0 = time.perf_counter()
    for _ in range(200):
        formulate_qahr_cost_hamiltonian_spec(payload)
    qaoa_ms = (time.perf_counter() - t0) * 1000 / 200

    blob = bytes(4096)
    env = build_wles_envelope(blob)
    t1 = time.perf_counter()
    _ = json.dumps(env, default=str)
    snap_ser_ms = (time.perf_counter() - t1) * 1000

    # RSS delta during WLES suspension: sample on Unix hosts in future (pre/post RSS).
    rss_delta_pct = None

    return {
        "local_containment_index_pct": lci_pct,
        "qaoa_hamiltonian_formulation_ms_avg": qaoa_ms,
        "wles_envelope_json_ms": snap_ser_ms,
        "wles_linear_bytes": len(blob),
        "rss_delta_suspended_pct_estimate": rss_delta_pct,
        "lme_note": "Linear Memory Efficiency requires long-horizon WASM integration tests",
    }


def run_one(cfg: Path) -> dict:
    t0 = time.perf_counter()
    p = subprocess.run(
        [sys.executable, "-m", "engine", "--config", str(cfg)],
        capture_output=True,
        text=True,
        check=False,
    )
    dt = time.perf_counter() - t0
    out = p.stdout or ""
    err = p.stderr or ""
    return {
        "config": str(cfg),
        "returncode": int(p.returncode),
        "elapsed_seconds": dt,
        "stdout_tail": out[-2000:],
        "stderr_tail": err[-1200:],
    }


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "--configs",
        nargs="+",
        default=[
            "configs/training/edge_fast_iter.toml",
            "configs/training/edge_full_curriculum_streaming.toml",
        ],
    )
    args = ap.parse_args()
    results = [run_one(Path(c)) for c in args.configs]
    out: dict = {"results": results}
    if os.environ.get("BENCHMARK_SOA", "").strip().lower() in ("1", "true", "yes"):
        out["soa_smoke"] = _soa_smoke_metrics()
    print(json.dumps(out, indent=2))
    bad = [r for r in results if r["returncode"] != 0]
    return 1 if bad else 0


if __name__ == "__main__":
    raise SystemExit(main())
