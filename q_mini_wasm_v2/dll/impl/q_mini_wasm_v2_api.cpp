#include "../q_mini_wasm_v2_api.hpp"
#include "../../core/ternary/trit.hpp"
#include "../../core/stabilizer/tableau.hpp"
#include "../../core/moe/router.hpp"
#include "../../core/learning/forward_forward.hpp"
#include "../../runtime/orchestrator.hpp"

#include <memory>
#include <unordered_map>
#include <mutex>
#include <iostream>

using namespace q_mini_wasm_v2;

// ============================================================================
// Error Messages
// ============================================================================

static const char* ERROR_MESSAGES[] = {
    "Success",                      // Q_MINI_WASM_V2_OK
    "Invalid or null handle",       // Q_MINI_WASM_V2_ERROR_INVALID_HANDLE
    "Invalid argument value",       // Q_MINI_WASM_V2_ERROR_INVALID_ARGUMENT
    "Index or value out of range",  // Q_MINI_WASM_V2_ERROR_OUT_OF_RANGE
    "Memory allocation failed",     // Q_MINI_WASM_V2_ERROR_ALLOCATION_FAILED
    "Internal error",               // Q_MINI_WASM_V2_ERROR_INTERNAL
    "Operation not supported"       // Q_MINI_WASM_V2_ERROR_NOT_SUPPORTED
};

static const size_t ERROR_MESSAGES_COUNT = sizeof(ERROR_MESSAGES) / sizeof(ERROR_MESSAGES[0]);

// ============================================================================
// Handle Management
// ============================================================================

static std::mutex g_handles_mutex;
static std::unordered_map<void*, std::shared_ptr<void>> g_handles;

template<typename T>
void* make_handle(std::shared_ptr<T> ptr) {
    std::lock_guard<std::mutex> lock(g_handles_mutex);
    void* raw = ptr.get();
    g_handles[raw] = std::static_pointer_cast<void>(ptr);
    return raw;
}

template<typename T>
std::shared_ptr<T> get_handle(void* handle) {
    std::lock_guard<std::mutex> lock(g_handles_mutex);
    auto it = g_handles.find(handle);
    if (it == g_handles.end()) {
        return nullptr;
    }
    return std::static_pointer_cast<T>(it->second);
}

void release_handle(void* handle) {
    std::lock_guard<std::mutex> lock(g_handles_mutex);
    g_handles.erase(handle);
}

// ============================================================================
// Utility Functions
// ============================================================================

Q_MINI_WASM_V2_API const char* q_mini_wasm_v2_error_string(int error_code) {
    // Convert negative error code to index
    int index = -error_code;
    if (index < 0 || static_cast<size_t>(index) >= ERROR_MESSAGES_COUNT) {
        return "Unknown error";
    }
    return ERROR_MESSAGES[index];
}

Q_MINI_WASM_V2_API int q_mini_wasm_v2_is_valid_handle(void* handle) {
    if (handle == nullptr) {
        return 0;
    }
    std::lock_guard<std::mutex> lock(g_handles_mutex);
    return g_handles.find(handle) != g_handles.end() ? 1 : 0;
}

static const char* VERSION = "1.0.0";
static const char* BUILD_INFO = "q_mini_wasm_v2 - Quantum-Classical Hybrid Framework";

Q_MINI_WASM_V2_API const char* q_mini_wasm_v2_version() {
    return VERSION;
}

Q_MINI_WASM_V2_API const char* q_mini_wasm_v2_build_info() {
    return BUILD_INFO;
}

// ============================================================================
// Trit Operations
// ============================================================================

Q_MINI_WASM_V2_API int8_t trit_add(int8_t a, int8_t b) {
    auto ta = static_cast<core::ternary::Trit>(a);
    auto tb = static_cast<core::ternary::Trit>(b);
    return static_cast<int8_t>(core::ternary::trit_ops::add(ta, tb));
}

Q_MINI_WASM_V2_API int8_t trit_multiply(int8_t a, int8_t b) {
    auto ta = static_cast<core::ternary::Trit>(a);
    auto tb = static_cast<core::ternary::Trit>(b);
    return static_cast<int8_t>(core::ternary::trit_ops::multiply(ta, tb));
}

Q_MINI_WASM_V2_API uint8_t trit_pack_5(const int8_t trits[5]) {
    core::ternary::TritBlock5 block;
    for (int i = 0; i < 5; ++i) {
        block.trits[i] = static_cast<core::ternary::Trit>(trits[i]);
    }
    return block.pack();
}

