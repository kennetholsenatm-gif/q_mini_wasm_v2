/* Pure C prototypes for Go cgo — must stay in lockstep with training_api.hpp (TRAINING_API exports). */
#ifndef TRAINING_API_CGO_H
#define TRAINING_API_CGO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

int Training_InitSession(
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
    uint32_t training_sycl_prereserve_chunk_mib,
    int32_t training_sycl_gpu_device_index,
    uint64_t training_gf3_sycl_min_weight_cells,
    uint32_t training_gf3_sycl_submit_grid_log,
    uint64_t training_gf3_ff_batched_weight_mib,
    uint32_t training_ff_multi_row_batch,
    uint32_t training_ff_multi_row_slots_chunk,
    uint32_t training_gf3_ff_layers_per_batch,
    uint64_t training_lazy_moe_resident_cap,
    const char* training_checkpoint_data_dir_utf8,
    uint64_t training_gf3_ff_multislot_slots_chunk_max,
    uint32_t training_gf3_ff_multislot_ignore_host_slot_budget,
    uint32_t training_auto_resume_from_checkpoint,
    uint32_t training_parallel_batches_runtime_cap);

int Training_StartTraining(uint64_t session_id, uint32_t epochs, const char* data_path, bool enable_data_accumulation);

int Training_GetProgress(uint64_t session_id,
                         uint32_t* current_epoch_out,
                         uint32_t* total_epochs_out,
                         double* current_loss_out,
                         uint64_t* samples_processed_out,
                         bool* is_running_out,
                         uint32_t* loop_count_out);

int Training_GetMetrics(uint64_t session_id,
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
                        size_t pipeline_status_utf8_cap);

int Training_StopTraining(uint64_t session_id);
int Training_CleanupSession(uint64_t session_id);
void Training_GetVersion(char* version_out, size_t max_len);

int Training_ProbeSyclDevice(int32_t gpu_device_index,
                             uint64_t* global_mem_bytes_out,
                             uint32_t* is_gpu_u32_out,
                             char* name_utf8_out,
                             size_t name_cap,
                             char* err_utf8_out,
                             size_t err_cap);

int Training_SetPendingLiveParallelStart(uint64_t session_id, uint32_t parallel_batches_start);
int Training_SetLiveParallelBatches(uint64_t session_id, uint32_t parallel_batches);
int Training_GetLiveParallelBatches(uint64_t session_id, uint32_t* live_out, uint32_t* ceiling_out);

#endif /* TRAINING_API_CGO_H */
