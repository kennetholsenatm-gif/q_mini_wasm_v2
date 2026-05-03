#!/usr/bin/env python3
"""
Regenerate autonomous_training_pipeline_defaults.gen.cpp from config/pipeline_defaults.toml.

Requires Python 3.11+ (tomllib) or: pip install tomli

Usage (from repo root):
  python scripts/gen_pipeline_default_config.py
"""
from __future__ import annotations

import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
TOML_PATH = REPO_ROOT / "config" / "pipeline_defaults.toml"
# Repo layout: <root>/q_mini_wasm_v2/CMakeLists.txt, <root>/config/, <root>/scripts/
OUT_PATH = (
    REPO_ROOT / "q_mini_wasm_v2" / "core" / "training" / "autonomous_training_pipeline_defaults.gen.cpp"
)

try:
    import tomllib
except ModuleNotFoundError:  # Python < 3.11
    try:
        import tomli as tomllib  # type: ignore
    except ImportError as e:
        print("Need Python 3.11+ or: pip install tomli", file=sys.stderr)
        raise e


HEADER = """// GENERATED FILE — do not edit by hand.
// Source: config/pipeline_defaults.toml
// Regenerate: python scripts/gen_pipeline_default_config.py (or CMake at configure time)

#include \"autonomous_training_pipeline.hpp\"

namespace q_mini_wasm_v2::core::training {

PipelineConfig default_pipeline_config() {
    PipelineConfig c{};
"""

FOOTER = """    return c;
}

} // namespace q_mini_wasm_v2::core::training
"""

# Order matches logical grouping; must cover every key in [pipeline_defaults].
FIELD_ORDER = [
    "acquisition_threads",
    "perturbation_threads",
    "ff_num_layers",
    "ff_layer_width",
    "ff_learning_rate_step",
    "learning_rate",
    "moe_num_experts",
    "moe_top_k",
    "moe_input_dim",
    "moe_output_dim",
    "moe_hidden_dim",
    "moe_expert_internal_layers",
    "routing_qutrits",
    "graph_initial_nodes",
    "graph_initial_edges",
    "betti_max_qutrits",
    "betti_guidance_threshold",
    "shadow_dim",
    "batch_size",
    "num_epochs",
    "samples_per_epoch",
    "training_micro_batch_cap",
    "training_collect_floor",
    "training_timing_to_stderr",
    "training_sycl_route_mode",
    "sycl_trit_quant_min_moe_dim",
    "goodness_log_level",
    "topology_evaluation_interval",
    "checkpoint_interval",
    "enable_prefill_ring_buffer",
    "prefill_target_samples",
    "prefill_timeout_ms",
    "prefill_poll_ms",
    "max_acquisition_queue_depth",
    "max_raw_queue_depth",
    "max_train_queue_depth",
    "enable_betti_guidance",
    "enable_knowledge_engine",
    "enable_checkpoints",
    "enable_wui_streaming",
    "enable_continuous_mode",
    "enable_steane_correction",
    "enable_error_correction",
    "enable_flash_cim",
    "lazy_moe_experts",
    "moe_ff_active_internal_layers",
    "data_path",
    "prefer_local_data",
    "data_sources_toml_path",
    "allow_generated_negatives",
    "directory_max_lines",
    "max_jsonl_local_samples",
    "min_text_length",
    "max_text_length",
    "checkpoint_async_queue_max",
    "collect_empty_backoff_base_ms",
    "collect_empty_backoff_max_shift",
    "collect_empty_backoff_cap_ms",
    "metrics_heartbeat_sec",
    "training_parallel_contrastive_rows",
    "training_parallel_batches",
    "training_parallel_batches_runtime_cap",
    "ff_expert_chunk_size",
    "target_routes_per_batch",
    "collect_window_ms",
    "collect_min_rows_per_batch",
    "training_checkpoint_data_dir",
    "sycl_prereserve_gib",
    "sycl_prereserve_chunk_mib",
    "sycl_gpu_device_index",
    "gf3_sycl_min_weight_cells",
    "gf3_sycl_submit_grid_log",
    "gf3_ff_batched_weight_mib",
    "ff_multi_row_batch",
    "ff_multi_row_slots_chunk",
    "gf3_ff_multislot_slots_chunk_max",
    "gf3_ff_multislot_ignore_host_slot_budget",
    "lazy_moe_resident_cap",
    "training_auto_resume_from_checkpoint",
    "prefill_progress_log_interval_sec",
    "prefill_stall_warn_sec",
    "collect_first_sample_timeout_sec",
    "collect_ff_coalesce_sleep_us",
    "training_pause_poll_ms",
    "training_idle_retry_ms",
    "training_empty_batch_log_interval",
    "training_empty_batch_metrics_interval",
    "train_queue_resync_discard_slack",
]