Q_MINI_WASM_V2_API void trit_unpack_5(uint8_t byte, int8_t trits[5]) {
    auto block = core::ternary::TritBlock5::unpack(byte);
    for (int i = 0; i < 5; ++i) {
        trits[i] = static_cast<int8_t>(block.trits[i]);
    }
}

// ============================================================================
// Stabilizer Tableau Operations
// ============================================================================

Q_MINI_WASM_V2_API void* tableau_create(size_t num_qutrits) {
    if (num_qutrits == 0) {
        return nullptr;
    }
    try {
        auto tableau = core::stabilizer::create_tableau(num_qutrits);
        return make_handle(std::move(tableau));
    } catch (...) {
        return nullptr;
    }
}

Q_MINI_WASM_V2_API void tableau_destroy(void* handle) {
    if (handle != nullptr) {
        release_handle(handle);
    }
}

Q_MINI_WASM_V2_API int tableau_apply_hadamard(void* handle, size_t qutrit) {
    auto tableau = get_handle<core::stabilizer::StabilizerTableau>(handle);
    if (!tableau) return Q_MINI_WASM_V2_ERROR_INVALID_HANDLE;
    
    try {
        tableau->apply_hadamard(qutrit);
        return Q_MINI_WASM_V2_OK;
    } catch (const std::out_of_range&) {
        return Q_MINI_WASM_V2_ERROR_OUT_OF_RANGE;
    } catch (...) {
        return Q_MINI_WASM_V2_ERROR_INTERNAL;
    }
}

Q_MINI_WASM_V2_API int tableau_apply_phase(void* handle, size_t qutrit) {
    auto tableau = get_handle<core::stabilizer::StabilizerTableau>(handle);
    if (!tableau) return Q_MINI_WASM_V2_ERROR_INVALID_HANDLE;
    
    try {
        tableau->apply_phase(qutrit);
        return Q_MINI_WASM_V2_OK;
    } catch (const std::out_of_range&) {
        return Q_MINI_WASM_V2_ERROR_OUT_OF_RANGE;
    } catch (...) {
        return Q_MINI_WASM_V2_ERROR_INTERNAL;
    }
}

Q_MINI_WASM_V2_API int tableau_apply_csum(void* handle, size_t control, size_t target) {
    auto tableau = get_handle<core::stabilizer::StabilizerTableau>(handle);
    if (!tableau) return Q_MINI_WASM_V2_ERROR_INVALID_HANDLE;
    
    try {
        tableau->apply_csum(control, target);
        return Q_MINI_WASM_V2_OK;
    } catch (const std::out_of_range&) {
        return Q_MINI_WASM_V2_ERROR_OUT_OF_RANGE;
    } catch (...) {
        return Q_MINI_WASM_V2_ERROR_INTERNAL;
    }
}

Q_MINI_WASM_V2_API size_t tableau_measure_all(void* handle, int8_t* outcomes, size_t max_outcomes) {
    auto tableau = get_handle<core::stabilizer::StabilizerTableau>(handle);
    if (!tableau || outcomes == nullptr || max_outcomes == 0) return 0;
    
    try {
        auto measurements = tableau->measure_all();
        size_t count = std::min(measurements.size(), max_outcomes);
        for (size_t i = 0; i < count; ++i) {
            outcomes[i] = measurements[i];
        }
        return count;
    } catch (...) {
        return 0;
    }
}

Q_MINI_WASM_V2_API int tableau_is_valid(void* handle) {
    auto tableau = get_handle<core::stabilizer::StabilizerTableau>(handle);
    if (!tableau) return 0;
    return tableau->is_valid() ? 1 : 0;
}

Q_MINI_WASM_V2_API size_t tableau_num_qutrits(void* handle) {
    auto tableau = get_handle<core::stabilizer::StabilizerTableau>(handle);
    if (!tableau) return 0;
    return tableau->num_qutrits();
}

// ============================================================================
// MoE Router Operations
// ============================================================================

Q_MINI_WASM_V2_API void* moe_router_create(size_t total_experts, size_t active_experts, size_t routing_qutrits) {
    if (total_experts == 0 || active_experts == 0 || routing_qutrits == 0) {
        return nullptr;
    }
    if (active_experts > total_experts) {
        return nullptr;
    }
    try {
        core::moe::ExpertConfig config{total_experts, active_experts, routing_qutrits};
        auto router = core::moe::create_moe_router(config);
        return make_handle(std::move(router));
    } catch (...) {
        return nullptr;
    }
}

Q_MINI_WASM_V2_API void moe_router_destroy(void* handle) {
    if (handle != nullptr) {
        release_handle(handle);
    }
}

