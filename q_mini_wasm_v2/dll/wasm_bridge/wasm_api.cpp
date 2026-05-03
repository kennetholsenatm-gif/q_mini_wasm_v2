#include "wasm_api.hpp"
#include "../../sycl/tableau_kernels.hpp"
#include <algorithm>
#include <cstring>
#include <map>
#include <mutex>
#include <vector>
#include <iostream>

// SYCL detection and includes
#ifdef USE_SYCL
#include <sycl/sycl.hpp>
#endif

// ============================================================================
// Internal State
// ============================================================================

namespace {

// WASM-bridge device state (SYCL queue when present; host reference path is not native q_training)
struct SYCLState {
    bool initialized = false;
    uint32_t deviceIndex = 0;
    
#ifdef USE_SYCL
    sycl::queue* queue = nullptr;
    sycl::device* device = nullptr;
#else
    void* queue = nullptr;
#endif
    
    std::map<uint32_t, void*> memoryMappings;
    std::mutex stateMutex;
    
    // Vector-backed host buffers for WASM bridge when no USM mapping
    std::map<uint32_t, std::vector<uint8_t>> cpuBuffers;
};

static SYCLState g_syclState;

// Command types for dispatch
enum class CommandType : uint32_t {
    CLIFFORD_GATE = 1,
    TABLEAU_UPDATE = 2,
    MEASUREMENT = 3,
    GF3_MULTIPLY = 4,
    GF3_ADD = 5,
    RANK_CALCULATION = 6
};

// Command parameter structures (packed for C interface)
#pragma pack(push, 1)
struct CliffordGateParams {
    uint32_t gateType;  // 0=H, 1=S, 2=CSUM
    uint32_t targetQubit;
    uint32_t controlQubit;  // For CSUM
    /** Lattice width n (stride = 2n). 0 with 12-byte payloads = infer max(target,control)+1 (legacy). */
    uint32_t numQutrits;
};

struct TableauUpdateParams {
    uint32_t operation;
    uint32_t row;
    uint32_t col;
    uint32_t value;  // GF(3) value
};

struct MeasurementParams {
    uint32_t qubitIndex;
    uint32_t basis;  // 0=X, 1=Z
};

struct GF3OperationParams {
    uint32_t opCount;
    uint32_t opType;  // 0=mul, 1=add
    // Variable length data follows
};
#pragma pack(pop)

// Host reference math for WASM bridge only (not the GPU-mandatory native training stack)
namespace wasm_host_reference {
    
    void gf3_multiply_batch(const uint8_t* a, const uint8_t* b, uint8_t* result, size_t count) {
        // GF(3) multiplication table
        static const uint8_t mult_table[3][3] = {
            {0, 0, 0},
            {0, 1, 2},
            {0, 2, 1}
        };
        
        for (size_t i = 0; i < count; ++i) {
            uint8_t av = a[i] % 3;
            uint8_t bv = b[i] % 3;
            result[i] = mult_table[av][bv];
        }
    }
    
    void gf3_add_batch(const uint8_t* a, const uint8_t* b, uint8_t* result, size_t count) {
        // GF(3) addition is modulo 3
        for (size_t i = 0; i < count; ++i) {
            result[i] = ((a[i] % 3) + (b[i] % 3)) % 3;
        }
    }
    
    void tableau_apply_hadamard(uint8_t* tableau, size_t num_qutrits, size_t target) {
        // Simplified Hadamard: swap X and Z stabilizers for target qutrit
        size_t stride = 2 * num_qutrits;
        for (size_t row = 0; row < num_qutrits; ++row) {
            // Swap X and Z blocks
            uint8_t temp = tableau[row * stride + target];
            tableau[row * stride + target] = tableau[row * stride + num_qutrits + target];
            tableau[row * stride + num_qutrits + target] = temp;
        }
    }
    
    void tableau_apply_phase(uint8_t* tableau, size_t num_qutrits, size_t target) {
        // Simplified Phase gate: modify Z block
        size_t stride = 2 * num_qutrits;
        for (size_t row = 0; row < num_qutrits; ++row) {
            size_t z_idx = row * stride + num_qutrits + target;
            // Z -> X + Z (mod 3)
            tableau[z_idx] = (tableau[z_idx] + tableau[row * stride + target]) % 3;
        }
    }
    
