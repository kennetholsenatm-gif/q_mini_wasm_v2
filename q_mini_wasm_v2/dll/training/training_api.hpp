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
 * @return 0 on success, negative error code on failure
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
    uint32_t moe_expert_internal_layers
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
 * @param samples_processed_out - Output: number of samples processed
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
 * Get DLL version string
 * 
 * @param version_out - Output buffer for version string
 * @param max_len - Maximum length of output buffer
 */
TRAINING_API void Training_GetVersion(char* version_out, size_t max_len);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // TRAINING_API_H
