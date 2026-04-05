#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <functional>
#include <unordered_map>
#include "../memory/arena.hpp"
#include "../ternary/trit.hpp"

/**
 * @brief Unified C++ MCP Multiplexer
 * 
 * Phase 2 Consolidation as specified in Quantum Architecture Review §47-73
 * 
 * Consolidates all 30+ Python MCP servers into a single native C++ implementation.
 * Implements MCP protocol natively in memory without IPC, JSON serialization overhead,
 * or separate OS processes.
 * 
 * All agent logic runs directly within the same memory space as the core inference engine
 * using zero-copy access to the shared memory arena.
 */

namespace q_mini_wasm_v2::core::agents {

using McpHandler = std::function<std::span<const uint8_t>(std::span<const uint8_t> params) noexcept>;

enum class McpCapability : uint32_t {
    ANALYZE_CODE          = 1 << 0,
    ANALYZE_DOCUMENTATION = 1 << 1,
    IMPROVEMENT_CYCLE     = 1 << 2,
    KANBAN_REVIEW         = 1 << 3,
    RAG_RETRIEVAL         = 1 << 4,
    RESEARCH_AGENT        = 1 << 5,
    COGNITIVE_LINT        = 1 << 6,
    DEPLOYMENT            = 1 << 7,
    TRAINING              = 1 << 8,
    PERFORMANCE_ANALYSIS  = 1 << 9,
    CODE_GENERATION       = 1 << 10,
    TEST_VALIDATION       = 1 << 11,
    CLEANUP               = 1 << 12
};

class McpMultiplexer {
public:
    /**
     * @brief Construct MCP multiplexer attached to shared memory arena
     */
    explicit McpMultiplexer(memory::MemoryArena& arena) noexcept;

    /**
     * @brief Register a capability handler
     */
    void register_handler(McpCapability capability, McpHandler handler) noexcept;

    /**
     * @brief Dispatch MCP request to appropriate handler
     * @return Zero-copy response buffer from the shared arena
     */
    std::span<const uint8_t> dispatch(std::string_view method, std::span<const uint8_t> params) noexcept;

    /**
     * @brief Query available capabilities
     */
    uint32_t get_capabilities() const noexcept;

    /**
     * @brief Batch execute multiple MCP operations in parallel
     */
    void batch_execute(std::span<const std::pair<std::string_view, std::span<const uint8_t>>> requests) noexcept;

    /**
     * @brief Get direct memory pointer to stabilizer state for in-place agent operations
     * @return Mutable view into the shared memory arena
     */
    std::span<ternary::Trit> get_state_access() noexcept;

private:
    memory::MemoryArena& arena_;
    std::unordered_map<std::string_view, McpHandler> handlers_;
    uint32_t capabilities_mask_ = 0;

    void initialize_native_handlers() noexcept;
};

} // namespace q_mini_wasm_v2::core::agents