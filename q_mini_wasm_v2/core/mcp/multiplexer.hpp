#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <functional>
#include <memory>
#include "../memory/arena.hpp"

namespace q_mini_wasm_v2::core::mcp {

/**
 * @brief Unified MCP Multiplexer
 * 
 * Implements Phase 2 Component Consolidation: Agentic Service Mesh
 * 
 * Consolidates all Python MCP servers into a single native C++ state machine.
 * Eliminates 30+ separate Python processes, stdio IPC overhead, and JSON serialization.
 * 
 * Architecture as specified in Quantum Architecture Review §4.2:
 * - Native MCP protocol implementation
 * - Zero-copy message passing via shared memory arena
 * - In-memory tool registration and dispatching
 * - Async event loop optimized for edge execution
 */
class MCPMultiplexer {
public:
    /**
     * @brief Tool handler function signature
     */
    using ToolHandler = std::function<size_t(const uint8_t* params, size_t params_len, uint8_t* result_buffer)>;

    /**
     * @brief Construct MCP multiplexer
     * @param arena Shared memory arena for zero-copy message passing
     */
    explicit MCPMultiplexer(memory::MemoryArena& arena);

    /**
     * @brief Register a tool handler
     * @param tool_name Unique tool identifier
     * @param handler Execution handler function
     */
    void register_tool(const std::string& tool_name, ToolHandler handler);

    /**
     * @brief Execute an MCP request
     * @param request Pointer to request in shared memory
     * @param request_len Length of request data
     * @return Result offset in shared memory arena
     */
    size_t execute_request(const uint8_t* request, size_t request_len) noexcept;

    /**
     * @brief Get result from arena offset
     * @param result_offset Offset returned from execute_request
     * @return Pointer to result data
     */
    const uint8_t* get_result(size_t result_offset) const noexcept;

    /**
     * @brief Get number of registered tools
     */
    size_t registered_tools() const noexcept { return handlers_.size(); }

    /**
     * @brief Global singleton multiplexer instance
     */
    static MCPMultiplexer& instance() noexcept;

private:
    memory::MemoryArena& arena_;
    std::vector<std::pair<std::string, ToolHandler>> handlers_;

    // Parse JSON-RPC 2.0 request without heap allocations
    size_t parse_and_dispatch(const uint8_t* request, size_t request_len) noexcept;
};

} // namespace q_mini_wasm_v2::core::mcp