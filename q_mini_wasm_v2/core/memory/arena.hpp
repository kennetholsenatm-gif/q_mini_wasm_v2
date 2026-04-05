#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <memory>
#include <atomic>
#include "../ternary/trit.hpp"

namespace q_mini_wasm_v2::core::memory {

/**
 * @brief Zero-copy Unified Memory Arena
 * 
 * Implements the shared memory architecture specified in Quantum Architecture Review §2.2.
 * 
 * This arena provides:
 *  - Memory64 compatible linear addressing
 *  - GF(3) native alignment and packing
 *  - Zero-copy WASM <-> C++ access
 *  - Cache-line aligned allocation for SYCL hardware
 *  - Deterministic lifecycle management
 * 
 * The arena is mapped directly into WASM linear memory address space,
 * eliminating all serialization and copy overhead between layers.
 */
class MemoryArena {
public:
    /**
     * @brief Arena configuration flags
     */
    enum class ArenaFlags : uint32_t {
        NONE                = 0,
        WASM_EXPORTED       = 1 << 0,   /// Memory is exported to WASM runtime
        SYCL_SHARED         = 1 << 1,   /// Memory is accessible to SYCL devices
        CACHE_ALIGNED       = 1 << 2,   /// All allocations are cache-line aligned
        ZERO_INITIALIZE     = 1 << 3,   /// Memory is zero-initialized on allocation
        PAGE_LOCKED         = 1 << 4,   /// Memory is pinned for DMA transfers
    };

    /**
     * @brief Construct memory arena with specified capacity
     * @param capacity Total capacity in bytes
     * @param flags Arena configuration flags
     */
    explicit MemoryArena(size_t capacity, ArenaFlags flags = ArenaFlags::NONE);

    /**
     * @brief Destructor - releases all arena memory
     */
    ~MemoryArena();

    /**
     * @brief Allocate aligned memory from arena
     * @param bytes Number of bytes to allocate
     * @param alignment Alignment requirement (power of 2)
     * @return Pointer to allocated memory, nullptr on overflow
     */
    void* allocate(size_t bytes, size_t alignment = alignof(std::max_align_t)) noexcept;

    /**
     * @brief Allocate memory for ternary trit blocks
     * @param num_trits Number of trits to allocate
     * @return Pointer to TritBlock5 packed storage
     */
    ternary::TritBlock5* allocate_trits(size_t num_trits) noexcept;

    /**
     * @brief Reset arena to empty state (invalidates all allocations)
     */
    void reset() noexcept;

    /**
     * @brief Get base pointer of arena memory
     * @return Raw base pointer
     */
    uint8_t* base_pointer() const noexcept { return base_; }

    /**
     * @brief Get total arena capacity
     * @return Capacity in bytes
     */
    size_t capacity() const noexcept { return capacity_; }

    /**
     * @brief Get current used memory
     * @return Used bytes
     */
    size_t used() const noexcept { return offset_.load(std::memory_order_relaxed); }

    /**
     * @brief Check if arena has remaining capacity
     * @param bytes Bytes required
     * @return True if allocation would succeed
     */
    bool has_capacity(size_t bytes) const noexcept;

    /**
     * @brief Get WASM export table offset
     * @return Offset for WASM memory mapping
     */
    size_t wasm_export_offset() const noexcept { return 0; }

private:
    uint8_t* base_ = nullptr;
    size_t capacity_ = 0;
    std::atomic<size_t> offset_ = 0;
    ArenaFlags flags_ = ArenaFlags::NONE;

    // Cache line size for target architecture
    static constexpr size_t CACHE_LINE_SIZE = 64;

    // Align pointer up to specified alignment (power of 2)
    static uintptr_t align_up(uintptr_t ptr, size_t alignment) noexcept {
        return (ptr + alignment - 1) & ~(alignment - 1);
    }

    // Disable copy/move
    MemoryArena(const MemoryArena&) = delete;
    MemoryArena& operator=(const MemoryArena&) = delete;
    MemoryArena(MemoryArena&&) = delete;
    MemoryArena& operator=(MemoryArena&&) = delete;
};

/**
 * @brief Global singleton arena for cross-layer communication
 */
MemoryArena& global_arena() noexcept;

} // namespace q_mini_wasm_v2::core::memory