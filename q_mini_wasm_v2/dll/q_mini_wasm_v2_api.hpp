#pragma once

/**
 * @file q_mini_wasm_v2_api.hpp
 * @brief DLL API for q_mini_wasm_v2 C++ engine
 * 
 * Provides C-compatible interface for Go/Python/other language integration.
 * 
 * Thread Safety: All functions are thread-safe unless otherwise noted.
 * Memory Management: All create functions return opaque handles that must be
 * destroyed using the corresponding destroy function.
 * 
 * Error Handling: Functions return 0 on success, negative error codes on failure.
 * Use q_mini_wasm_v2_error_string() to get human-readable error messages.
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

// ============================================================================
// Version Macros
// ============================================================================

#define Q_MINI_WASM_V2_VERSION_MAJOR 1
#define Q_MINI_WASM_V2_VERSION_MINOR 0
#define Q_MINI_WASM_V2_VERSION_PATCH 0

#define Q_MINI_WASM_V2_VERSION_STRING "1.0.0"

// ============================================================================
// Error Codes
// ============================================================================

/**
 * @brief Error codes returned by API functions
 */
typedef enum {
    Q_MINI_WASM_V2_OK = 0,                    /**< Success */
    Q_MINI_WASM_V2_ERROR_INVALID_HANDLE = -1,  /**< Invalid or null handle */
    Q_MINI_WASM_V2_ERROR_INVALID_ARGUMENT = -2,/**< Invalid argument value */
    Q_MINI_WASM_V2_ERROR_OUT_OF_RANGE = -3,    /**< Index or value out of range */
    Q_MINI_WASM_V2_ERROR_ALLOCATION_FAILED = -4,/**< Memory allocation failed */
    Q_MINI_WASM_V2_ERROR_INTERNAL = -5,        /**< Internal error */
    Q_MINI_WASM_V2_ERROR_NOT_SUPPORTED = -6    /**< Operation not supported */
} q_mini_wasm_v2_error_t;

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Utility Functions
// ============================================================================

/**
 * @brief Get human-readable error string for error code
 * @param error_code Error code from API function
 * @return Error message string (never null)
 */
Q_MINI_WASM_V2_API const char* q_mini_wasm_v2_error_string(int error_code);

/**
 * @brief Validate a handle pointer
 * @param handle Handle to validate
 * @return 1 if valid, 0 if null or invalid
 */
Q_MINI_WASM_V2_API int q_mini_wasm_v2_is_valid_handle(void* handle);

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

// ============================================================================
// Trit Operations
// ============================================================================

/**
 * @brief Add two trits over GF(3)
 * @param a First trit value (-1, 0, or 1)
 * @param b Second trit value (-1, 0, or 1)
 * @return Result trit value (-1, 0, or 1)
 */
Q_MINI_WASM_V2_API int8_t trit_add(int8_t a, int8_t b);

/**
 * @brief Multiply two trits over GF(3)
 * @param a First trit value (-1, 0, or 1)
 * @param b Second trit value (-1, 0, or 1)
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
 * @param num_qutrits Number of qutrits (must be > 0)
 * @return Opaque handle to tableau, or NULL on failure
 */
Q_MINI_WASM_V2_API void* tableau_create(size_t num_qutrits);

/**
 * @brief Destroy a stabilizer tableau
 * @param handle Tableau handle (can be NULL)
 */
Q_MINI_WASM_V2_API void tableau_destroy(void* handle);

/**
 * @brief Apply Hadamard gate to qutrit
 * @param handle Tableau handle
 * @param qutrit Target qutrit index (0-based)
 * @return Q_MINI_WASM_V2_OK on success, error code otherwise
 */
Q_MINI_WASM_V2_API int tableau_apply_hadamard(void* handle, size_t qutrit);

/**
 * @brief Apply Phase gate to qutrit
 * @param handle Tableau handle
 * @param qutrit Target qutrit index (0-based)
 * @return Q_MINI_WASM_V2_OK on success, error code otherwise
 */
Q_MINI_WASM_V2_API int tableau_apply_phase(void* handle, size_t qutrit);

/**
 * @brief Apply Controlled-SUM gate between two qutrits
 * @param handle Tableau handle
 * @param control Control qutrit index (0-based)
 * @param target Target qutrit index (0-based)
 * @return Q_MINI_WASM_V2_OK on success, error code otherwise
 */
Q_MINI_WASM_V2_API int tableau_apply_csum(void* handle, size_t control, size_t target);

