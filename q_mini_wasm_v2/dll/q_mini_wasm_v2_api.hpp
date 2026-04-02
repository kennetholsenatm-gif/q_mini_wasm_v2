#pragma once

/**
 * @file q_mini_wasm_v2_api.hpp
 * @brief DLL API for q_mini_wasm_v2 C++ engine
 * 
 * Provides C-compatible interface for Go/Python/other language integration
 */

#include <cstdint>
#include <cstddef>

#ifdef _WIN32
    #ifdef Q_MINI_WASM_V2_EXPORTS
        #define Q_MINI_WASM_V2_API __declspec(dllexport)
    #else
        #define Q_MINI_WASM_V2_API __declspec(dllimport)
    #endif
#else
    #define Q_MINI_WASM_V2_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Trit Operations
// ============================================================================

/**
 * @brief Add two trits over GF(3)
 * @return Result trit value (-1, 0, or 1)
 */
Q_MINI_WASM_V2_API int8_t trit_add(int8_t a, int8_t b);

/**
 * @brief Multiply two trits over GF(3)
 * @return Result trit value (-1, 0, or 1)
 */
Q_MINI_WASM_V2_API int8_t trit_multiply(int8_t a, int8_t b);

/**
 * @brief Pack 5 trits into single byte
 * @param trits Array of 5 trit values
 * @return Packed byte
 */
Q_MINI_WASM_V2_API uint8_t trit_pack_5(const int8_t trits[5]);

/**
 * @brief Unpack byte into 5 trits
 * @param byte Packed byte
 * @param trits Output array of 5 trit values
 */
Q_MINI_WASM_V2_API void trit_unpack_5(uint8_t byte, int8_t trits[5]);

// ============================================================================
// Stabilizer Tableau Operations
// ============================================================================

/**
 * @brief Create a new stabilizer tableau
 * @param num_qutrits Number of qutrits
 * @return Opaque handle to tableau
 */
Q_MINI_WASM_V2_API void* tableau_create(size_t num_qutrits);

/**
 * @brief Destroy a stabilizer tableau
 * @param handle Tableau handle
 */
Q_MINI_WASM_V2_API void tableau_destroy(void* handle);

/**
 * @brief Apply Hadamard gate
 * @param handle Tableau handle
 * @param qutrit Target qutrit index
 * @return 0 on success, error code otherwise
 */
Q_MINI_WASM_V2_API int tableau_apply_hadamard(void* handle, size_t qutrit);

/**
 * @brief Apply Phase gate
 * @param handle Tableau handle
 * @param qutrit Target qutrit index
 * @return 0 on success, error code otherwise
 */
Q_MINI_WASM_V2_API int tableau_apply_phase(void* handle, size_t qutrit);

/**
 * @brief Apply Controlled-SUM gate
 * @param handle Tableau handle
 * @param control Control qutrit index
 * @param target Target qutrit index
 * @return 0 on success, error code otherwise
 */
Q_MINI_WASM_V2_API int tableau_apply_csum(void* handle, size_t control, size_t target);

/**
 * @brief Measure all qutrits
 * @param handle Tableau handle
 * @param outcomes Output array for measurement outcomes
 * @param max_outcomes Maximum number of outcomes to store
 * @return Number of outcomes written
 */
Q_MINI_WASM_V2_API size_t tableau_measure_all(void* handle, int8_t* outcomes, size_t max_outcomes);

/**
 * @brief Check if tableau is valid
 * @param handle Tableau handle
 * @return 1 if valid, 0 otherwise
 */
Q_MINI_WASM_V2_API int tableau_is_valid(void* handle);

/**
 * @brief Get number of qutrits in tableau
 * @param handle Tableau handle
 * @return Number of qutrits
 */
Q_MINI_WASM_V2_API size_t tableau_num_qutrits(void* handle);

// ============================================================================
// MoE Router Operations
// ============================================================================

/**
 * @brief Create MoE router
 * @param total_experts Total number of experts
 * @param active_experts Number of experts to activate (Top-K)
 * @param routing_qutrits Number of qutrits for routing
 * @return Opaque handle to router
 */
Q_MINI_WASM_V2_API void* moe_router_create(size_t total_experts, size_t active_experts, size_t routing_qutrits);

/**
 * @brief Destroy MoE router
 * @param handle Router handle
 */
Q_MINI_WASM_V2_API void moe_router_destroy(void* handle);

/**
 * @brief Route input to Top-K experts
 * @param handle Router handle
 * @param input Input features (ternary values)
 * @param input_size Size of input array
 * @param selected Output array for selected expert indices
 * @param max_selected Maximum selections to store
 * @return Number of experts selected
 */
Q_MINI_WASM_V2_API size_t moe_router_route_topk(
    void* handle,
    const int8_t* input,
    size_t input_size,
    size_t* selected,
    size_t max_selected
);

/**
 * @brief Compute hypersimplex capacity
 * @param handle Router handle
 * @return C(total_experts, active_experts)
 */
Q_MINI_WASM_V2_API size_t moe_router_capacity(void* handle);

// ============================================================================
// Forward-Forward Learner Operations
// ============================================================================

/**
 * @brief Create Forward-Forward learner
 * @param num_layers Number of layers
 * @param neurons_per_layer Neurons per layer
 * @param learning_rate Learning rate
 * @return Opaque handle to learner
 */
Q_MINI_WASM_V2_API void* ff_learner_create(size_t num_layers, size_t neurons_per_layer, double learning_rate);

/**
 * @brief Destroy Forward-Forward learner
 * @param handle Learner handle
 */
Q_MINI_WASM_V2_API void ff_learner_destroy(void* handle);

/**
 * @brief Forward pass through learner
 * @param handle Learner handle
 * @param input Input features
 * @param input_size Size of input
 * @param output Output buffer
 * @param output_size Size of output buffer
 * @return Number of outputs written
 */
Q_MINI_WASM_V2_API size_t ff_learner_forward(
    void* handle,
    const int8_t* input,
    size_t input_size,
    int8_t* output,
    size_t output_size
);

/**
 * @brief Compute goodness metric
 * @param handle Learner handle
 * @param activations Layer activations
 * @param size Size of activations array
 * @return Goodness value
 */
Q_MINI_WASM_V2_API double ff_learner_goodness(
    void* handle,
    const int8_t* activations,
    size_t size
);

// ============================================================================
// Runtime Orchestrator Operations
// ============================================================================

/**
 * @brief Create runtime orchestrator
 * @param num_threads Number of worker threads
 * @return Opaque handle to orchestrator
 */
Q_MINI_WASM_V2_API void* orchestrator_create(size_t num_threads);

/**
 * @brief Destroy runtime orchestrator
 * @param handle Orchestrator handle
 */
Q_MINI_WASM_V2_API void orchestrator_destroy(void* handle);

/**
 * @brief Wait for all pending tasks
 * @param handle Orchestrator handle
 */
Q_MINI_WASM_V2_API void orchestrator_wait_all(void* handle);

/**
 * @brief Check if tasks are pending
 * @param handle Orchestrator handle
 * @return 1 if tasks pending, 0 otherwise
 */
Q_MINI_WASM_V2_API int orchestrator_has_pending(void* handle);

// ============================================================================
// Version Information
// ============================================================================

/**
 * @brief Get library version
 * @return Version string
 */
Q_MINI_WASM_V2_API const char* q_mini_wasm_v2_version();

/**
 * @brief Get library build info
 * @return Build info string
 */
Q_MINI_WASM_V2_API const char* q_mini_wasm_v2_build_info();

#ifdef __cplusplus
}
#endif