#pragma once

#include <cstdint>
#include <vector>
#include <memory>
#include <functional>
#include <string>
#include <chrono>
#include "../../core/ternary/trit.hpp"

namespace q_mini_incus::runtime::ipc {

/**
 * @brief Ternary-native IPC message types
 */
enum class MessageType : uint8_t {
    // Control messages
    SHUTDOWN = 0,           // Request service shutdown
    PING = 1,               // Health check
    PONG = 2,               // Health response
    
    // Service management
    SERVICE_CONTROL = 10,   // Start/stop/restart service
    SERVICE_STATUS = 11,    // Query service state
    
    // Data flow
    INFERENCE_REQUEST = 20, // Inference task request
    INFERENCE_RESPONSE = 21,// Inference result
    TRAINING_BATCH = 22,    // Training batch data
    TRAINING_METRICS = 23,  // Training progress
    
    // Boundary operations
    BOUNDARY_CONVERSION = 30, // Namespace boundary crossing
    NETWORK_TX = 31,         // Network transmit (to binary world)
    NETWORK_RX = 32,         // Network receive (from binary world)
    
    // Internal
    IPC_INTERNAL = 40       // Implementation-specific
};

/**
 * @brief Ternary-native message endpoint identifier
 * 
 * Uses 243-expert compatible addressing (5-trit blocks)
 * Each endpoint has a 5-trit address (243 possible endpoints)
 */
struct TernaryEndpoint {
    // 5-trit address: 0-242 (fits in single byte with 5-trit packing)
    uint8_t address;
    
    static constexpr uint8_t BROADCAST = 242;  // All endpoints
    static constexpr uint8_t QMINID = 0;       // Init daemon
    static constexpr uint8_t INFERENCE = 1;    // Inference service
    static constexpr uint8_t TRAINING = 2;     // Training service
    static constexpr uint8_t WUI = 3;          // Web UI service
    static constexpr uint8_t AGENTS = 4;       // Agent service
    
    bool IsValid() const { return address <= BROADCAST; }
};

/**
 * @brief Ternary-native IPC message
 * 
 * All communication within the ternary namespace uses this format.
 * No binary conversion happens here - only at namespace boundaries.
 */
struct TernaryMessage {
    MessageType type;
    TernaryEndpoint source;
    TernaryEndpoint destination;
    
    // Ternary payload - native GF(3) data
    std::vector<q_mini_wasm_v2::core::ternary::Trit> payload;
    
    // Energy-aware priority (from core/ternary/trit.hpp)
    q_mini_wasm_v2::core::ternary::EnergyTrit priority;
    
    // Timestamp in ternary-native time units (not Unix time)
    // Each unit is ~1ms, encoded as 5-trit blocks
    std::vector<q_mini_wasm_v2::core::ternary::Trit> timestamp;
    
    /**
     * @brief Get message size in trits (for flow control)
     */
    size_t SizeInTrits() const {
        return payload.size() + timestamp.size() + 16; // Header overhead
    }
};

/**
 * @brief Ternary-native IPC transport
 * 
 * Uses Unix domain sockets with ternary framing.
 * No binary serialization within the ternary namespace.
 */
class TernaryTransport {
public:
    virtual ~TernaryTransport() = default;
    
    /**
     * @brief Initialize transport
     */
    virtual bool Initialize(const std::string& socket_path) = 0;
    
    /**
     * @brief Send ternary message
     */
    virtual bool Send(const TernaryMessage& msg) = 0;
    
    /**
     * @brief Receive ternary message (blocking)
     */
    virtual bool Receive(TernaryMessage& msg) = 0;
    
    /**
     * @brief Receive with timeout
     */
    virtual bool Receive(TernaryMessage& msg, std::chrono::milliseconds timeout) = 0;
    
    /**
     * @brief Check if transport is connected
     */
    virtual bool IsConnected() const = 0;
    
    /**
     * @brief Close transport
     */
    virtual void Close() = 0;
};

/**
 * @brief Ternary IPC manager (central broker)
 * 
 * Manages all ternary communication within the namespace.
 * Routes messages between services.
 */
class TernaryIPCManager {
public:
    virtual ~TernaryIPCManager() = default;
    
    /**
     * @brief Initialize IPC manager
     * @param container_mode If true, sets up cross-container sockets
     */
    virtual bool Initialize(bool container_mode = false) = 0;
    
    /**
     * @brief Register an endpoint
     */
    virtual bool RegisterEndpoint(TernaryEndpoint endpoint,
                                   const std::string& service_name) = 0;
    
    /**
     * @brief Unregister an endpoint
     */
    virtual void UnregisterEndpoint(TernaryEndpoint endpoint) = 0;
    
    /**
     * @brief Send message to specific endpoint
     */
    virtual bool SendMessage(TernaryEndpoint destination,
                              const TernaryMessage& msg) = 0;
    
    /**
     * @brief Broadcast message to all endpoints
     */
    virtual bool BroadcastMessage(const TernaryMessage& msg) = 0;
    
    /**
     * @brief Set handler for incoming messages
     */
    using MessageHandler = std::function<void(const TernaryMessage&)>;
    virtual void SetMessageHandler(MessageHandler handler) = 0;
    
    /**
     * @brief Poll for messages (non-blocking)
     */
    virtual void PollMessages(std::chrono::milliseconds timeout) = 0;
    
    /**
     * @brief Shutdown IPC manager
     */
    virtual void Shutdown() = 0;
};

/**
 * @brief Factory functions
 */
std::unique_ptr<TernaryTransport> CreateTernaryTransport();
std::unique_ptr<TernaryIPCManager> CreateTernaryIPCManager();

/**
 * @brief Message construction helpers
 */
TernaryMessage CreateShutdownMessage(TernaryEndpoint target);
TernaryMessage CreatePingMessage();
TernaryMessage CreatePongMessage();
TernaryMessage CreateInferenceRequest(const std::vector<q_mini_wasm_v2::core::ternary::Trit>& input);
TernaryMessage CreateInferenceResponse(const std::vector<q_mini_wasm_v2::core::ternary::Trit>& output);

} // namespace q_mini_incus::runtime::ipc
