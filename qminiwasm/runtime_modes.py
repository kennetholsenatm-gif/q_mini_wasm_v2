"""Runtime toggles: prefer ``configs/runtime.toml``; env overrides when set (CI / WUI injection).

See ``docs/CONFIGURATION_POLICY.md`` and ``configs/runtime.toml``.
"""

from __future__ import annotations

import os
from pathlib import Path
from typing import Any, Mapping

_IMPL_VALUES = frozenset({"auto", "python", "native"})

# Fallback when ``configs/runtime.toml`` is missing (embedded defaults match file).
_OPTIMIZED_AUTO_DEFAULTS = {
    "QMINIWASM_TERNARY_IMPL": "auto",
    "QMINIWASM_TRIT_PACK_IMPL": "auto",
    "QMINIWASM_MEMORY_ENCODE_IMPL": "auto",
    "QMINIWASM_WASM_EXEC_IMPL": "auto",
    "QMINIWASM_CASCADE_RL_IMPL": "auto",
    "QMINIWASM_TPEM_NATIVE_BUNDLE": "0",
}

_RUNTIME_DATA: dict[str, Any] | None = None


def reset_runtime_config_cache() -> None:
    """Clear parsed ``runtime.toml`` (tests or hot reload)."""
    global _RUNTIME_DATA
    _RUNTIME_DATA = None


def _read_toml_file(path: Path) -> dict[str, Any]:
    try:
        import tomllib  # Python 3.11+
    except ImportError:
        import tomli as tomllib  # type: ignore[no-redef,import-untyped]

    with path.open("rb") as f:
        raw = tomllib.load(f)
    return raw if isinstance(raw, dict) else {}


def _runtime_root() -> Path | None:
    env_root = os.getenv("QMW_REPO_ROOT", "").strip()
    if env_root:
        p = Path(env_root)
        if (p / "configs" / "runtime.toml").is_file():
            return p.resolve()
    here = Path(__file__).resolve()
    for base in [here.parent.parent, *here.parent.parent.parents][:10]:
        if (base / "configs" / "runtime.toml").is_file():
            return base.resolve()
    cwd = Path.cwd()
    if (cwd / "configs" / "runtime.toml").is_file():
        return cwd.resolve()
    return None


def _load_runtime_toml() -> dict[str, Any]:
    global _RUNTIME_DATA
    if _RUNTIME_DATA is not None:
        return _RUNTIME_DATA
    root = _runtime_root()
    if root is None:
        _RUNTIME_DATA = {}
        return _RUNTIME_DATA
    path = root / "configs" / "runtime.toml"
    try:
        _RUNTIME_DATA = _read_toml_file(path)
    except OSError:
        _RUNTIME_DATA = {}
    return _RUNTIME_DATA


def _get_nested(data: Mapping[str, Any], *keys: str, default: Any = None) -> Any:
    cur: Any = data
    for k in keys:
        if not isinstance(cur, Mapping):
            return default
        cur = cur.get(k)
    return default if cur is None else cur


def _truthy_string(s: str) -> bool:
    return s.strip().lower() in {"1", "true", "yes", "on"}


def _falsy_string(s: str) -> bool:
    return s.strip().lower() in {"", "0", "false", "off", "no"}


def resolve_impl_mode(env_name: str, default: str = "auto") -> str:
    raw = os.getenv(env_name)
    if raw is not None and str(raw).strip() != "":
        mode = str(raw).strip().lower()
        if mode in _IMPL_VALUES:
            return mode
        return default if default in _IMPL_VALUES else "auto"
    key = {
        "QMINIWASM_TERNARY_IMPL": "ternary_impl",
        "QMINIWASM_TRIT_PACK_IMPL": "trit_pack_impl",
        "QMINIWASM_MEMORY_ENCODE_IMPL": "memory_encode_impl",
        "QMINIWASM_WASM_EXEC_IMPL": "wasm_exec_impl",
        "QMINIWASM_CASCADE_RL_IMPL": "cascade_rl_impl",
    }.get(env_name)
    if not key:
        d = default if default in _IMPL_VALUES else "auto"
        return d
    v = _get_nested(_load_runtime_toml(), "runtime", "impl", key)
    if v is None:
        return default if default in _IMPL_VALUES else "auto"
    mode = str(v).strip().lower()
    return mode if mode in _IMPL_VALUES else (default if default in _IMPL_VALUES else "auto")


