#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include "../memory/arena.hpp"

/**
 * @brief Go-WASM Zero-Copy Memory Bridge
 * 
 * Phase 1 §25: Zero-copy unified memory arena implementation
 * 
 * Provides memory mapping interface between Go gateway and WASM runtime using
 * WASM Memory64 standard. Allows both environments to access the exact same
 * physical memory regions without serialization or copying.
 * 
 * Implements the memory domain resolution specified in Architecture Review §22-28.
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Exported C API for Go/WASM boundary
 * 
 * These functions are directly imported into Go runtime via WASM import table.
 * All memory addresses are linear pointers within the unified WASM address space.
 */

/**
 * @brief Initialize shared memory arena
 * @param memory_base Base address of WASM linear memory
 * @param memory_size Total size of WASM memory region
 * @return 0 on success, error code otherwise
 */
int32_t go_wasm_bridge_initialize(uint64_t memory_base, uint64_t memory_size) __attribute__((export_name("go_wasm_bridge_initialize")));

/**
 * @brief Get pointer to global stabilizer state buffer
 * @return Linear memory address of state buffer
 */
uint64_t go_wasm_bridge_get_global_state() __attribute__((export_name("go_wasm_bridge_get_global_state")));

/**
 * @brief Allocate buffer in shared arena
 * @param size Number of bytes to allocate
 * @return Linear memory address of allocated buffer
 */
uint64_t go_wasm_bridge_allocate(uint64_t size) __attribute__((export_name("go_wasm_bridge_allocate")));

/**
 * @brief Release buffer back to shared arena
 * @param address Linear memory address to release
 */
void go_wasm_bridge_deallocate(uint64_t address) __attribute__((export_name("go_wasm_bridge_deallocate")));

/**
 * @brief Dispatch MCP request from Go to native C++ handlers
 * @param method_address Linear address of method string
 * @param params_address Linear address of parameters buffer
 * @param params_size Size of parameters buffer
 * @return Linear address of response buffer
 */
uint64_t go_wasm_bridge_dispatch(
    uint64_t method_address,
    uint64_t params_address,
    uint64_t params_size
) __attribute__((export_name("go_wasm_bridge_dispatch")));

/**
 * @brief Get response buffer size
 * @param address Response buffer address
 * @return Size of response in bytes
 */
uint64_t go_wasm_bridge_get_response_size(uint64_t address) __attribute__((export_name("go_wasm_bridge_get_response_size")));

#ifdef __cplusplus
}
#endif

namespace q_mini_wasm_v2::core::go {

class GoWasmBridge {
public:
    static GoWasmBridge& instance() noexcept;

    void initialize(uint8_t* memory_base, size_t memory_size) noexcept;

    memory::MemoryArena& get_arena() noexcept;

    std::span<uint8_t> map_address(uint64_t linear_address) noexcept;

    uint64_t unmap_address(std::span<const uint8_t> buffer) noexcept;

private:
    GoWasmBridge() noexcept = default;

    uint8_t* memory_base_ = nullptr;
    size_t memory_size_ = 0;
    memory::MemoryArena* arena_ = nullptr;
};

} // namespace q_mini_wasm_v2::core::go