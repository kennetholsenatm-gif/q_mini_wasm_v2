#include "../q_mini_wasm_v2_api.hpp"
#include "../../core/moe/router.hpp"
#include <unordered_map>
#include <mutex>
#include <cstring>
#define _USE_MATH_DEFINES
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace q_mini_wasm_v2::core::moe;

extern "C" {

Q_MINI_WASM_V2_API size_t moe_router_llep_route(
    void* handle,
    const double* logits,
    const size_t* expert_loads,
    size_t num_experts,
    size_t k,
    size_t* selected,
    size_t max_selected
) {
    if (!handle || !logits || !expert_loads || !selected || k == 0 || max_selected == 0) {
        return 0;
    }
    
    MoERouter* router = static_cast<MoERouter*>(handle);
    
    std::vector<int32_t> logit_vec(num_experts);
    for (size_t i = 0; i < num_experts; ++i) {
        logit_vec[i] = static_cast<int32_t>(logits[i]);
    }
    std::vector<size_t> load_vec(expert_loads, expert_loads + num_experts);
    
    std::vector<size_t> result = router->llep_route(logit_vec, load_vec, std::min(k, max_selected));
    
    for (size_t i = 0; i < result.size() && i < max_selected; ++i) {
        selected[i] = result[i];
    }
    
    return result.size();
}

Q_MINI_WASM_V2_API double quantum_entanglement_score(uint64_t content_hash, double base_score) {
    // Quantum Entanglement Entropy Scoring over GF(3) qutrit space
    uint64_t signature_hash = content_hash % 2187; // 3^7 = 2187 states
    
    // Map to ternary stabilizer phase space
    int t0 = signature_hash / 729;
    uint64_t remainder = signature_hash % 729;
    int t1 = remainder / 243;
    remainder %= 243;
    int t2 = remainder / 81;
    remainder %= 81;
    int t3 = remainder / 27;
    remainder %= 27;
    int t4 = remainder / 9;
    remainder %= 9;
    int t5 = remainder / 3;
    int t6 = remainder % 3;
    
    // Symplectic inner product calculation
    int symplectic_product = (t0 * t3 + t1 * t4 + t2 * t5) % 3;
    
    double entropy;
    if (symplectic_product == 0) {
        // Self-orthogonal state (maximally entangled)
        entropy = 1.0;
    } else {
        // Calculate purity
        double purity = pow(cos((symplectic_product * M_PI) / 3.0), 2);
        entropy = -purity * log2(purity) - (1-purity) * log2(1-purity);
        if (purity <= 0 || purity >= 1) entropy = 0.0;
    }
    
    // 60% quantum / 40% classical weighted scoring
    return (base_score * 0.4) + (entropy * 0.6);
}

// ============================================================================
// 1.58-bit Ternary Weight Packing / Unpacking
// ============================================================================

Q_MINI_WASM_V2_API uint8_t ternary_pack_5(const int8_t trits[5]) {
    // Pack 5 ternary values (-1, 0, 1) into single 8-bit byte
    // 3^5 = 243 possible states < 256 byte capacity
    uint8_t result = 0;
    int multiplier = 1;
    
    for (int i = 0; i < 5; ++i) {
        // Normalize to 0..2 range
        int normalized = trits[i] + 1;
        result += normalized * multiplier;
        multiplier *= 3;
    }
    
    return result;
}

Q_MINI_WASM_V2_API void ternary_unpack_5(uint8_t packed, int8_t trits[5]) {
    // Unpack single byte back into 5 ternary values
    int remainder = packed;
    
    for (int i = 0; i < 5; ++i) {
        int normalized = remainder % 3;
        trits[i] = static_cast<int8_t>(normalized - 1);
        remainder /= 3;
    }
}

Q_MINI_WASM_V2_API void ternary_gemm_execute(
    const int8_t* activations,
    const uint8_t* packed_weights,
    int32_t* output,
    size_t dimension
) {
    // 1.58-bit ternary matrix multiplication: replaces floating point MAC
    // with pure integer addition/subtraction operations only
    int8_t weight_trits[5];
    
    for (size_t i = 0; i < dimension; ++i) {
        ternary_unpack_5(packed_weights[i / 5], weight_trits);
        int8_t w = weight_trits[i % 5];
        
        if (w == 0) {
            // Zero weight: no contribution
            continue;
        } else if (w == 1) {
            output[i] += activations[i];
        } else { // w == -1
            output[i] -= activations[i];
        }
    }
}

}
#include "../../core/ternary/trit.hpp"
#include "../../core/qutrit_stabilizer.h"
#include "../../core/stabilizer/tableau.hpp"
#include "../../core/learning/forward_forward.hpp"
#include "../../runtime/orchestrator.hpp"