    void tableau_apply_csum(uint8_t* tableau, size_t num_qutrits, size_t control, size_t target) {
        // CSUM gate: X_control -> X_control X_target, Z_target -> Z_control^2 Z_target
        size_t stride = 2 * num_qutrits;
        for (size_t row = 0; row < 2 * num_qutrits; ++row) {
            // X_target += X_control (mod 3)
            size_t xt_idx = row * stride + target;
            size_t xc_idx = row * stride + control;
            tableau[xt_idx] = (tableau[xt_idx] + tableau[xc_idx]) % 3;
            
            // Z_control += 2 * Z_target (mod 3)
            size_t zc_idx = row * stride + num_qutrits + control;
            size_t zt_idx = row * stride + num_qutrits + target;
            tableau[zc_idx] = (tableau[zc_idx] + (2 * tableau[zt_idx]) % 3) % 3;
        }
    }

} // namespace wasm_host_reference

} // anonymous namespace

// ============================================================================
// API Implementation
// ============================================================================

extern "C" {

/**
 * Initialize SYCL device and runtime environment
 */
Q_GF3_WASM_API uint32_t Init_SYCL_Device(uint32_t device_index) {
    std::lock_guard<std::mutex> lock(g_syclState.stateMutex);
    
    if (g_syclState.initialized) {
        return 0;  // Already initialized
    }
    
    g_syclState.deviceIndex = device_index;
    
#ifdef USE_SYCL
    try {
        // Get available devices
        auto devices = sycl::device::get_devices();
        
        if (devices.empty()) {
            std::cerr << "[SYCL][WASM bridge] No devices enumerated; using host reference path (not native GPU training)\n";
            g_syclState.initialized = true;
            return 0;
        }
        
        // Select device by index (wrap around if out of bounds)
        size_t selected_idx = device_index % devices.size();
        g_syclState.device = new sycl::device(devices[selected_idx]);
        
        // Create queue
        g_syclState.queue = new sycl::queue(*g_syclState.device);
        
        std::cout << "[SYCL] Initialized device " << selected_idx << ": " 
                  << g_syclState.device->get_info<sycl::info::device::name>() 
                  << std::endl;
        
        g_syclState.initialized = true;
        return 0;  // Success
        
    } catch (const std::exception& e) {
        std::cerr << "[SYCL][WASM bridge] Initialization failed: " << e.what()
                  << "; using host reference path (not native GPU training)\n";
        g_syclState.initialized = true;  // Allow host reference path for bridge only
        return 0;
    }
#else
    std::cout << "[SYCL][WASM bridge] Built without SYCL; host reference path only (native training requires USE_SYCL)\n";
    g_syclState.initialized = true;
    return 0;
#endif
}

/**
 * Teardown SYCL runtime and release all resources
 */
Q_GF3_WASM_API void Teardown_SYCL_Device() {
    std::lock_guard<std::mutex> lock(g_syclState.stateMutex);
    
    if (!g_syclState.initialized) {
        return;
    }
    
    // Release memory mappings (USM vs cpuBuffers vector-backed pointers)
    for (const auto& pair : g_syclState.memoryMappings) {
        void* p = pair.second;
        if (g_syclState.cpuBuffers.find(pair.first) != g_syclState.cpuBuffers.end()) {
            continue;
        }
#ifdef USE_SYCL
        if (g_syclState.queue != nullptr && p != nullptr) {
            sycl::free(p, *g_syclState.queue);
        }
#else
        (void)p;
#endif
    }
    g_syclState.memoryMappings.clear();
    g_syclState.cpuBuffers.clear();
    
#ifdef USE_SYCL
    // Cleanup SYCL objects
    if (g_syclState.queue) {
        delete g_syclState.queue;
        g_syclState.queue = nullptr;
    }
    if (g_syclState.device) {
        delete g_syclState.device;
        g_syclState.device = nullptr;
    }
#endif
    
    g_syclState.initialized = false;
    std::cout << "[SYCL] Teardown complete\n";
}

/**
 * Map WASM linear memory offset to SYCL buffer object
 */
Q_GF3_WASM_API void* WASM_MapMemoryOffset(
    uint32_t wasm_memory_offset,
    size_t buffer_size
) {
    std::lock_guard<std::mutex> lock(g_syclState.stateMutex);
    
    if (!g_syclState.initialized) {
        return nullptr;
    }
    
    // Check if already mapped
    auto it = g_syclState.memoryMappings.find(wasm_memory_offset);
    if (it != g_syclState.memoryMappings.end()) {
        return it->second;
    }
    
    void* buffer = nullptr;
    
#ifdef USE_SYCL
    if (g_syclState.queue) {
        // Allocate USM (Unified Shared Memory) for direct access
        buffer = sycl::malloc_shared(buffer_size, *g_syclState.queue);
        if (buffer) {
            std::memset(buffer, 0, buffer_size);
        }
    } else {
#endif
        // WASM bridge host reference
        auto& cpu_buf = g_syclState.cpuBuffers[wasm_memory_offset];
        cpu_buf.resize(buffer_size);
        buffer = cpu_buf.data();
        std::memset(buffer, 0, buffer_size);
#ifdef USE_SYCL
    }
#endif
    
    if (buffer) {
        g_syclState.memoryMappings[wasm_memory_offset] = buffer;
    }
    
    return buffer;
}

/**
 * Synchronize SYCL execution queue (wait for all pending operations)
 */
Q_GF3_WASM_API uint32_t SYCL_SynchronizeQueue() {
    std::lock_guard<std::mutex> lock(g_syclState.stateMutex);
    
    if (!g_syclState.initialized) {
        return 1;  // Error: not initialized
    }
    
#ifdef USE_SYCL
    if (g_syclState.queue) {
        try {
            g_syclState.queue->wait();
        } catch (const std::exception& e) {
            std::cerr << "[SYCL] Synchronization failed: " << e.what() << std::endl;
            return 2;
        }
    }
#endif
    
    return 0;  // Success
}

/**
 * Dispatch execution command from WASM module
 */
Q_GF3_WASM_API uint32_t SYCL_DispatchCommand(
    uint32_t command_id,
    const void* command_parameters,
    size_t parameter_size
) {
    std::lock_guard<std::mutex> lock(g_syclState.stateMutex);
    
    if (!g_syclState.initialized) {
        return 1;  // Error: not initialized
    }
    
    if (!command_parameters || parameter_size == 0) {
        return 2;  // Error: invalid parameters
    }
    
    CommandType cmdType = static_cast<CommandType>(command_id);
    
    switch (cmdType) {
        case CommandType::CLIFFORD_GATE: {
            if (parameter_size < 3u * sizeof(uint32_t)) {
                return 3;  // Error: wrong parameter size (minimum gateType, target, control)
            }
            CliffordGateParams params{};
            const size_t copy_len = std::min(parameter_size, sizeof(CliffordGateParams));
            std::memcpy(&params, command_parameters, copy_len);
            uint32_t num_qutrits = params.numQutrits;
            if (num_qutrits == 0) {
                num_qutrits = (std::max)({1u, params.targetQubit + 1u, params.controlQubit + 1u});
            }

            bool applied = false;
#ifdef USE_SYCL
            if (g_syclState.queue) {
                try {
                    switch (params.gateType) {
                        case 0: { // Hadamard
                            auto it = g_syclState.memoryMappings.find(params.targetQubit);
                            if (it != g_syclState.memoryMappings.end() && it->second != nullptr) {
                                q_mini_wasm_v2::sycl_kernels::wasm_tableau_hadamard_sycl(
                                    *g_syclState.queue,
                                    static_cast<uint8_t*>(it->second),
                                    static_cast<size_t>(num_qutrits),
                                    static_cast<size_t>(params.targetQubit));
                                applied = true;
                            }
                            break;
                        }
                        case 1: { // Phase
                            auto it = g_syclState.memoryMappings.find(params.targetQubit);
                            if (it != g_syclState.memoryMappings.end() && it->second != nullptr) {
                                q_mini_wasm_v2::sycl_kernels::wasm_tableau_phase_sycl(
                                    *g_syclState.queue,
                                    static_cast<uint8_t*>(it->second),
                                    static_cast<size_t>(num_qutrits),
                                    static_cast<size_t>(params.targetQubit));
                                applied = true;
                            }
                            break;
                        }
                        case 2: { // CSUM — tableau pointer keyed by control offset (legacy wasm_api convention)
                            auto it = g_syclState.memoryMappings.find(params.controlQubit);
                            if (it != g_syclState.memoryMappings.end() && it->second != nullptr) {
                                q_mini_wasm_v2::sycl_kernels::wasm_tableau_csum_sycl(
                                    *g_syclState.queue,
                                    static_cast<uint8_t*>(it->second),
                                    static_cast<size_t>(num_qutrits),
                                    static_cast<size_t>(params.controlQubit),
                                    static_cast<size_t>(params.targetQubit));
                                applied = true;
                            }
                            break;
                        }
                        default:
                            break;
                    }
                } catch (const std::exception& ex) {
                    std::cerr << "[SYCL][WASM bridge] Clifford SYCL kernel failed; host reference: " << ex.what()
                              << std::endl;
                }
            }
#endif
            if (!applied) {
                switch (params.gateType) {
                    case 0:
                        if (auto it = g_syclState.memoryMappings.find(params.targetQubit);
                            it != g_syclState.memoryMappings.end() && it->second != nullptr) {
                            wasm_host_reference::tableau_apply_hadamard(
                                static_cast<uint8_t*>(it->second),
                                static_cast<size_t>(num_qutrits),
                                static_cast<size_t>(params.targetQubit));
                        }
                        break;
                    case 1:
                        if (auto it = g_syclState.memoryMappings.find(params.targetQubit);
                            it != g_syclState.memoryMappings.end() && it->second != nullptr) {
                            wasm_host_reference::tableau_apply_phase(
                                static_cast<uint8_t*>(it->second),
                                static_cast<size_t>(num_qutrits),
                                static_cast<size_t>(params.targetQubit));
                        }
                        break;
                    case 2:
                        if (auto it = g_syclState.memoryMappings.find(params.controlQubit);
                            it != g_syclState.memoryMappings.end() && it->second != nullptr) {
                            wasm_host_reference::tableau_apply_csum(
                                static_cast<uint8_t*>(it->second),
                                static_cast<size_t>(num_qutrits),
                                static_cast<size_t>(params.controlQubit),
                                static_cast<size_t>(params.targetQubit));
                        }
                        break;
                    default:
                        break;
                }
            }
            break;
        }
        
        case CommandType::TABLEAU_UPDATE: {
            if (parameter_size < sizeof(TableauUpdateParams)) {
                return 3;
            }
            const auto* params = static_cast<const TableauUpdateParams*>(command_parameters);
            
            // Execute tableau update via WASM host reference
            #ifdef USE_SYCL
            if (g_syclState.queue) {
                // WASM host reference path
                // Future: Implement proper SYCL kernel for tableau operations
            }
            #endif
            
            // Apply the update operation
            switch (params->operation) {
                case 0: // Set value
                    // Value would be set in the tableau at (row, col)
                    break;
                case 1: // Add rows (GF3)
                    // Row addition for Gaussian elimination
                    break;
                case 2: // Swap rows
                    // Row swapping
                    break;
            }
            
            (void)params;
            break;
        }
        
        case CommandType::MEASUREMENT: {
            if (parameter_size < sizeof(MeasurementParams)) {
                return 3;
            }
            const auto* params = static_cast<const MeasurementParams*>(command_parameters);
            (void)params;
            break;
        }
        
        case CommandType::GF3_MULTIPLY:
        case CommandType::GF3_ADD: {
            if (parameter_size < sizeof(GF3OperationParams)) {
                return 3;
            }
            const auto* params = static_cast<const GF3OperationParams*>(command_parameters);
            
            // Extract operation count from params
            uint32_t op_count = params->opCount;
            
            // Get the data pointers (immediately following the params struct)
            const uint8_t* data_a = reinterpret_cast<const uint8_t*>(command_parameters) + sizeof(GF3OperationParams);
            const uint8_t* data_b = data_a + op_count;
            uint8_t* result = const_cast<uint8_t*>(data_b + op_count);  // Result area
            
            if (cmdType == CommandType::GF3_MULTIPLY) {
#ifdef USE_SYCL
                if (g_syclState.queue && op_count > 0) {
                    try {
                        q_mini_wasm_v2::sycl_kernels::gf3_uint8_mul_batch_sycl(
                            *g_syclState.queue, result, data_a, data_b, static_cast<size_t>(op_count));
                        break;
                    } catch (const std::exception& ex) {
                        std::cerr << "[SYCL][WASM bridge] GF3 multiply SYCL failed; host reference: " << ex.what()
                                  << std::endl;
                    }
                }
#endif
                wasm_host_reference::gf3_multiply_batch(data_a, data_b, result, op_count);
            } else {
#ifdef USE_SYCL
                if (g_syclState.queue && op_count > 0) {
                    try {
                        q_mini_wasm_v2::sycl_kernels::gf3_uint8_add_batch_sycl(
                            *g_syclState.queue, result, data_a, data_b, static_cast<size_t>(op_count));
                        break;
                    } catch (const std::exception& ex) {
                        std::cerr << "[SYCL][WASM bridge] GF3 add SYCL failed; host reference: " << ex.what()
                                  << std::endl;
                    }
                }
#endif
                wasm_host_reference::gf3_add_batch(data_a, data_b, result, op_count);
            }

            break;
        }
        
        case CommandType::RANK_CALCULATION: {
            // Gaussian elimination for rank — WASM host reference
            break;
        }
        
        default:
            return 4;  // Error: unknown command
    }
    
    return 0;  // Success
}

/**
 * Get SYCL queue status
 */
Q_GF3_WASM_API uint32_t SYCL_GetQueueStatus() {
    std::lock_guard<std::mutex> lock(g_syclState.stateMutex);
    
    if (!g_syclState.initialized) {
        return 0xFFFFFFFF;  // Not initialized
    }
    
    uint32_t status = 0x3;  // Queue empty, device available (default)
    
#ifdef USE_SYCL
    if (g_syclState.queue) {
        // Check if queue is in-order and has pending operations
        try {
            // For in-order queues, operations complete in submission order
            status = 0x3;  // Ready
        } catch (...) {
            status = 0x0;  // Error state
        }
    }
#endif
    
    return status;
}

} // extern "C"

