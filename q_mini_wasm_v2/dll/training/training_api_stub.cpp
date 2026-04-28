// Training DLL API - Stub: real training requires q_mini_wasm_v2_core + training_api.cpp build.
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

#ifdef TRAINING_API_EXPORTS
#define TRAINING_API __declspec(dllexport)
#else
#define TRAINING_API __declspec(dllimport)
#endif

extern "C" {

TRAINING_API int Training_InitSession(
    uint64_t* session_id_out,
    uint32_t num_experts,
    uint32_t top_k,
    uint32_t num_layers,
    uint32_t batch_size,
    bool lazy_init,
    bool continuous_mode,
    uint32_t epochs,
    double learning_rate,
    uint32_t checkpoint_interval,
    uint32_t context_window,
    uint32_t entanglement_tokens,
    uint32_t shadow_dim,
    uint32_t neurons_per_layer,
    uint32_t routing_qutrits,
    bool steane_correction,
    bool flash_cim,
    uint32_t worker_threads,
    const char* data_sources_toml_path,
    uint32_t moe_input_dim,
    uint32_t moe_output_dim,
    uint32_t moe_hidden_dim,
    uint32_t moe_expert_internal_layers,
    uint32_t moe_ff_active_internal_layers,
    uint32_t prefill_target_samples,
    uint32_t prefill_timeout_ms,
    uint32_t prefill_poll_ms,
    uint32_t max_acquisition_queue_depth,
    uint32_t max_raw_queue_depth,
    uint32_t max_train_queue_depth,
    uint64_t samples_per_epoch_or_zero,
    uint32_t training_micro_batch_cap,
    uint32_t training_collect_floor,
    bool training_timing_to_stderr,
    bool training_serial_experts,
    uint32_t training_sycl_route_mode,
    uint32_t training_sycl_trit_quant_min_moe_dim,
    uint32_t training_goodness_log_level,
    uint32_t training_allow_generated_negatives,
    uint64_t directory_max_lines,
    uint64_t max_jsonl_local_samples,
    uint32_t min_text_length,
    uint32_t max_text_length,
    uint32_t acquisition_threads,
    uint32_t perturbation_threads,
    uint32_t checkpoint_async_queue_max,
    uint32_t collect_empty_backoff_base_ms,
    uint32_t collect_empty_backoff_max_shift,
    uint32_t collect_empty_backoff_cap_ms,
    uint32_t metrics_heartbeat_sec
) {
    (void)session_id_out;
    (void)num_experts;
    (void)top_k;
    (void)num_layers;
    (void)batch_size;
    (void)lazy_init;
    (void)continuous_mode;
    (void)epochs;
    (void)learning_rate;
    (void)checkpoint_interval;
    (void)context_window;
    (void)entanglement_tokens;
    (void)shadow_dim;
    (void)neurons_per_layer;
    (void)routing_qutrits;
    (void)steane_correction;
    (void)flash_cim;
    (void)worker_threads;
    (void)data_sources_toml_path;
    (void)moe_input_dim;
    (void)moe_output_dim;
    (void)moe_hidden_dim;
    (void)moe_expert_internal_layers;
    (void)moe_ff_active_internal_layers;
    (void)prefill_target_samples;
    (void)prefill_timeout_ms;
    (void)prefill_poll_ms;
    (void)max_acquisition_queue_depth;
    (void)max_raw_queue_depth;
    (void)max_train_queue_depth;
    (void)samples_per_epoch_or_zero;
    (void)training_micro_batch_cap;
    (void)training_collect_floor;
    (void)training_timing_to_stderr;
    (void)training_serial_experts;
    (void)training_sycl_route_mode;
    (void)training_sycl_trit_quant_min_moe_dim;
    (void)training_goodness_log_level;
    (void)training_allow_generated_negatives;
    (void)directory_max_lines;
    (void)max_jsonl_local_samples;
    (void)min_text_length;
    (void)max_text_length;
    (void)acquisition_threads;
    (void)perturbation_threads;
    (void)checkpoint_async_queue_max;
    (void)collect_empty_backoff_base_ms;
    (void)collect_empty_backoff_max_shift;
    (void)collect_empty_backoff_cap_ms;
    (void)metrics_heartbeat_sec;
    printf("[Training] ERROR: Real training not compiled in this DLL\n");
    return -1;
}

TRAINING_API int Training_StartTraining(
    uint64_t session_id,
    uint32_t epochs,
    const char* data_path,
    bool enable_data_accumulation
) {
    (void)session_id;
    (void)epochs;
    (void)data_path;
    (void)enable_data_accumulation;
    printf("[Training] ERROR: Real training not implemented\n");
    return -1;
}

TRAINING_API int Training_GetProgress(
    uint64_t session_id,
    uint32_t* current_epoch_out,
    uint32_t* total_epochs_out,
    double* current_loss_out,
    uint64_t* samples_processed_out,
    bool* is_running_out,
    uint32_t* loop_count_out
) {
    (void)session_id;
    (void)current_epoch_out;
    (void)total_epochs_out;
    (void)current_loss_out;
    (void)samples_processed_out;
    (void)is_running_out;
    (void)loop_count_out;
    return -1;
}

TRAINING_API int Training_GetMetrics(
    uint64_t session_id,
    uint32_t* experts_active_out,
    uint32_t* data_acquired_out,
    uint32_t* data_perturbed_out,
    uint32_t* api_failures_out,
    uint32_t* betti_0_out,
    uint32_t* betti_1_out,
    uint32_t* betti_2_out,
    uint32_t* topic_frontier_size_out,
    uint32_t* topic_frontier_max_out,
    uint32_t* topic_frontier_evictions_out,
    uint32_t* ds_queue_depth_out,
    uint32_t* ds_raw_queue_depth_out,
    uint32_t* ds_raw_queue_max_out,
    uint32_t* ds_train_queue_max_out,
    uint32_t* ds_acq_queue_depth_out,
    uint32_t* ds_acq_queue_max_out,
    uint64_t* ds_blocked_raw_pushes_out,
    uint64_t* ds_blocked_train_pushes_out,
    uint64_t* ds_blocked_wait_ms_out,
    uint64_t* ds_dropped_payloads_out,
    uint64_t* ds_acq_blocked_pushes_out,
    uint64_t* ds_acq_blocked_wait_ms_out,
    uint64_t* samples_total_out,
    uint64_t* ds_acq_dropped_too_short_out,
    uint64_t* gf3_hebbian_weight_cell_updates_out,
    char* pipeline_status_utf8_out,
    size_t pipeline_status_utf8_cap
) {
    (void)session_id;
    (void)experts_active_out;
    (void)data_acquired_out;
    (void)data_perturbed_out;
    (void)api_failures_out;
    (void)betti_0_out;
    (void)betti_1_out;
    (void)betti_2_out;
    (void)topic_frontier_size_out;
    (void)topic_frontier_max_out;
    (void)topic_frontier_evictions_out;
    (void)ds_queue_depth_out;
    (void)ds_raw_queue_depth_out;
    (void)ds_raw_queue_max_out;
    (void)ds_train_queue_max_out;
    (void)ds_acq_queue_depth_out;
    (void)ds_acq_queue_max_out;
    (void)ds_blocked_raw_pushes_out;
    (void)ds_blocked_train_pushes_out;
    (void)ds_blocked_wait_ms_out;
    (void)ds_dropped_payloads_out;
    (void)ds_acq_blocked_pushes_out;
    (void)ds_acq_blocked_wait_ms_out;
    (void)samples_total_out;
    (void)ds_acq_dropped_too_short_out;
    (void)gf3_hebbian_weight_cell_updates_out;
    (void)pipeline_status_utf8_out;
    (void)pipeline_status_utf8_cap;
    return -1;
}

TRAINING_API int Training_StopTraining(uint64_t session_id) {
    (void)session_id;
    return -1;
}

TRAINING_API int Training_CleanupSession(uint64_t session_id) {
    (void)session_id;
    return -1;
}

TRAINING_API int Training_ExportCheckpoint(uint64_t session_id, const char* path_utf8) {
    (void)session_id;
    (void)path_utf8;
    return -1;
}

TRAINING_API int Training_ImportCheckpoint(uint64_t session_id, const char* path_utf8) {
    (void)session_id;
    (void)path_utf8;
    return -1;
}

TRAINING_API void Training_GetVersion(char* version_out, size_t max_len) {
    if (!version_out || max_len == 0) {
        return;
    }
    (void)snprintf(version_out, max_len, "q_training_stub %s %s", __DATE__, __TIME__);
    version_out[max_len - 1] = '\0';
}

TRAINING_API int Training_GetIngestionStats(
    uint64_t session_id,
    uint64_t* samples_total_out,
    uint32_t* ds_queue_depth_out,
    uint32_t* ds_raw_queue_depth_out,
    uint32_t* ds_raw_queue_max_out,
    uint32_t* ds_train_queue_max_out,
    uint32_t* ds_acq_queue_depth_out,
    uint32_t* ds_acq_queue_max_out,
    uint64_t* ds_blocked_raw_pushes_out,
    uint64_t* ds_blocked_train_pushes_out,
    uint64_t* ds_blocked_wait_ms_out,
    uint64_t* ds_dropped_payloads_out,
    uint64_t* ds_acq_blocked_pushes_out,
    uint64_t* ds_acq_blocked_wait_ms_out
) {
    (void)session_id;
    (void)samples_total_out;
    (void)ds_queue_depth_out;
    (void)ds_raw_queue_depth_out;
    (void)ds_raw_queue_max_out;
    (void)ds_train_queue_max_out;
    (void)ds_acq_queue_depth_out;
    (void)ds_acq_queue_max_out;
    (void)ds_blocked_raw_pushes_out;
    (void)ds_blocked_train_pushes_out;
    (void)ds_blocked_wait_ms_out;
    (void)ds_dropped_payloads_out;
    (void)ds_acq_blocked_pushes_out;
    (void)ds_acq_blocked_wait_ms_out;
    return -1;
}

} // extern "C"
