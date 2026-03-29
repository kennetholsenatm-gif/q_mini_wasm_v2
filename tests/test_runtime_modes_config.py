from __future__ import annotations

import os
from unittest.mock import patch

from qminiwasm.runtime_modes import (
    reset_runtime_config_cache,
    resolve_impl_mode,
    routing_latency_budget_ms,
    strict_native_enabled,
    taxonomy_tier,
)


def _pop_env(key: str) -> str | None:
    return os.environ.pop(key, None)


def test_resolve_impl_reads_runtime_toml_when_env_unset():
    reset_runtime_config_cache()
    prev = _pop_env("QMINIWASM_TERNARY_IMPL")
    try:
        reset_runtime_config_cache()
        assert resolve_impl_mode("QMINIWASM_TERNARY_IMPL", "auto") == "auto"
    finally:
        if prev is not None:
            os.environ["QMINIWASM_TERNARY_IMPL"] = prev


def test_taxonomy_tier_default_from_toml():
    reset_runtime_config_cache()
    prev = _pop_env("QMINIWASM_TAXONOMY_TIER")
    try:
        reset_runtime_config_cache()
        assert taxonomy_tier() == "edge_constrained"
    finally:
        if prev is not None:
            os.environ["QMINIWASM_TAXONOMY_TIER"] = prev


def test_routing_budget_from_toml():
    reset_runtime_config_cache()
    prev = _pop_env("QMW_ROUTING_LATENCY_BUDGET_MS")
    try:
        reset_runtime_config_cache()
        assert routing_latency_budget_ms() == 50.0
    finally:
        if prev is not None:
            os.environ["QMW_ROUTING_LATENCY_BUDGET_MS"] = prev


def test_strict_native_env_overrides_toml():
    reset_runtime_config_cache()
    with patch.dict(os.environ, {"QMINIWASM_NATIVE_STRICT": "1"}):
        reset_runtime_config_cache()
        assert strict_native_enabled() is True
