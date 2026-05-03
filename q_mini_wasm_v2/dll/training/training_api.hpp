#pragma once

#ifndef TRAINING_API_H
#define TRAINING_API_H

#include <cstdint>
#include <cstddef>

#ifdef TRAINING_API_EXPORTS
#define TRAINING_API __declspec(dllexport)
#else
#define TRAINING_API __declspec(dllimport)
#endif

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Shared Global State (defined in training_api.cpp)
// ============================================================================
extern void* g_sessions_mutex;
extern void* g_sessions;
extern uint64_t g_next_session_id;

// ============================================================================
// Training Session Management
// ============================================================================

/**
 * Initialize a new training session with real C++ training code
 * 
 * @param session_id_out - Output: unique session ID
 * @param num_experts - Total number of MoE experts
 * @param top_k - Number of experts to activate (Top-K routing)
 * @param num_layers - Number of layers per expert
 * @param batch_size - Training batch size
 * @param lazy_init - Enable lazy initialization of experts
 * @param continuous_mode - Enable continuous training mode
 * @param epochs - Total epochs for training
 * @param learning_rate - Learning rate for optimizer
 * @param checkpoint_interval - Epochs between checkpoints
 * @param context_window - Model context window size
 * @param entanglement_tokens - Number of entanglement tokens
 * @param shadow_dim - Shadow dimension for stabilizer
 * @param neurons_per_layer - Neurons per layer
 * @param routing_qutrits - Number of routing qutrits
 * @param steane_correction - Enable Steane error correction
 * @param flash_cim - Enable Flash CIM optimization
 * @param worker_threads - Number of worker threads (maps to acquisition/perturbation thread counts)
 * @param data_sources_toml_path - Absolute path to data_sources.toml (UTF-8)
 * @param moe_input_dim - Expert input width
 * @param moe_output_dim - Expert output width
 * @param moe_hidden_dim - Expert hidden width
 * @param moe_expert_internal_layers - Layer count inside each expert FF stack
 * @param moe_ff_active_internal_layers - Cap internal FF layers per expert (0 = use full moe_expert_internal_layers)
 * @param samples_per_epoch_or_zero - Route-units per epoch (must be >= 1; from TOML training.samples_per_epoch)
 * @param training_micro_batch_cap - Row cap per process_batch vs batch_size (0 = use batch_size; from TOML training.micro_batch_cap)
 * @param training_collect_floor - Legacy ABI; stored on PipelineConfig but does not cap collect size (TOML training.collect_floor)
 * @param training_timing_to_stderr - Enable `[TrainingTiming]` stderr lines
 * @param training_sycl_route_mode - 0=auto, 1=on for symplectic routing logits (TOML training.sycl_route_mode; both use SYCL when moe_experts>=8)
 * @param training_sycl_trit_quant_min_moe_dim - Minimum moe_input_dim to use SYCL float/int32/string->trit quant (TOML training.sycl_trit_quant_min_moe_dim); 0 means default 128 in the DLL
 * @param training_goodness_log_level - 0=off, 1=stderr batch FF goodness summary, 2=+ first rows (TOML training.goodness_log_level; higher values reserved / clamped in pipeline)
 * @param training_allow_generated_negatives - Nonzero = allow generated negatives (stable FFI: uint32, not bool — bool before uint64 misaligns on Win64 CGO/MSVC)
 * @param directory_max_lines - Max directory corpus lines indexed (TOML `[training.data_filter].directory_max_lines`)
 * @param max_jsonl_local_samples - Max JSONL rows in RAM for single local file (TOML `training.max_jsonl_local_samples`)
 * @param min_text_length - Min feature / text length (TOML `[training.data_filter].min_text_length`)
 * @param max_text_length - Max feature / text length (TOML `[training.data_filter].max_text_length`)
 * @param acquisition_threads - DataSynthesizer acquisition pool size (TOML training.acquisition_threads)
 * @param perturbation_threads - DataSynthesizer perturbation pool size (TOML training.perturbation_threads)
 * @param checkpoint_async_queue_max - Pending checkpoint write jobs (TOML training.checkpoint_async_queue_max)
 * @param collect_empty_backoff_base_ms - Empty-queue backoff base (TOML)
 * @param collect_empty_backoff_max_shift - Empty-queue backoff exponent cap (TOML)
 * @param collect_empty_backoff_cap_ms - Empty-queue backoff ceiling ms (TOML)
 * @param metrics_heartbeat_sec - Metrics emit interval while waiting for samples; 0=every poll (TOML)
 * @param training_parallel_contrastive_rows - 0/1 OpenMP parallel rows (TOML training.parallel_contrastive_rows)
 * @param ff_expert_chunk_size - Experts per FF progress chunk; 0 = whole route (TOML training.ff_expert_chunk_size)
 * @param target_routes_per_batch - Route budget per batch (TOML training.target_routes_per_batch)
 * @param collect_window_ms - Collect-phase time slice before compute (TOML training.collect_window_ms)
 * @param collect_min_rows_per_batch - Minimum contrastive rows before timer can end collect (0 = derive to effective collect_limit; TOML training.collect_min_rows_per_batch)
 * @param training_checkpoint_data_dir_utf8 - Parent dir for `<parent>/checkpoints`; empty uses ./checkpoints under cwd — prefer `C:/q_mini_data` (TOML training.checkpoint_data_dir)
 * @return 0 on success, negative error code on failure (q_training.dll is always built with SYCL;
 *         -18 no usable SYCL GPU queue when routing requires GPU,
 *         -19 SYCL contrastive-negative probe failed (GPU mandatory),
 *         -20 invalid training.sycl_gpu_device_index or training.ff_multi_row_slots_chunk,
 *         -21 InitSession scalar ABI mismatch — rebuild qminiwasm.exe from the same commit as q_training.dll)
 */
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
    uint32_t metrics_heartbeat_sec,
    uint32_t training_parallel_contrastive_rows,
    uint32_t training_parallel_batches,
    uint32_t ff_expert_chunk_size,
    uint32_t target_routes_per_batch,
    uint32_t collect_window_ms,
    uint32_t collect_min_rows_per_batch,
    uint32_t training_sycl_prereserve_gib,
    /** MiB per `malloc_device` chunk while pre-reserving; 0 = use pipeline_defaults / generated default. */
    uint32_t training_sycl_prereserve_chunk_mib,
    int32_t training_sycl_gpu_device_index,
    uint64_t training_gf3_sycl_min_weight_cells,
    /** 0/1: when 1, throttled stderr for batched GF3 SYCL `parallel_for` grid sizing (`training.gf3_sycl_submit_grid_log`). */
    uint32_t training_gf3_sycl_submit_grid_log,
    uint64_t training_gf3_ff_batched_weight_mib,
    uint32_t training_ff_multi_row_batch,
    uint32_t training_ff_multi_row_slots_chunk,
    uint32_t training_gf3_ff_layers_per_batch,
    uint64_t training_lazy_moe_resident_cap,
    const char* training_checkpoint_data_dir_utf8,
    /** 0 = off; else hard ceiling on slots per multi-row SYCL FF chunk (see training.gf3_ff_multislot_slots_chunk_max). */
    uint64_t training_gf3_ff_multislot_slots_chunk_max,
    /** 0/1: when 1, skip host RAM estimate for that ceiling (see training.gf3_ff_multislot_ignore_host_slot_budget). */
    uint32_t training_gf3_ff_multislot_ignore_host_slot_budget,
    /** 0/1: when 1, `start_training` may auto-import the newest compatible checkpoint (see training.auto_resume_from_checkpoint). */
    uint32_t training_auto_resume_from_checkpoint,
    /** 0 = no extra cap on concurrent process_batch workers; else min(live_parallel_batches, this). See training.parallel_batches_runtime_cap. */
    uint32_t training_parallel_batches_runtime_cap
);

