#include "mcp_multiplexer.hpp"
#include <algorithm>
#include <cstring>

namespace q_mini_wasm_v2::core::agents {

McpMultiplexer::McpMultiplexer(memory::MemoryArena& arena) noexcept
    : arena_(arena)
{
    initialize_native_handlers();
}

void McpMultiplexer::register_handler(McpCapability capability, McpHandler handler) noexcept
{
    const uint32_t mask = static_cast<uint32_t>(capability);
    capabilities_mask_ |= mask;
    
    // Map capability enum to standard MCP method names
    static const std::unordered_map<McpCapability, std::string_view> capability_names = {
        { McpCapability::ANALYZE_CODE, "analyze_code" },
        { McpCapability::ANALYZE_DOCUMENTATION, "analyze_documentation" },
        { McpCapability::IMPROVEMENT_CYCLE, "improvement_cycle" },
        { McpCapability::KANBAN_REVIEW, "kanban_review" },
        { McpCapability::RAG_RETRIEVAL, "rag_retrieval" },
        { McpCapability::RESEARCH_AGENT, "research_agent" },
        { McpCapability::COGNITIVE_LINT, "cognitive_lint" },
        { McpCapability::DEPLOYMENT, "deploy" },
        { McpCapability::TRAINING, "train" },
        { McpCapability::PERFORMANCE_ANALYSIS, "analyze_performance" },
        { McpCapability::CODE_GENERATION, "generate_code" },
        { McpCapability::TEST_VALIDATION, "validate_tests" },
        { McpCapability::CLEANUP, "cleanup" }
    };

    auto it = capability_names.find(capability);
    if (it != capability_names.end()) {
        handlers_[it->second] = std::move(handler);
    }
}

std::span<const uint8_t> McpMultiplexer::dispatch(std::string_view method, std::span<const uint8_t> params) noexcept
{
    auto it = handlers_.find(method);
    if (it != handlers_.end()) {
        return it->second(params);
    }
    
    // Return empty response for unimplemented methods
    return {};
}

uint32_t McpMultiplexer::get_capabilities() const noexcept
{
    return capabilities_mask_;
}

void McpMultiplexer::batch_execute(std::span<const std::pair<std::string_view, std::span<const uint8_t>>> requests) noexcept
{
    // Execute all requests in sequence (parallel execution to be added via SYCL)
    for (const auto& req : requests) {
        dispatch(req.first, req.second);
    }
}

std::span<ternary::Trit> McpMultiplexer::get_state_access() noexcept
{
    return arena_.get_global_state_buffer();
}

void McpMultiplexer::initialize_native_handlers() noexcept
{
    // Native C++ implementation handlers will be registered here
    // Each migrated Python agent will be implemented as a native handler
}

} // namespace q_mini_wasm_v2::core::agents