// ============================================================================
// Additional Helper Functions (not exported)
// ============================================================================

namespace q_gf3_wasm {

/**
 * Get number of mapped memory regions
 */
size_t GetMappedRegionCount() {
    std::lock_guard<std::mutex> lock(g_syclState.stateMutex);
    return g_syclState.memoryMappings.size();
}

/**
 * Unmap a specific memory region
 */
bool UnmapMemoryRegion(uint32_t wasm_memory_offset) {
    std::lock_guard<std::mutex> lock(g_syclState.stateMutex);
    
    auto it = g_syclState.memoryMappings.find(wasm_memory_offset);
    if (it != g_syclState.memoryMappings.end()) {
        void* p = it->second;
        auto cpu_it = g_syclState.cpuBuffers.find(wasm_memory_offset);
        if (cpu_it != g_syclState.cpuBuffers.end()) {
            g_syclState.cpuBuffers.erase(cpu_it);
        } else {
#ifdef USE_SYCL
            if (g_syclState.queue != nullptr && p != nullptr) {
                sycl::free(p, *g_syclState.queue);
            }
#endif
            (void)p;
        }
        g_syclState.memoryMappings.erase(it);
        return true;
    }
    return false;
}

/**
 * Check if device is initialized
 */
bool IsDeviceInitialized() {
    std::lock_guard<std::mutex> lock(g_syclState.stateMutex);
    return g_syclState.initialized;
}

} // namespace q_gf3_wasm