#include <memory>
#include <unordered_map>
#include <mutex>
#include <iostream>
#include <fstream>
#include <chrono>
#include <cstdarg>

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

// ============================================================================
// Logging Support
// ============================================================================

static std::mutex g_logging_mutex;
static std::ofstream g_log_file;
static bool g_logging_enabled = false;
static int g_log_level = 0;  // 0=error, 1=warning, 2=info, 3=debug

static void init_logging() {
    if (!g_logging_enabled) {
        g_log_file.open("q_mini_wasm_v2.log", std::ios::app);
        g_logging_enabled = g_log_file.is_open();
    }
}

static void log_message(int level, const char* format, ...) {
    if (!g_logging_enabled || level > g_log_level) return;
    
    std::lock_guard<std::mutex> lock(g_logging_mutex);
    
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    
    const char* level_str = level == 0 ? "ERROR" : level == 1 ? "WARN" : level == 2 ? "INFO" : "DEBUG";
    
    if (g_log_file.is_open()) {
        char time_buf[26];
        ctime_s(time_buf, sizeof(time_buf), &time);
        // Remove newline from ctime_s
        char* newline = strchr(time_buf, '\n');
        if (newline) *newline = '\0';
        
        g_log_file << "[" << level_str << "] " << time_buf << " - ";
        
        va_list args;
        va_start(args, format);
        char buffer[1024];
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        
        g_log_file << buffer << std::endl;
        g_log_file.flush();
    }
}

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
// Logging Functions
// ============================================================================

Q_MINI_WASM_V2_API void q_mini_wasm_v2_enable_logging(int log_level) {
    g_log_level = log_level;
    init_logging();
    log_message(2, "Logging enabled at level %d", log_level);
}

Q_MINI_WASM_V2_API void q_mini_wasm_v2_disable_logging() {
    log_message(2, "Logging disabled");
    std::lock_guard<std::mutex> lock(g_logging_mutex);
    if (g_log_file.is_open()) {
        g_log_file.close();
    }
    g_logging_enabled = false;
}

Q_MINI_WASM_V2_API void q_mini_wasm_v2_log(int log_level, const char* message) {
    log_message(log_level, "%s", message);
}

// ============================================================================
// Handle Enumeration
// ============================================================================

Q_MINI_WASM_V2_API size_t q_mini_wasm_v2_get_handle_count() {
    std::lock_guard<std::mutex> lock(g_handles_mutex);
    return g_handles.size();
}

Q_MINI_WASM_V2_API size_t q_mini_wasm_v2_enumerate_handles(void** handles, size_t max_handles) {
    std::lock_guard<std::mutex> lock(g_handles_mutex);
    size_t count = 0;
    for (const auto& pair : g_handles) {
        if (count >= max_handles) break;
        handles[count++] = pair.first;
    }
    return count;
}