def strict_native_enabled() -> bool:
    raw = os.getenv("QMINIWASM_NATIVE_STRICT")
    if raw is not None and str(raw).strip() != "":
        return _truthy_string(str(raw))
    v = _get_nested(_load_runtime_toml(), "runtime", "native_strict")
    if isinstance(v, bool):
        return v
    return False


def apply_optimized_auto_defaults() -> None:
    """Populate unset ``QMINIWASM_*`` impl keys from ``runtime.toml`` (then built-in fallbacks)."""
    data = _load_runtime_toml()
    impl = _get_nested(data, "runtime", "impl", default={})
    if not isinstance(impl, Mapping):
        impl = {}

    def impl_val(key: str, env_key: str) -> str:
        if key in impl and impl[key] is not None:
            return str(impl[key]).strip()
        return _OPTIMIZED_AUTO_DEFAULTS.get(env_key, "auto")

    merged = {
        "QMINIWASM_TERNARY_IMPL": impl_val("ternary_impl", "QMINIWASM_TERNARY_IMPL"),
        "QMINIWASM_TRIT_PACK_IMPL": impl_val("trit_pack_impl", "QMINIWASM_TRIT_PACK_IMPL"),
        "QMINIWASM_MEMORY_ENCODE_IMPL": impl_val(
            "memory_encode_impl", "QMINIWASM_MEMORY_ENCODE_IMPL"
        ),
        "QMINIWASM_WASM_EXEC_IMPL": impl_val("wasm_exec_impl", "QMINIWASM_WASM_EXEC_IMPL"),
        "QMINIWASM_CASCADE_RL_IMPL": impl_val("cascade_rl_impl", "QMINIWASM_CASCADE_RL_IMPL"),
        "QMINIWASM_TPEM_NATIVE_BUNDLE": impl_val(
            "tpem_native_bundle", "QMINIWASM_TPEM_NATIVE_BUNDLE"
        ),
    }
    for k, v in merged.items():
        os.environ.setdefault(k, str(v))

    ns = _get_nested(data, "runtime", "native_strict")
    if isinstance(ns, bool):
        os.environ.setdefault("QMINIWASM_NATIVE_STRICT", "1" if ns else "0")


def tpem_native_bundle_enabled() -> bool:
    raw = os.getenv("QMINIWASM_TPEM_NATIVE_BUNDLE", "").strip().lower()
    if raw != "":
        return not _falsy_string(raw)
    v = _get_nested(_load_runtime_toml(), "runtime", "impl", "tpem_native_bundle")
    if v is None:
        return True
    s = str(v).strip().lower()
    return not _falsy_string(s)


def wasm_exec_impl_mode() -> str:
    return resolve_impl_mode("QMINIWASM_WASM_EXEC_IMPL", "auto")


def enclave_adapter_enabled() -> bool:
    raw = os.getenv("QMINIWASM_ENCLAVE_ADAPTER")
    if raw is not None and str(raw).strip() != "":
        return _truthy_string(str(raw))
    v = _get_nested(_load_runtime_toml(), "training", "enclave_adapter")
    if isinstance(v, bool):
        return v
    return False


def strict_xpu_training() -> bool:
    raw = os.getenv("QMINIWASM_STRICT_XPU")
    if raw is not None and str(raw).strip() != "":
        return _truthy_string(str(raw))
    v = _get_nested(_load_runtime_toml(), "training", "strict_xpu")
    if isinstance(v, bool):
        return v
    return False


def strict_config_validation() -> bool:
    raw = os.getenv("QMINIWASM_STRICT_CONFIG_VALIDATION")
    if raw is not None and str(raw).strip() != "":
        return _truthy_string(str(raw))
    v = _get_nested(_load_runtime_toml(), "training", "strict_config_validation")
    if isinstance(v, bool):
        return v
    return False


