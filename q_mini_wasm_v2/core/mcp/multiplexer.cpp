#include "multiplexer.hpp"
#include <cstring>
#include <cstdlib>
#include <algorithm>

namespace q_mini_wasm_v2::core::mcp {

// ============================================================================
// JSON-RPC 2.0 Minimal Parser
// ============================================================================

struct ParsedRequest {
    char method[64];
    size_t method_len;
    size_t params_offset;
    size_t params_len;
    int id;
    bool has_id;
};

/**
 * @brief Minimal JSON-RPC 2.0 request parser
 * 
 * Parses requests without heap allocations using stack buffers.
 * Handles the essential fields: jsonrpc, method, params, id.
 */
static bool parse_jsonrpc_request(const uint8_t* data, size_t len, ParsedRequest& out) {
    // Initialize output
    out.method[0] = '\0';
    out.method_len = 0;
    out.params_offset = 0;
    out.params_len = 0;
    out.id = 0;
    out.has_id = false;
    
    if (len < 20) return false;  // Too short for valid JSON-RPC
    
    // Scan for "method" field
    const char* str = reinterpret_cast<const char*>(data);
    const char* method_key = "\"method\"";
    const char* method_ptr = std::search(str, str + len, 
                                          method_key, method_key + 8);
    
    if (method_ptr == str + len) return false;
    
    // Find the value after "method":
    method_ptr += 8;
    while (method_ptr < str + len && (*method_ptr == ' ' || *method_ptr == ':' || *method_ptr == '"')) {
        method_ptr++;
    }
    
    // Extract method name
    size_t i = 0;
    while (method_ptr < str + len && i < 63 && *method_ptr != '"' && *method_ptr != ' ' && *method_ptr != ',') {
        out.method[i++] = *method_ptr++;
    }
    out.method[i] = '\0';
    out.method_len = i;
    
    // Scan for "params" field
    const char* params_key = "\"params\"";
    const char* params_ptr = std::search(str, str + len,
                                          params_key, params_key + 8);
    
    if (params_ptr != str + len) {
        params_ptr += 8;
        while (params_ptr < str + len && (*params_ptr == ' ' || *params_ptr == ':')) {
            params_ptr++;
        }
        
        // Find params object/array
        if (params_ptr < str + len) {
            char open_char = *params_ptr;
            char close_char = (open_char == '{') ? '}' : (open_char == '[') ? ']' : '\0';
            
            if (close_char != '\0') {
                out.params_offset = static_cast<size_t>(params_ptr - str);
                
                // Find matching close bracket (simple counter, no nesting)
                int depth = 1;
                size_t j = 1;
                while (params_ptr + j < str + len && depth > 0) {
                    if (*(params_ptr + j) == open_char) depth++;
                    else if (*(params_ptr + j) == close_char) depth--;
                    j++;
                }
                out.params_len = j;
            }
        }
    }
    
    // Scan for "id" field
    const char* id_key = "\"id\"";
    const char* id_ptr = std::search(str, str + len,
                                      id_key, id_key + 4);
    
    if (id_ptr != str + len) {
        id_ptr += 4;
        while (id_ptr < str + len && (*id_ptr == ' ' || *id_ptr == ':')) {
            id_ptr++;
        }
        
        // Parse integer ID
        if (id_ptr < str + len && (*id_ptr >= '0' && *id_ptr <= '9')) {
            out.id = 0;
            while (id_ptr < str + len && *id_ptr >= '0' && *id_ptr <= '9') {
                out.id = out.id * 10 + (*id_ptr - '0');
                id_ptr++;
            }
            out.has_id = true;
        }
    }
    
    return out.method_len > 0;
}

// ============================================================================
// Response Builder
// ============================================================================

static size_t build_jsonrpc_response(uint8_t* buffer, size_t buffer_size,
                                      int id, const uint8_t* result, size_t result_len,
                                      bool is_error, const char* error_message) {
    if (buffer_size < 64) return 0;
    
    int written = 0;
    
    if (is_error) {
        written = snprintf(reinterpret_cast<char*>(buffer), buffer_size,
                          "{\"jsonrpc\":\"2.0\",\"id\":%d,\"error\":{\"code\":-32603,\"message\":\"%s\"}}",
                          id, error_message);
    } else {
        written = snprintf(reinterpret_cast<char*>(buffer), buffer_size,
                          "{\"jsonrpc\":\"2.0\",\"id\":%d,\"result\":",
                          id);
        
        if (written > 0 && static_cast<size_t>(written) < buffer_size) {
            // Append result
            size_t remaining = buffer_size - written;
            size_t to_copy = (result_len < remaining - 2) ? result_len : remaining - 2;
            
            memcpy(buffer + written, result, to_copy);
            written += static_cast<int>(to_copy);
            
            // Close JSON
            if (static_cast<size_t>(written) < buffer_size - 1) {
                buffer[written++] = '}';
                buffer[written] = '\0';
            }
        }
    }
    
    return (written > 0) ? static_cast<size_t>(written) : 0;
}

static size_t build_jsonrpc_error(uint8_t* buffer, size_t buffer_size,
                                   const char* error_message) {
    if (buffer_size < 128) return 0;
    
    int written = snprintf(reinterpret_cast<char*>(buffer), buffer_size,
                          "{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":-32600,\"message\":\"%s\"}}",
                          error_message);
    
    return (written > 0) ? static_cast<size_t>(written) : 0;
}

// ============================================================================
// MCP Multiplexer Implementation
// ============================================================================

MCPMultiplexer::MCPMultiplexer(memory::MemoryArena& arena)
    : arena_(arena)
    , handlers_()
{
    handlers_.reserve(32);  // Pre-allocate for typical tool count
}

void MCPMultiplexer::register_tool(const std::string& tool_name, ToolHandler handler) {
    handlers_.emplace_back(tool_name, handler);
}

size_t MCPMultiplexer::execute_request(const uint8_t* request, size_t request_len) noexcept {
    return parse_and_dispatch(request, request_len);
}

const uint8_t* MCPMultiplexer::get_result(size_t result_offset) const noexcept {
    return arena_.get_ptr(result_offset);
}

size_t MCPMultiplexer::parse_and_dispatch(const uint8_t* request, size_t request_len) noexcept {
    // Parse the JSON-RPC request
    ParsedRequest parsed;
    if (!parse_jsonrpc_request(request, request_len, parsed)) {
        // Invalid request - allocate error response in arena
        size_t error_offset = arena_.allocate(256);
        uint8_t* error_buffer = arena_.get_ptr(error_offset);
        
        if (error_buffer) {
            size_t error_len = build_jsonrpc_error(error_buffer, 256,
                                                    "Invalid JSON-RPC request");
            return error_offset;
        }
        return 0;  // Arena allocation failed
    }
    
    // Find matching tool handler
    ToolHandler* handler = nullptr;
    for (auto& [name, h] : handlers_) {
        if (name == parsed.method) {
            handler = &h;
            break;
        }
    }
    
    if (!handler) {
        // Method not found
        size_t error_offset = arena_.allocate(256);
        uint8_t* error_buffer = arena_.get_ptr(error_offset);
        
        if (error_buffer) {
            char msg[128];
            snprintf(msg, sizeof(msg), "Method not found: %s", parsed.method);
            size_t error_len = build_jsonrpc_error(error_buffer, 256, msg);
            return error_offset;
        }
        return 0;
    }
    
    // Allocate result buffer in arena (4KB typical response size)
    size_t result_buffer_size = 4096;
    size_t result_offset = arena_.allocate(result_buffer_size);
    uint8_t* result_buffer = arena_.get_ptr(result_offset);
    
    if (!result_buffer) {
        return 0;  // Arena allocation failed
    }
    
    // Execute the handler
    size_t result_len = 0;
    try {
        result_len = (*handler)(
            request + parsed.params_offset,
            parsed.params_len,
            result_buffer + 256  // Reserve space for JSON-RPC wrapper
        );
    } catch (...) {
        // Handler threw exception
        size_t error_len = build_jsonrpc_response(result_buffer, result_buffer_size,
                                                   parsed.has_id ? parsed.id : 0,
                                                   nullptr, 0,
                                                   true, "Tool execution failed");
        return result_offset;
    }
    
    // Build JSON-RPC response with tool result
    size_t response_len = build_jsonrpc_response(
        result_buffer, result_buffer_size,
        parsed.has_id ? parsed.id : 0,
        result_buffer + 256, result_len,
        false, nullptr
    );
    
    if (response_len == 0) {
        // Response building failed
        size_t error_len = build_jsonrpc_response(result_buffer, result_buffer_size,
                                                   parsed.has_id ? parsed.id : 0,
                                                   nullptr, 0,
                                                   true, "Response serialization failed");
    }
    
    return result_offset;
}

// ============================================================================
// Global Singleton
// ============================================================================

MCPMultiplexer& MCPMultiplexer::instance() noexcept {
    // Static instance with placeholder arena (will be reinitialized)
    static memory::MemoryArena static_arena(1024 * 1024);  // 1MB default
    static MCPMultiplexer instance(static_arena);
    return instance;
}

} // namespace q_mini_wasm_v2::core::mcp
