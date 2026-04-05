#include "arena.hpp"
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <mutex>

namespace q_mini_wasm_v2::core::memory {

MemoryArena::MemoryArena(size_t capacity, ArenaFlags flags)
    : capacity_(capacity)
    , flags_(flags)
{
    const size_t alignment = (static_cast<uint32_t>(flags) & static_cast<uint32_t>(ArenaFlags::CACHE_ALIGNED))
        ? CACHE_LINE_SIZE
        : alignof(std::max_align_t);

    // Allocate aligned memory
    int err = posix_memalign(reinterpret_cast<void**>(&base_), alignment, capacity);
    if (err != 0 || base_ == nullptr) {
        throw std::bad_alloc();
    }

    if (static_cast<uint32_t>(flags) & static_cast<uint32_t>(ArenaFlags::ZERO_INITIALIZE)) {
        std::memset(base_, 0, capacity_);
    }

    offset_.store(0, std::memory_order_release);
}

MemoryArena::~MemoryArena()
{
    if (base_ != nullptr) {
        std::free(base_);
        base_ = nullptr;
    }
}

void* MemoryArena::allocate(size_t bytes, size_t alignment) noexcept
{
    if (bytes == 0) return nullptr;

    // Alignment must be power of 2
    if ((alignment & (alignment - 1)) != 0) {
        return nullptr;
    }

    size_t current_offset = offset_.load(std::memory_order_relaxed);
    uintptr_t current_ptr = reinterpret_cast<uintptr_t>(base_) + current_offset;

    uintptr_t aligned_ptr = align_up(current_ptr, alignment);
    size_t padding = aligned_ptr - current_ptr;
    size_t total_required = padding + bytes;

    if (!has_capacity(total_required)) {
        return nullptr;
    }

    // Atomic compare-exchange for thread-safe allocation
    while (!offset_.compare_exchange_weak(
        current_offset,
        current_offset + total_required,
        std::memory_order_release,
        std::memory_order_relaxed
    )) {
        current_ptr = reinterpret_cast<uintptr_t>(base_) + current_offset;
        aligned_ptr = align_up(current_ptr, alignment);
        padding = aligned_ptr - current_ptr;
        total_required = padding + bytes;

        if (!has_capacity(total_required)) {
            return nullptr;
        }
    }

    return reinterpret_cast<void*>(aligned_ptr);
}

ternary::TritBlock5* MemoryArena::allocate_trits(size_t num_trits) noexcept
{
    const size_t blocks_needed = (num_trits + 4) / 5; // Round up to 5-trit blocks
    return static_cast<ternary::TritBlock5*>(allocate(
        blocks_needed * sizeof(ternary::TritBlock5),
        alignof(ternary::TritBlock5)
    ));
}

void MemoryArena::reset() noexcept
{
    offset_.store(0, std::memory_order_release);
    if (static_cast<uint32_t>(flags_) & static_cast<uint32_t>(ArenaFlags::ZERO_INITIALIZE)) {
        std::memset(base_, 0, capacity_);
    }
}

bool MemoryArena::has_capacity(size_t bytes) const noexcept
{
    return offset_.load(std::memory_order_relaxed) + bytes <= capacity_;
}

MemoryArena& global_arena() noexcept
{
    static std::once_flag init_flag;
    static std::unique_ptr<MemoryArena> instance;

    std::call_once(init_flag, []() {
        instance = std::make_unique<MemoryArena>(
            1024 * 1024 * 128, // 128MB default capacity
            ArenaFlags::WASM_EXPORTED | ArenaFlags::SYCL_SHARED | ArenaFlags::CACHE_ALIGNED
        );
    });

    return *instance;
}

} // namespace q_mini_wasm_v2::core::memory