def tpem_latest_every_n_epochs() -> int:
    raw = os.getenv("QMINIWASM_TPEM_LATEST_EVERY_N_EPOCHS", "").strip()
    if raw:
        try:
            return max(1, int(raw))
        except ValueError:
            return 1
    v = _get_nested(_load_runtime_toml(), "training", "tpem_latest_every_n_epochs")
    try:
        return max(1, int(v)) if v is not None else 1
    except (TypeError, ValueError):
        return 1


def strict_enclave_footprint() -> bool:
    raw = os.getenv("QMINIWASM_STRICT_ENCLAVE_FOOTPRINT")
    if raw is not None and str(raw).strip() != "":
        return _truthy_string(str(raw))
    v = _get_nested(_load_runtime_toml(), "training", "strict_enclave_footprint")
    if isinstance(v, bool):
        return v
    return False


def assert_zero_mock_ratio_gate_enabled() -> bool:
    raw = os.getenv("QMINIWASM_ASSERT_ZERO_MOCK_RATIO")
    if raw is not None and str(raw).strip() != "":
        return _truthy_string(str(raw))
    v = _get_nested(_load_runtime_toml(), "training", "strict_mock_gate")
    if isinstance(v, bool):
        return v
    return False


def taxonomy_tier() -> str:
    raw = os.getenv("QMINIWASM_TAXONOMY_TIER", "").strip()
    if raw:
        return raw.lower()
    v = _get_nested(_load_runtime_toml(), "fabric", "taxonomy_tier")
    if v is not None and str(v).strip():
        return str(v).strip().lower()
    return "edge_constrained"


def native_dqaoa_routing_enabled() -> bool:
    raw = os.getenv("QMINIWASM_NATIVE_DQAOA_ROUTING")
    if raw is not None and str(raw).strip() != "":
        return not _falsy_string(str(raw))
    v = _get_nested(_load_runtime_toml(), "fabric", "native_dqaoa_routing")
    if isinstance(v, bool):
        return v
    return False


def routing_latency_budget_ms() -> float:
    raw = os.getenv("QMW_ROUTING_LATENCY_BUDGET_MS", "").strip()
    if raw:
        try:
            return float(raw)
        except ValueError:
            pass
    v = _get_nested(_load_runtime_toml(), "fabric", "routing_latency_budget_ms")
    try:
        return float(v) if v is not None else 50.0
    except (TypeError, ValueError):
        return 50.0


def hf_loader_impl() -> str:
    raw = os.getenv("QMINIWASM_HF_LOADER_IMPL", "").strip().lower()
    if raw:
        return raw
    v = _get_nested(_load_runtime_toml(), "loader", "hf_loader_impl")
    return str(v).strip().lower() if v is not None else "auto"


def native_rl_runtime_enabled() -> bool:
    raw = os.getenv("QMINIWASM_NATIVE_RL_RUNTIME")
    if raw is not None and str(raw).strip() != "":
        return not _falsy_string(str(raw))
    v = _get_nested(_load_runtime_toml(), "training", "native_rollout")
    if isinstance(v, bool):
        return v
    return False


def native_lota_qaf_enabled() -> bool:
    raw = os.getenv("QMINIWASM_NATIVE_LOTA_QAF")
    if raw is not None and str(raw).strip() != "":
        return not _falsy_string(str(raw))
    v = _get_nested(_load_runtime_toml(), "native", "native_lota_qaf")
    if isinstance(v, bool):
        return v
    return False


def strict_sycl_helper() -> bool:
    raw = os.getenv("QMINIWASM_STRICT_SYCL_HELPER")
    if raw is not None and str(raw).strip() != "":
        return _truthy_string(str(raw))
    v = _get_nested(_load_runtime_toml(), "hardware", "strict_sycl_helper")
    if isinstance(v, bool):
        return v
    return False


def sycl_backend_name() -> str:
    raw = os.getenv("SYCL_BACKEND", "").strip()
    if raw:
        return raw.lower()
    v = _get_nested(_load_runtime_toml(), "hardware", "sycl_backend")
    if v is not None and str(v).strip():
        return str(v).strip().lower()
    return "sycl"