/**
 * Start training for a session
 * 
 * @param session_id - Session ID from Training_InitSession
 * @param epochs - Number of epochs to train
 * @param data_path - Path to training data file/directory
 * @param enable_data_accumulation - Enable data accumulation between cycles
 * @return 0 on success, negative error code on failure
 */
TRAINING_API int Training_StartTraining(
    uint64_t session_id,
    uint32_t epochs,
    const char* data_path,
    bool enable_data_accumulation
);

/**
 * Get current training progress
 * 
 * @param session_id - Session ID
 * @param current_epoch_out - Output: current epoch number
 * @param total_epochs_out - Output: total epochs
 * @param current_loss_out - Output: current loss value
 * @param samples_processed_out - Output: contrastive rows completed in current epoch (real training only; no ingestion substitute)
 * @param is_running_out - Output: whether training is still running
 * @param loop_count_out - Output: continuous mode loop count (0 if not in continuous mode)
 * @return 0 on success, negative error code on failure
 */
TRAINING_API int Training_GetProgress(
    uint64_t session_id,
    uint32_t* current_epoch_out,
    uint32_t* total_epochs_out,
    double* current_loss_out,
    uint64_t* samples_processed_out,
    bool* is_running_out,
    uint32_t* loop_count_out
);

/**
 * Stop training for a session
 * 
 * @param session_id - Session ID
 * @return 0 on success, negative error code on failure
 */
TRAINING_API int Training_StopTraining(uint64_t session_id);

/**
 * Cleanup and destroy a training session
 * 
 * @param session_id - Session ID
 * @return 0 on success, negative error code on failure
 */
TRAINING_API int Training_CleanupSession(uint64_t session_id);

/**
 * Export MoE GF(3) expert weights and pipeline config to a QMINI_V3 file.
 * Training must be stopped (same constraints as checkpointing).
 * @return 0 success, -1 session not found, -2 still running, -3 I/O or encode error, -4 bad path
 */
TRAINING_API int Training_ExportCheckpoint(uint64_t session_id, const char* path_utf8);

/**
 * Replace the in-memory pipeline from a QMINI_V3 file (training stopped).
 * @return 0 success, -1 session not found, -2 still running, -3 read or layout error, -4 bad path
 */
TRAINING_API int Training_ImportCheckpoint(uint64_t session_id, const char* path_utf8);

/**
 * Get DLL identity string (semantic tag plus compile date/time).
 * Use after LoadLibrary to confirm the loaded q_training.dll matches a rebuild
 * (compare the date/time suffix to the artifact you intended to ship).
 *
 * @param version_out - Output buffer for UTF-8 string (NUL-terminated)
 * @param max_len - Size of buffer including NUL
 */
TRAINING_API void Training_GetVersion(char* version_out, size_t max_len);

/**
 * Extended pipeline / DataSynthesizer metrics (includes queue depths).
 * @param samples_total_out Cumulative trained contrastive rows only (@c samples_processed_total), never ingestion substitutes.
 * @return 0 on success, negative error code on failure
 */
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
    /** Optional: native pipeline status (batch_inflight, queues). May be NULL / 0 to skip. */
    char* pipeline_status_utf8_out,
    size_t pipeline_status_utf8_cap
);

/**
 * Get ingestion/backpressure diagnostics from the current session metrics.
 *
 * @param samples_total_out Same semantics as training: cumulative contrastive rows trained (@c samples_processed_total), not DS volume.
 * @return 0 on success, negative error code on failure
 */
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
);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // TRAINING_API_H
