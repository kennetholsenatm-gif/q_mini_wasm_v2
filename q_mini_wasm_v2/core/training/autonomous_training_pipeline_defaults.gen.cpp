// GENERATED FILE — do not edit by hand.
// Source: config/pipeline_defaults.toml
// Regenerate: python scripts/gen_pipeline_default_config.py (or CMake at configure time)

#include "autonomous_training_pipeline.hpp"

namespace q_mini_wasm_v2::core::training {

PipelineConfig default_pipeline_config() {
    PipelineConfig c{};

    c.acquisition_threads = 4;
    c.perturbation_threads = 2;
    c.ff_num_layers = 3;
    c.ff_layer_width = 128;
    c.ff_learning_rate_step = 1;
    c.learning_rate = 0.001f;
    c.moe_num_experts = 243;
    c.moe_top_k = 3;
    c.moe_input_dim = 64;
    c.moe_output_dim = 64;
    c.moe_hidden_dim = 128;
    c.moe_expert_internal_layers = 2;
    c.routing_qutrits = 16;
    c.graph_initial_nodes = 64;
    c.graph_initial_edges = 112;
    c.betti_max_qutrits = 243;
    c.betti_guidance_threshold = 15;
    c.shadow_dim = 64;
    c.batch_size = 32;
    c.num_epochs = 100;
    c.samples_per_epoch = 1000;
    c.training_micro_batch_cap = 0;
    c.training_collect_floor = 0;
    c.training_timing_to_stderr = false;
    c.training_sycl_route_mode = moe::SyclRouteMode::Auto;
    c.sycl_trit_quant_min_moe_dim = 128;
    c.goodness_log_level = 0;
    c.topology_evaluation_interval = 10;
    c.checkpoint_interval = 10;
    c.enable_prefill_ring_buffer = true;
    c.prefill_target_samples = 0;
    c.prefill_timeout_ms = 120000;
    c.prefill_poll_ms = 5;
    c.max_acquisition_queue_depth = 8388608;
    c.max_raw_queue_depth = 8388608;
    c.max_train_queue_depth = 16777216;
    c.enable_betti_guidance = true;
    c.enable_knowledge_engine = true;
    c.enable_checkpoints = true;
    c.enable_wui_streaming = true;
    c.enable_continuous_mode = false;
    c.enable_steane_correction = false;
    c.enable_error_correction = false;
    c.enable_flash_cim = false;
    c.lazy_moe_experts = true;
    c.moe_ff_active_internal_layers = 0;
    c.data_path = "";
    c.prefer_local_data = true;
    c.data_sources_toml_path = "";
    c.allow_generated_negatives = true;
    c.directory_max_lines = 1000000000ull;
    c.max_jsonl_local_samples = 5000000ull;
    c.min_text_length = 50;
    c.max_text_length = 100000;
    c.checkpoint_async_queue_max = 24;
    c.collect_empty_backoff_base_ms = 10;
    c.collect_empty_backoff_max_shift = 10;
    c.collect_empty_backoff_cap_ms = 1000;
    c.metrics_heartbeat_sec = 2;
    c.training_parallel_contrastive_rows = true;
    c.training_parallel_batches = 32;
    c.training_parallel_batches_runtime_cap = 0;
    c.ff_expert_chunk_size = 0;
    c.target_routes_per_batch = 0;
    c.collect_window_ms = 5;
    c.collect_min_rows_per_batch = 0u;
    c.training_checkpoint_data_dir = "C:/q_mini_data";
    c.sycl_prereserve_gib = 0;
    c.sycl_prereserve_chunk_mib = 2048;
    c.sycl_gpu_device_index = -1;
    c.gf3_sycl_min_weight_cells = 0ull;
    c.gf3_sycl_submit_grid_log = false;
    c.gf3_ff_batched_weight_mib = 65536ull;
    c.ff_multi_row_batch = true;
    c.ff_multi_row_slots_chunk = 0;
    c.gf3_ff_multislot_slots_chunk_max = 0ull;
    c.gf3_ff_multislot_ignore_host_slot_budget = false;
    c.lazy_moe_resident_cap = 0ull;
    c.training_auto_resume_from_checkpoint = true;
    c.prefill_progress_log_interval_sec = 2;
    c.prefill_stall_warn_sec = 8;
    c.collect_first_sample_timeout_sec = 30;
    c.collect_ff_coalesce_sleep_us = 10;
    c.training_pause_poll_ms = 10;
    c.training_idle_retry_ms = 1;
    c.training_empty_batch_log_interval = 100;
    c.training_empty_batch_metrics_interval = 25;
    c.train_queue_resync_discard_slack = 64;
    return c;
}

} // namespace q_mini_wasm_v2::core::training