def emit_line(name: str, value: object) -> str:
    if name == "training_sycl_route_mode":
        if not isinstance(value, str):
            raise SystemExit(f"training_sycl_route_mode must be string, got {value!r}")
        m = {
            "auto": "moe::SyclRouteMode::Auto",
            "on": "moe::SyclRouteMode::On",
        }.get(value.lower().strip())
        if m is None:
            raise SystemExit(f"Invalid training_sycl_route_mode: {value!r}")
        return f"    c.{name} = {m};"
    if name == "collect_min_rows_per_batch":
        if not isinstance(value, int):
            raise SystemExit(f"collect_min_rows_per_batch must be int, got {value!r}")
        return f"    c.{name} = {int(value)}u;"
    if name == "sycl_gpu_device_index":
        if not isinstance(value, int):
            raise SystemExit(f"sycl_gpu_device_index must be int, got {value!r}")
        return f"    c.{name} = {int(value)};"
    if name in (
        "gf3_sycl_min_weight_cells",
        "gf3_ff_batched_weight_mib",
        "gf3_ff_multislot_slots_chunk_max",
        "lazy_moe_resident_cap",
    ):
        if isinstance(value, bool):
            raise SystemExit(f"{name} must be integer")
        return f"    c.{name} = {int(value)}ull;"
    if name in ("data_path", "data_sources_toml_path", "training_checkpoint_data_dir"):
        if value is None:
            value = ""
        if not isinstance(value, str):
            raise SystemExit(f"{name} must be string")
        # Escape for C++ string literal
        esc = value.replace("\\", "\\\\").replace('"', '\\"')
        return f'    c.{name} = "{esc}";'
    if isinstance(value, bool):
        return f"    c.{name} = {'true' if value else 'false'};"
    if isinstance(value, int):
        if name in ("directory_max_lines", "max_jsonl_local_samples"):
            return f"    c.{name} = {value}ull;"
        return f"    c.{name} = {value};"
    if isinstance(value, float):
        return f"    c.{name} = {value!r}f;"
    raise SystemExit(f"Unsupported type for {name}: {type(value).__name__}")


def main() -> None:
    if not TOML_PATH.is_file():
        raise SystemExit(f"Missing {TOML_PATH}")

    raw = TOML_PATH.read_bytes()
    data = tomllib.loads(raw.decode("utf-8"))
    table = data.get("pipeline_defaults")
    if not isinstance(table, dict):
        raise SystemExit("Expected [pipeline_defaults] table")

    missing = [k for k in FIELD_ORDER if k not in table]
    if missing:
        raise SystemExit(f"Missing keys in TOML: {missing}")

    extra = [k for k in table if k not in FIELD_ORDER]
    if extra:
        raise SystemExit(f"Unknown keys in TOML (add to FIELD_ORDER or remove): {extra}")

    lines = [HEADER]
    for key in FIELD_ORDER:
        lines.append(emit_line(key, table[key]))
    lines.append(FOOTER)

    OUT_PATH.parent.mkdir(parents=True, exist_ok=True)
    text = "\n".join(lines) + "\n"
    OUT_PATH.write_text(text, encoding="utf-8")
    print(f"Wrote {OUT_PATH.relative_to(REPO_ROOT)}")


if __name__ == "__main__":
    main()
