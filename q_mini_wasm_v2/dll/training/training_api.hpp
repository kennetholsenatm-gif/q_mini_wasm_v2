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
 * @return 0 on success, negative error code on failure
 */
TRAINING_API int Training_InitSession(
    uint64_t* session_id_out,
    uint32_t num_experts,
    uint32_t top_k,
    uint32_t num_layers,
    uint32_t batch_size,
    bool lazy_init,
    bool continuous_mode
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
 * @return 0 on success, negative error code on failure
 */
TRAINING_API int Training_GetProgress(
    uint64_t session_id,
    uint32_t* current_epoch_out,
    uint32_t* total_epochs_out,
    double* current_loss_out,
    uint64_t* samples_processed_out,
    bool* is_running_out
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
