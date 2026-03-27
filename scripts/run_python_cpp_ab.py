from __future__ import annotations

import json
import os
import re
import subprocess
import time
from dataclasses import dataclass, asdict
from typing import Dict, List, Optional


@dataclass
class ABResult:
    mode: str
    command: str
    returncode: int
    elapsed_ms: float
    stdout_tail: str
    stderr_tail: str
    metrics: Dict[str, float]
    native_readiness: Dict[str, object]
    fallback_count: int
    fallback_reasons: Dict[str, int]
    gate_pass: bool


def _extract_metric(pattern: str, text: str) -> Optional[float]:
    m = re.search(pattern, text)
    if not m:
        return None
    try:
        return float(m.group(1))
    except Exception:
        return None


def _parse_metrics(stdout: str) -> Dict[str, float]:
    metrics: Dict[str, float] = {}
    p = _extract_metric(r"pack_ns_per_trit=([0-9]+(?:\.[0-9]+)?)", stdout)
    u = _extract_metric(r"unpack_ns_per_trit=([0-9]+(?:\.[0-9]+)?)", stdout)
    me = _extract_metric(r"bench_memory_encode.*us_per_call=([0-9]+(?:\.[0-9]+)?)", stdout)
    st = _extract_metric(r"store_us=([0-9]+(?:\.[0-9]+)?)", stdout)
    lk = _extract_metric(r"lookup_us=([0-9]+(?:\.[0-9]+)?)", stdout)
    if p is not None:
        metrics["pack_ns_per_trit"] = p
    if u is not None:
        metrics["unpack_ns_per_trit"] = u
    if me is not None:
        metrics["memory_encode_us_per_call"] = me
    if st is not None:
        metrics["state_store_us"] = st
    if lk is not None:
        metrics["state_lookup_us"] = lk
    return metrics


def _run(cmd: List[str], env: Dict[str, str]) -> ABResult:
    caps = subprocess.run(
        [
            "python",
            "-c",
            "import json; from qminiwasm.native_bridge import native_capabilities; print(json.dumps(native_capabilities()))",
        ],
        capture_output=True,
        text=True,
        env=env,
    )
    try:
        readiness = json.loads(caps.stdout.strip() or "{}")
    except Exception:
        readiness = {"error": "native_capability_probe_failed"}
    t0 = time.perf_counter()
    p = subprocess.run(cmd, capture_output=True, text=True, env=env)
    elapsed = (time.perf_counter() - t0) * 1000.0
    metrics = _parse_metrics(p.stdout)
    reasons: Dict[str, int] = {}
    for m in re.finditer(r"fallback_reason=([a-zA-Z0-9_\\-]*)", p.stdout):
        reason = m.group(1) or "unknown"
        reasons[reason] = reasons.get(reason, 0) + 1
    gate_pass = p.returncode == 0
    return ABResult(
        mode=env.get("QMINIWASM_TERNARY_IMPL", "auto"),
        command=" ".join(cmd),
        returncode=int(p.returncode),
        elapsed_ms=elapsed,
        stdout_tail="\n".join(p.stdout.splitlines()[-30:]),
        stderr_tail="\n".join(p.stderr.splitlines()[-30:]),
        metrics=metrics,
        native_readiness=readiness,
        fallback_count=sum(reasons.values()),
        fallback_reasons=reasons,
        gate_pass=gate_pass,
    )


def _delta_pct(base: Optional[float], value: Optional[float]) -> Optional[float]:
    if base is None or value is None or base == 0:
        return None
    return (base - value) / base * 100.0


def main() -> None:
    cmd = [
        "python",
        "-m",
        "pytest",
        "tests/benchmarks/test_bench_trit_pack.py",
        "tests/benchmarks/test_bench_memory_encode.py",
        "tests/benchmarks/test_bench_runtime_cache_paths.py",
        "-q",
        "-s",
    ]
    base_env = os.environ.copy()
    out: List[ABResult] = []
    for mode in ("python", "native", "auto"):
        env = base_env.copy()
        env["QMINIWASM_TERNARY_IMPL"] = mode
        env["QMINIWASM_TRIT_PACK_IMPL"] = mode
        env["QMINIWASM_MEMORY_ENCODE_IMPL"] = mode
        out.append(_run(cmd, env))
    rows = [asdict(x) for x in out]
    py_metrics = out[0].metrics if out else {}
    for i, row in enumerate(rows):
        m = out[i].metrics
        row["deltas_vs_python_pct"] = {
            "pack": _delta_pct(py_metrics.get("pack_ns_per_trit"), m.get("pack_ns_per_trit")),
            "unpack": _delta_pct(py_metrics.get("unpack_ns_per_trit"), m.get("unpack_ns_per_trit")),
            "memory_encode": _delta_pct(
                py_metrics.get("memory_encode_us_per_call"),
                m.get("memory_encode_us_per_call"),
            ),
            "state_store": _delta_pct(py_metrics.get("state_store_us"), m.get("state_store_us")),
            "state_lookup": _delta_pct(py_metrics.get("state_lookup_us"), m.get("state_lookup_us")),
        }
    print(json.dumps(rows, indent=2))


if __name__ == "__main__":
    main()
