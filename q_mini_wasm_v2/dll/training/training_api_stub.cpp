// Training DLL API - Returns error: real training requires linking core library
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
    bool continuous_mode
) {
    printf("[Training] ERROR: Real training not compiled in this DLL\n");
    printf("[Training] The q_training.dll does not include actual training code\n");
    printf("[Training] To enable training, rebuild with q_mini_wasm_v2_core linked\n");
    return -1;
}

TRAINING_API int Training_StartTraining(
    uint64_t session_id,
    uint32_t epochs,
    const char* data_path,
    bool enable_data_accumulation
) {
    printf("[Training] ERROR: Real training not implemented\n");
    return -1;
}

TRAINING_API int Training_GetProgress(
    uint64_t session_id,
    uint32_t* current_epoch_out,
    uint32_t* total_epochs_out,
    double* current_loss_out,
    uint64_t* samples_processed_out,
    bool* is_running_out
) {
    return -1;
}

TRAINING_API int Training_StopTraining(uint64_t session_id) {
    return -1;
}

TRAINING_API int Training_CleanupSession(uint64_t session_id) {
    return -1;
}

TRAINING_API void Training_GetVersion(char* version_out, size_t max_len) {
    strncpy(version_out, "q_training_stub_no_real_code", max_len - 1);
    version_out[max_len - 1] = '\0';
}

} // extern "C"
