#include "go_wasm_bridge.hpp"
#include <cstring>
#include "../agents/mcp_multiplexer.hpp"

#ifdef __cplusplus
extern "C" {
#endif

int32_t go_wasm_bridge_initialize(uint64_t memory_base, uint64_t memory_size) {
    auto& bridge = q_mini_wasm_v2::core::go::GoWasmBridge::instance();
    bridge.initialize(reinterpret_cast<uint8_t*>(memory_base), memory_size);
    return 0;
}

uint64_t go_wasm_bridge_get_global_state() {
    auto& bridge = q_mini_wasm_v2::core::go::GoWasmBridge::instance();
    return bridge.unmap_address(bridge.get_arena().get_global_state_buffer());
}

uint64_t go_wasm_bridge_allocate(uint64_t size) {
    auto& bridge = q_mini_wasm_v2::core::go::GoWasmBridge::instance();
    auto buffer = bridge.get_arena().allocate(size);
    return bridge.unmap_address(buffer);
}

void go_wasm_bridge_deallocate(uint64_t address) {
    auto& bridge = q_mini_wasm_v2::core::go::GoWasmBridge::instance();
    auto buffer = bridge.map_address(address);
    bridge.get_arena().deallocate(buffer);
}

uint64_t go_wasm_bridge_dispatch(uint64_t method_address, uint64_t params_address, uint64_t params_size) {
    auto& bridge = q_mini_wasm_v2::core::go::GoWasmBridge::instance();

    auto method_buf = bridge.map_address(method_address);
    auto params_buf = bridge.map_address(params_address);

    std::string_view method(reinterpret_cast<char*>(method_buf.data()), method_buf.size());
    std::span<const uint8_t> params(params_buf.data(), params_size);

    auto response = agents::McpMultiplexer::instance().dispatch(method, params);

    return bridge.unmap_address(response);
}

uint64_t go_wasm_bridge_get_response_size(uint64_t address) {
    auto& bridge = q_mini_wasm_v2::core::go::GoWasmBridge::instance();
    return bridge.map_address(address).size();
}

#ifdef __cplusplus
}
#endif

namespace q_mini_wasm_v2::core::go {

GoWasmBridge& GoWasmBridge::instance() noexcept {
    static GoWasmBridge instance;
    return instance;
}

void GoWasmBridge::initialize(uint8_t* memory_base, size_t memory_size) noexcept {
    memory_base_ = memory_base;
    memory_size_ = memory_size;

    // Place memory arena at offset 1MB into linear memory
    arena_ = new (memory_base + 0x100000) memory::MemoryArena(memory_base + 0x100000, memory_size - 0x100000);
}

memory::MemoryArena& GoWasmBridge::get_arena() noexcept {
    return *arena_;
}

std::span<uint8_t> GoWasmBridge::map_address(uint64_t linear_address) noexcept {
    uint8_t* ptr = memory_base_ + linear_address;
    return std::span<uint8_t>(ptr, *reinterpret_cast<size_t*>(ptr - sizeof(size_t)));
}

uint64_t GoWasmBridge::unmap_address(std::span<const uint8_t> buffer) noexcept {
    return reinterpret_cast<uint64_t>(buffer.data()) - reinterpret_cast<uint64_t>(memory_base_);
}

} // namespace q_mini_wasm_v2::core::go