/**
 * @brief Measure all qutrits in tableau
 * @param handle Tableau handle
 * @param outcomes Output array for measurement outcomes (-1, 0, or 1)
 * @param max_outcomes Maximum number of outcomes to store
 * @return Number of outcomes written, or 0 on error
 */
Q_MINI_WASM_V2_API size_t tableau_measure_all(void* handle, int8_t* outcomes, size_t max_outcomes);

/**
 * @brief Check if tableau represents valid stabilizer state
 * @param handle Tableau handle
 * @return 1 if valid, 0 if invalid or handle is NULL
 */
Q_MINI_WASM_V2_API int tableau_is_valid(void* handle);

/**
 * @brief Get number of qutrits in tableau
 * @param handle Tableau handle
 * @return Number of qutrits, or 0 if handle is NULL
 */
Q_MINI_WASM_V2_API size_t tableau_num_qutrits(void* handle);

// ============================================================================
// MoE Router Operations
// ============================================================================

/**
 * @brief Create MoE router
 * @param total_experts Total number of experts (must be > 0)
 * @param active_experts Number of experts to activate (Top-K) (must be > 0 and <= total_experts)
 * @param routing_qutrits Number of qutrits for routing (must be > 0)
 * @return Opaque handle to router, or NULL on failure
 */
Q_MINI_WASM_V2_API void* moe_router_create(size_t total_experts, size_t active_experts, size_t routing_qutrits);

/**
 * @brief Destroy MoE router
 * @param handle Router handle (can be NULL)
 */
Q_MINI_WASM_V2_API void moe_router_destroy(void* handle);

/**
 * @brief Route input to Top-K experts
 * @param handle Router handle
 * @param input Input features (ternary values: -1, 0, or 1)
 * @param input_size Size of input array
 * @param selected Output array for selected expert indices
 * @param max_selected Maximum selections to store
 * @return Number of experts selected, or 0 on error
 */
Q_MINI_WASM_V2_API size_t moe_router_route_topk(
    void* handle,
    const int8_t* input,
    size_t input_size,
    size_t* selected,
    size_t max_selected
);

/**
 * @brief Compute hypersimplex capacity C(total_experts, active_experts)
 * @param handle Router handle
 * @return Capacity value, or 0 if handle is NULL
 */
Q_MINI_WASM_V2_API size_t moe_router_capacity(void* handle);

// ============================================================================
// Forward-Forward Learner Operations
// ============================================================================

/**
 * @brief Create Forward-Forward learner
 * @param num_layers Number of layers (must be > 0)
 * @param neurons_per_layer Neurons per layer (must be > 0)
 * @param learning_rate Learning rate (must be > 0.0)
 * @return Opaque handle to learner, or NULL on failure
 */
Q_MINI_WASM_V2_API void* ff_learner_create(size_t num_layers, size_t neurons_per_layer, double learning_rate);

/**
 * @brief Destroy Forward-Forward learner
 * @param handle Learner handle (can be NULL)
 */
Q_MINI_WASM_V2_API void ff_learner_destroy(void* handle);

/**
 * @brief Forward pass through learner
 * @param handle Learner handle
 * @param input Input features (ternary values: -1, 0, or 1)
 * @param input_size Size of input array
 * @param output Output buffer for activations
 * @param output_size Size of output buffer
 * @return Number of outputs written, or 0 on error
 */
Q_MINI_WASM_V2_API size_t ff_learner_forward(
    void* handle,
    const int8_t* input,
    size_t input_size,
    int8_t* output,
    size_t output_size
);

/**
 * @brief Compute goodness metric for activations
 * @param handle Learner handle
 * @param activations Layer activations (ternary values: -1, 0, or 1)
 * @param size Size of activations array
 * @return Goodness value (>= 0.0), or 0.0 on error
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
 * @param num_threads Number of worker threads (0 for auto-detect)
 * @return Opaque handle to orchestrator, or NULL on failure
 */
Q_MINI_WASM_V2_API void* orchestrator_create(size_t num_threads);

/**
 * @brief Destroy runtime orchestrator
 * @param handle Orchestrator handle (can be NULL)
 */
Q_MINI_WASM_V2_API void orchestrator_destroy(void* handle);

/**
 * @brief Wait for all pending tasks to complete
 * @param handle Orchestrator handle
 */
Q_MINI_WASM_V2_API void orchestrator_wait_all(void* handle);

/**
 * @brief Check if tasks are pending
 * @param handle Orchestrator handle
 * @return 1 if tasks pending, 0 otherwise or if handle is NULL
 */
Q_MINI_WASM_V2_API int orchestrator_has_pending(void* handle);

#ifdef __cplusplus
}
#endif