Q_MINI_WASM_V2_API const char* q_mini_wasm_v2_get_handle_type(void* handle) {
    if (handle == nullptr) return "null";
    std::lock_guard<std::mutex> lock(g_handles_mutex);
    auto it = g_handles.find(handle);
    if (it == g_handles.end()) return "invalid";
    // Type information is not stored, return "unknown"
    return "unknown";
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
        return make_handle(std::shared_ptr<core::stabilizer::StabilizerTableau>(tableau.release()));
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
// Qutrit Stabilizer Tableau Operations
// ============================================================================

Q_MINI_WASM_V2_API void* qutrit_stabilizer_create(size_t num_qutrits) {
    if (num_qutrits == 0) {
        return nullptr;
    }
    try {
        StabilizerTableau* t = stabilizer_init(static_cast<uint32_t>(num_qutrits));
        return make_handle(std::shared_ptr<StabilizerTableau>(t, stabilizer_free));
    } catch (...) {
        return nullptr;
    }
}

Q_MINI_WASM_V2_API void qutrit_stabilizer_destroy(void* handle) {
    if (handle != nullptr) {
        release_handle(handle);
    }
}

Q_MINI_WASM_V2_API int qutrit_stabilizer_apply_hadamard(void* handle, size_t target) {
    auto tableau = get_handle<StabilizerTableau>(handle);
    if (!tableau) return Q_MINI_WASM_V2_ERROR_INVALID_HANDLE;
    if (target >= tableau->n) return Q_MINI_WASM_V2_ERROR_OUT_OF_RANGE;
    
    try {
        stabilizer_apply_hadamard(tableau.get(), static_cast<uint32_t>(target));
        return Q_MINI_WASM_V2_OK;
    } catch (...) {
        return Q_MINI_WASM_V2_ERROR_INTERNAL;
    }
}

Q_MINI_WASM_V2_API int qutrit_stabilizer_apply_phase(void* handle, size_t target) {
    auto tableau = get_handle<StabilizerTableau>(handle);
    if (!tableau) return Q_MINI_WASM_V2_ERROR_INVALID_HANDLE;
    if (target >= tableau->n) return Q_MINI_WASM_V2_ERROR_OUT_OF_RANGE;
    
    try {
        stabilizer_apply_phase(tableau.get(), static_cast<uint32_t>(target));
        return Q_MINI_WASM_V2_OK;
    } catch (...) {
        return Q_MINI_WASM_V2_ERROR_INTERNAL;
    }
}

Q_MINI_WASM_V2_API int qutrit_stabilizer_apply_csum(void* handle, size_t control, size_t target) {
    auto tableau = get_handle<StabilizerTableau>(handle);
    if (!tableau) return Q_MINI_WASM_V2_ERROR_INVALID_HANDLE;
    if (control >= tableau->n || target >= tableau->n) return Q_MINI_WASM_V2_ERROR_OUT_OF_RANGE;
    
    try {
        stabilizer_apply_csum(tableau.get(), static_cast<uint32_t>(control), static_cast<uint32_t>(target));
        return Q_MINI_WASM_V2_OK;
    } catch (...) {
        return Q_MINI_WASM_V2_ERROR_INTERNAL;
    }
}

Q_MINI_WASM_V2_API int qutrit_stabilizer_phase_penalty(void* handle, size_t target, uint8_t utilization) {
    auto tableau = get_handle<StabilizerTableau>(handle);
    if (!tableau) return Q_MINI_WASM_V2_ERROR_INVALID_HANDLE;
    if (target >= tableau->n) return Q_MINI_WASM_V2_ERROR_OUT_OF_RANGE;
    
    try {
        stabilizer_phase_penalty(tableau.get(), static_cast<uint32_t>(target), utilization);
        return Q_MINI_WASM_V2_OK;
    } catch (...) {
        return Q_MINI_WASM_V2_ERROR_INTERNAL;
    }
}

Q_MINI_WASM_V2_API int qutrit_stabilizer_superposition(void* handle, size_t target) {
    auto tableau = get_handle<StabilizerTableau>(handle);
    if (!tableau) return Q_MINI_WASM_V2_ERROR_INVALID_HANDLE;
    if (target >= tableau->n) return Q_MINI_WASM_V2_ERROR_OUT_OF_RANGE;
    
    try {
        stabilizer_superposition(tableau.get(), static_cast<uint32_t>(target));
        return Q_MINI_WASM_V2_OK;
    } catch (...) {
        return Q_MINI_WASM_V2_ERROR_INTERNAL;
    }
}

Q_MINI_WASM_V2_API uint8_t qutrit_stabilizer_measure(void* handle, size_t target) {
    auto tableau = get_handle<StabilizerTableau>(handle);
    if (!tableau || target >= tableau->n) return 0;
    
    try {
        return stabilizer_measure(tableau.get(), static_cast<uint32_t>(target));
    } catch (...) {
        return 0;
    }
}

Q_MINI_WASM_V2_API int qutrit_stabilizer_entangle_graph(void* handle, const uint32_t* edges, size_t edge_count) {
    auto tableau = get_handle<StabilizerTableau>(handle);
    if (!tableau) return Q_MINI_WASM_V2_ERROR_INVALID_HANDLE;
    if (edges == nullptr) return Q_MINI_WASM_V2_ERROR_INVALID_ARGUMENT;
    
    try {
        stabilizer_entangle_graph(tableau.get(), const_cast<uint32_t*>(edges), static_cast<uint32_t>(edge_count));
        return Q_MINI_WASM_V2_OK;
    } catch (...) {
        return Q_MINI_WASM_V2_ERROR_INTERNAL;
    }
}

Q_MINI_WASM_V2_API size_t qutrit_stabilizer_num_qutrits(void* handle) {
    auto tableau = get_handle<StabilizerTableau>(handle);
    if (!tableau) return 0;
    return tableau->n;
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
        return make_handle(std::shared_ptr<core::moe::MoERouter>(router.release()));
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
        return make_handle(std::shared_ptr<core::learning::ForwardForwardLearner>(learner.release()));
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
        
        return static_cast<double>(learner->compute_goodness(activations_vec));
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
        return make_handle(std::shared_ptr<runtime::RuntimeOrchestrator>(orchestrator.release()));
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

// ============================================================================
// Batch Operations
// ============================================================================

Q_MINI_WASM_V2_API size_t tableau_batch_apply_hadamard(
    void** handles,
    size_t num_handles,
    size_t qutrit
) {
    if (handles == nullptr || num_handles == 0) return 0;
    
    size_t success_count = 0;
    for (size_t i = 0; i < num_handles; ++i) {
        int result = tableau_apply_hadamard(handles[i], qutrit);
        if (result == Q_MINI_WASM_V2_OK) {
            success_count++;
        }
    }
    return success_count;
}

Q_MINI_WASM_V2_API size_t tableau_batch_apply_phase(
    void** handles,
    size_t num_handles,
    size_t qutrit
) {
    if (handles == nullptr || num_handles == 0) return 0;
    
    size_t success_count = 0;
    for (size_t i = 0; i < num_handles; ++i) {
        int result = tableau_apply_phase(handles[i], qutrit);
        if (result == Q_MINI_WASM_V2_OK) {
            success_count++;
        }
    }
    return success_count;
}

Q_MINI_WASM_V2_API size_t tableau_batch_measure_all(
    void** handles,
    size_t num_handles,
    int8_t** outcomes,
    size_t* outcome_sizes,
    size_t max_outcomes_per_tableau
) {
    if (handles == nullptr || outcomes == nullptr || outcome_sizes == nullptr || num_handles == 0) return 0;
    
    size_t total_measured = 0;
    for (size_t i = 0; i < num_handles; ++i) {
        outcome_sizes[i] = tableau_measure_all(handles[i], outcomes[i], max_outcomes_per_tableau);
        total_measured += outcome_sizes[i];
    }
    return total_measured;
}

Q_MINI_WASM_V2_API size_t moe_router_batch_route_topk(
    void** handles,
    size_t num_handles,
    const int8_t* input,
    size_t input_size,
    size_t** selected,
    size_t* selected_counts,
    size_t max_selected_per_router
) {
    if (handles == nullptr || selected == nullptr || selected_counts == nullptr || num_handles == 0) return 0;
    
    size_t total_routed = 0;
    for (size_t i = 0; i < num_handles; ++i) {
        selected_counts[i] = moe_router_route_topk(
            handles[i], input, input_size, selected[i], max_selected_per_router
        );
        total_routed += selected_counts[i];
    }
    return total_routed;
}

Q_MINI_WASM_V2_API size_t ff_learner_batch_forward(
    void** handles,
    size_t num_handles,
    const int8_t* input,
    size_t input_size,
    int8_t** output,
    size_t* output_sizes,
    size_t max_output_per_learner
) {
    if (handles == nullptr || output == nullptr || output_sizes == nullptr || num_handles == 0) return 0;
    
    size_t total_processed = 0;
    for (size_t i = 0; i < num_handles; ++i) {
        output_sizes[i] = ff_learner_forward(
            handles[i], input, input_size, output[i], max_output_per_learner
        );
        total_processed += output_sizes[i];
    }
    return total_processed;
}

// ============================================================================
// Autonomous Training Pipeline Operations
// ============================================================================

#include "../../core/training/autonomous_training_pipeline.hpp"

using namespace q_mini_wasm_v2::core::training;

static std::unordered_map<void*, std::unique_ptr<AutonomousTrainingPipeline>> g_pipelines;
static std::mutex g_pipeline_mutex;

Q_MINI_WASM_V2_API void* training_pipeline_create(
    size_t num_experts,
    size_t graph_nodes,
    int enable_betti_guidance
) {
    try {
        auto pipeline = std::make_unique<AutonomousTrainingPipeline>();
        
        PipelineConfig config;
        config.moe_num_experts = num_experts > 0 ? num_experts : 243;
        config.graph_initial_nodes = graph_nodes > 0 ? graph_nodes : 64;
        config.graph_initial_edges = config.graph_initial_nodes + config.graph_initial_nodes / 2;
        config.enable_betti_guidance = enable_betti_guidance != 0;
        
        if (!pipeline->initialize(config)) {
            return nullptr;
        }
        
        void* handle = pipeline.get();
        
        std::lock_guard<std::mutex> lock(g_pipeline_mutex);
        g_pipelines[handle] = std::move(pipeline);
        
        return handle;
    } catch (...) {
        return nullptr;
    }
}

Q_MINI_WASM_V2_API void training_pipeline_destroy(void* handle) {
    if (handle == nullptr) return;
    
    std::lock_guard<std::mutex> lock(g_pipeline_mutex);
    auto it = g_pipelines.find(handle);
    if (it != g_pipelines.end()) {
        it->second->stop_training();
        g_pipelines.erase(it);
    }
}

Q_MINI_WASM_V2_API int training_pipeline_initialize(
    void* handle,
    size_t acquisition_threads,
    size_t epochs,
    size_t batch_size
) {
    std::lock_guard<std::mutex> lock(g_pipeline_mutex);
    auto it = g_pipelines.find(handle);
    if (it == g_pipelines.end()) return Q_MINI_WASM_V2_ERROR_INVALID_HANDLE;
    
    auto& pipeline = it->second;
    auto config = pipeline->get_config();
    config.acquisition_threads = acquisition_threads > 0 ? acquisition_threads : 4;
    config.num_epochs = epochs > 0 ? epochs : 1000;
    config.batch_size = batch_size > 0 ? batch_size : 32;
    
    if (!pipeline->update_config(config)) {
        return Q_MINI_WASM_V2_ERROR_INTERNAL;
    }
    
    return Q_MINI_WASM_V2_OK;
}

Q_MINI_WASM_V2_API int training_pipeline_start(void* handle) {
    std::lock_guard<std::mutex> lock(g_pipeline_mutex);
    auto it = g_pipelines.find(handle);
    if (it == g_pipelines.end()) return Q_MINI_WASM_V2_ERROR_INVALID_HANDLE;
    
    if (!it->second->start_training()) {
        return Q_MINI_WASM_V2_ERROR_INTERNAL;
    }
    
    return Q_MINI_WASM_V2_OK;
}

Q_MINI_WASM_V2_API void training_pipeline_stop(void* handle) {
    if (handle == nullptr) return;
    
    std::lock_guard<std::mutex> lock(g_pipeline_mutex);
    auto it = g_pipelines.find(handle);
    if (it != g_pipelines.end()) {
        it->second->stop_training();
    }
}

Q_MINI_WASM_V2_API int training_pipeline_pause(void* handle) {
    std::lock_guard<std::mutex> lock(g_pipeline_mutex);
    auto it = g_pipelines.find(handle);
    if (it == g_pipelines.end()) return Q_MINI_WASM_V2_ERROR_INVALID_HANDLE;
    
    it->second->pause_training();
    return Q_MINI_WASM_V2_OK;
}

Q_MINI_WASM_V2_API int training_pipeline_resume(void* handle) {
    std::lock_guard<std::mutex> lock(g_pipeline_mutex);
    auto it = g_pipelines.find(handle);
    if (it == g_pipelines.end()) return Q_MINI_WASM_V2_ERROR_INVALID_HANDLE;
    
    it->second->resume_training();
    return Q_MINI_WASM_V2_OK;
}

Q_MINI_WASM_V2_API size_t training_pipeline_get_metrics(
    void* handle,
    char* metrics_json,
    size_t max_size
) {
    std::lock_guard<std::mutex> lock(g_pipeline_mutex);
    auto it = g_pipelines.find(handle);
    if (it == g_pipelines.end()) return 0;
    
    auto metrics = it->second->get_metrics();
    
    // Format as JSON
    std::string json = "{";
    json += "\"ff_positive_goodness\":" + std::to_string(metrics.ff_positive_goodness) + ",";
    json += "\"ff_negative_goodness\":" + std::to_string(metrics.ff_negative_goodness) + ",";
    json += "\"ff_goodness_delta\":" + std::to_string(metrics.ff_goodness_delta) + ",";
    json += "\"ff_total_train_calls\":" + std::to_string(metrics.ff_total_train_calls) + ",";
    json += "\"ff_route_steps_current_batch\":" + std::to_string(metrics.ff_route_steps_current_batch) + ",";
    json += "\"train_batch_rows_done\":" + std::to_string(metrics.train_batch_rows_done) + ",";
    json += "\"train_batch_collect_limit\":" + std::to_string(metrics.train_batch_collect_limit) + ",";
    json += "\"last_betti_eval_batch\":" + std::to_string(metrics.last_betti_eval_batch) + ",";
    json += "\"used_tritpack5_input_route\":" + std::string(metrics.used_tritpack5_input_route ? "true" : "false") + ",";
    json += "\"router_sycl_path_used\":" + std::string(metrics.router_sycl_path_used ? "true" : "false") + ",";
    json += "\"moe_load_balance_score\":" + std::to_string(metrics.moe_load_balance_score) + ",";
    json += "\"betti_beta_0\":" + std::to_string(metrics.betti_beta_0) + ",";
    json += "\"betti_beta_1\":" + std::to_string(metrics.betti_beta_1) + ",";
    json += "\"betti_beta_2\":" + std::to_string(metrics.betti_beta_2) + ",";
    json += "\"graph_nodes\":" + std::to_string(metrics.graph_nodes) + ",";
    json += "\"graph_edges\":" + std::to_string(metrics.graph_edges) + ",";
    json += "\"ds_total_acquired\":" + std::to_string(metrics.ds_total_acquired) + ",";
    json += "\"ds_total_perturbed\":" + std::to_string(metrics.ds_total_perturbed) + ",";
    json += "\"current_epoch\":" + std::to_string(metrics.current_epoch) + ",";
    json += "\"current_batch\":" + std::to_string(metrics.current_batch) + ",";
    json += "\"training_progress\":" + std::to_string(metrics.training_progress) + ",";
    json += "\"is_running\":" + std::string(metrics.is_running ? "true" : "false") + ",";
    json += "\"status_message\":\"" + metrics.status_message + "\"";
    json += "}";
    
    size_t len = json.length();
    if (len >= max_size) len = max_size - 1;
    
    std::memcpy(metrics_json, json.c_str(), len);
    metrics_json[len] = '\0';
    
    return len;
}

Q_MINI_WASM_V2_API int training_pipeline_apply_betti_guidance(
    void* handle,
    int force
) {
    std::lock_guard<std::mutex> lock(g_pipeline_mutex);
    auto it = g_pipelines.find(handle);
    if (it == g_pipelines.end()) return Q_MINI_WASM_V2_ERROR_INVALID_HANDLE;
    
    it->second->apply_betti_guidance();
    return Q_MINI_WASM_V2_OK;
}

Q_MINI_WASM_V2_API int training_pipeline_export(
    void* handle,
    const char* path,
    int include_topology
) {
    std::lock_guard<std::mutex> lock(g_pipeline_mutex);
    auto it = g_pipelines.find(handle);
    if (it == g_pipelines.end()) return Q_MINI_WASM_V2_ERROR_INVALID_HANDLE;
    
    if (!it->second->export_model(path)) {
        return Q_MINI_WASM_V2_ERROR_INTERNAL;
    }
    
    return Q_MINI_WASM_V2_OK;
}

Q_MINI_WASM_V2_API int training_pipeline_import(
    void* handle,
    const char* path
) {
    std::lock_guard<std::mutex> lock(g_pipeline_mutex);
    auto it = g_pipelines.find(handle);
    if (it == g_pipelines.end()) return Q_MINI_WASM_V2_ERROR_INVALID_HANDLE;
    
    if (!it->second->import_model(path)) {
        return Q_MINI_WASM_V2_ERROR_INTERNAL;
    }
    
    return Q_MINI_WASM_V2_OK;
}

Q_MINI_WASM_V2_API const char* training_pipeline_get_state(void* handle) {
    std::lock_guard<std::mutex> lock(g_pipeline_mutex);
    auto it = g_pipelines.find(handle);
    if (it == g_pipelines.end()) return "invalid";
    
    static thread_local std::string state_str;
    
    switch (it->second->get_state()) {
        case PipelineState::IDLE: state_str = "idle"; break;
        case PipelineState::READY: state_str = "ready"; break;
        case PipelineState::TRAINING: state_str = "training"; break;
        case PipelineState::PAUSED: state_str = "paused"; break;
        case PipelineState::COMPLETE: state_str = "complete"; break;
        case PipelineState::FAILED: state_str = "error"; break;
        default: state_str = "unknown"; break;
    }
    
    return state_str.c_str();
}