Q_MINI_WASM_V2_API size_t moe_router_route_topk(
    void* handle,
    const int8_t* input,
    size_t input_size,
    size_t* selected,
    size_t max_selected
) {
    auto router = get_handle<core::moe::MoERouter>(handle);
    if (!router || input == nullptr || selected == nullptr || input_size == 0 || max_selected == 0) return 0;
    
    try {
        std::vector<core::ternary::Trit> input_vec(input_size);
        for (size_t i = 0; i < input_size; ++i) {
            input_vec[i] = static_cast<core::ternary::Trit>(input[i]);
        }
        
        auto selected_experts = router->route_topk(input_vec);
        size_t count = std::min(selected_experts.size(), max_selected);
        for (size_t i = 0; i < count; ++i) {
            selected[i] = selected_experts[i];
        }
        return count;
    } catch (...) {
        return 0;
    }
}

Q_MINI_WASM_V2_API size_t moe_router_capacity(void* handle) {
    auto router = get_handle<core::moe::MoERouter>(handle);
    if (!router) return 0;
    return router->compute_hypersimplex_capacity();
}

// ============================================================================
// Forward-Forward Learner Operations
// ============================================================================

Q_MINI_WASM_V2_API void* ff_learner_create(size_t num_layers, size_t neurons_per_layer, double learning_rate) {
    if (num_layers == 0 || neurons_per_layer == 0 || learning_rate <= 0.0) {
        return nullptr;
    }
    try {
        core::learning::FFConfig config{
            num_layers,
            neurons_per_layer,
            learning_rate,
            1.0,   // positive_threshold
            -1.0   // negative_threshold
        };
        auto learner = core::learning::create_ff_learner(config);
        return make_handle(std::move(learner));
    } catch (...) {
        return nullptr;
    }
}

Q_MINI_WASM_V2_API void ff_learner_destroy(void* handle) {
    if (handle != nullptr) {
        release_handle(handle);
    }
}

Q_MINI_WASM_V2_API size_t ff_learner_forward(
    void* handle,
    const int8_t* input,
    size_t input_size,
    int8_t* output,
    size_t output_size
) {
    auto learner = get_handle<core::learning::ForwardForwardLearner>(handle);
    if (!learner || input == nullptr || output == nullptr || input_size == 0 || output_size == 0) return 0;
    
    try {
        std::vector<core::ternary::Trit> input_vec(input_size);
        for (size_t i = 0; i < input_size; ++i) {
            input_vec[i] = static_cast<core::ternary::Trit>(input[i]);
        }
        
        auto result = learner->forward(input_vec);
        size_t count = std::min(result.size(), output_size);
        for (size_t i = 0; i < count; ++i) {
            output[i] = static_cast<int8_t>(result[i]);
        }
        return count;
    } catch (...) {
        return 0;
    }
}

Q_MINI_WASM_V2_API double ff_learner_goodness(
    void* handle,
    const int8_t* activations,
    size_t size
) {
    auto learner = get_handle<core::learning::ForwardForwardLearner>(handle);
    if (!learner || activations == nullptr || size == 0) return 0.0;
    
    try {
        std::vector<core::ternary::Trit> activations_vec(size);
        for (size_t i = 0; i < size; ++i) {
            activations_vec[i] = static_cast<core::ternary::Trit>(activations[i]);
        }
        
        return learner->compute_goodness(activations_vec);
    } catch (...) {
        return 0.0;
    }
}

// ============================================================================
// Runtime Orchestrator Operations
// ============================================================================

Q_MINI_WASM_V2_API void* orchestrator_create(size_t num_threads) {
    try {
        runtime::RuntimeConfig config{num_threads, 100, true, false};
        auto orchestrator = runtime::create_orchestrator(config);
        return make_handle(std::move(orchestrator));
    } catch (...) {
        return nullptr;
    }
}

Q_MINI_WASM_V2_API void orchestrator_destroy(void* handle) {
    if (handle != nullptr) {
        release_handle(handle);
    }
}

Q_MINI_WASM_V2_API void orchestrator_wait_all(void* handle) {
    auto orchestrator = get_handle<runtime::RuntimeOrchestrator>(handle);
    if (orchestrator) {
        orchestrator->wait_all();
    }
}

Q_MINI_WASM_V2_API int orchestrator_has_pending(void* handle) {
    auto orchestrator = get_handle<runtime::RuntimeOrchestrator>(handle);
    if (!orchestrator) return 0;
    return orchestrator->has_pending_tasks() ? 1 : 0;
}
