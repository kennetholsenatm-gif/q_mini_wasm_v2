#pragma once

#include <cstdint>
#include <vector>
#include <memory>
#include <string>
#include "../../runtime/ipc/ternary_ipc.hpp"
#include "../../../q_mini_wasm_v2/core/moe/router.hpp"
#include "../../../q_mini_wasm_v2/core/inference/inference_pipeline.hpp"

namespace q_mini_incus::containers::inference {

/**
 * @brief Ternary-native inference service for Incus containers
 * 
 * Runs inside the ternary-native namespace and communicates via
 * ternary IPC. No binary conversion except at namespace boundary.
 */
class InferenceService {
public:
    struct Config {
        size_t num_experts = 243;
        size_t active_experts = 16;
        size_t input_dim = 64;
        size_t output_dim = 64;
        std::string model_path;
        bool enable_load_balancing = true;
        bool enable_betti_guidance = true;
    };
    
    explicit InferenceService(const Config& config);
    ~InferenceService();
    
    /**
     * @brief Initialize the service
     */
    bool Initialize();
    
    /**
     * @brief Run the service (blocking)
     */
    void Run();
    
    /**
     * @brief Request graceful shutdown
     */
    void Shutdown();
    
    /**
     * @brief Get service statistics
     */
    struct Stats {
        uint64_t requests_processed = 0;
        uint64_t avg_latency_us = 0;
        uint64_t errors = 0;
        float load_balance_score = 0.0f;
    };
    Stats GetStats() const;
    
private:
    Config config_;
    bool shutdown_requested_{false};
    
    // Core components (ternary-native)
    std::unique_ptr<q_mini_wasm_v2::core::moe::MoERouter> router_;
    std::unique_ptr<q_mini_wasm_v2::core::inference::InferencePipeline> pipeline_;
    
    // IPC
    std::unique_ptr<ipc::TernaryTransport> transport_;
    
    // Statistics
    mutable std::mutex stats_mutex_;
    Stats stats_;
    
    // Message handlers
    void OnInferenceRequest(const ipc::TernaryMessage& msg);
    void OnServiceControl(const ipc::TernaryMessage& msg);
    void OnShutdown();
    
    // Core inference
    std::vector<q_mini_wasm_v2::core::ternary::Trit> DoInference(
        const std::vector<q_mini_wasm_v2::core::ternary::Trit>& input
    );
};

/**
 * @brief Factory function
 */
std::unique_ptr<InferenceService> CreateInferenceService(const InferenceService::Config& config);

} // namespace q_mini_incus::containers::inference
