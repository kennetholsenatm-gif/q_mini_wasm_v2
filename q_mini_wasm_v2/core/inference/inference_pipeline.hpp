#pragma once

#include <vector>
#include <cstdint>
#include <memory>
#include <string>
#include "entanglement_token.hpp"
#include "geometric_context.hpp"
#include "householder_synthesizer.hpp"
#include "latency_profiler.hpp"
#include "../stabilizer/tableau.hpp"
#include "../moe/router.hpp"

namespace q_mini_wasm_v2::core::inference {

/**
 * @brief Inference Pipeline Configuration
 */
struct InferencePipelineConfig {
    EntanglementTokenManager::TokenConfig token_config;
    GeometricContextWindow::ContextConfig context_config;
    HouseholderSynthesizer::SynthesisConfig synthesis_config;
    moe::ExpertConfig moe_config;
    double target_latency_ms;
    size_t num_experts_to_activate;
};

/**
 * @brief Inference Input
 */
struct InferenceInput {
    std::vector<size_t> token_ids;
    size_t max_output_length;
    double temperature;
    bool use_entanglement;
    bool use_geometric_context;
};

/**
 * @brief Inference Output
 */
struct InferenceOutput {
    std::vector<size_t> output_tokens;
    std::vector<double> token_probabilities;
    double total_latency_ms;
    double tokenization_latency_ms;
    double context_latency_ms;
    double synthesis_latency_ms;
    double routing_latency_ms;
    bool meets_latency_target;
    size_t tokens_generated;
};

/**
 * @brief Complete Inference Pipeline
 * 
 * Orchestrates entanglement tokens, geometric context windows,
 * and Householder circuit synthesis for real-time edge AI inference.
 */
class InferencePipeline {
public:
    explicit InferencePipeline(const InferencePipelineConfig& config);
    ~InferencePipeline();

    InferenceOutput infer(const InferenceInput& input);
    
    InferenceOutput infer_streaming(
        const InferenceInput& input,
        std::function<void(size_t)> token_callback
    );

    void warmup(size_t num_iterations = 10);
    LatencyProfiler::LatencyStats get_performance_stats() const;
    const InferencePipelineConfig& config() const { return config_; }

private:
    InferencePipelineConfig config_;
    std::unique_ptr<EntanglementTokenManager> token_manager_;
    std::unique_ptr<GeometricContextWindow> context_window_;
    std::unique_ptr<HouseholderSynthesizer> synthesizer_;
    std::unique_ptr<LatencyProfiler> profiler_;
    std::unique_ptr<moe::MoERouter> moe_router_;
    std::shared_ptr<stabilizer::StabilizerTableau> shared_tableau_;

    InferenceOutput tokenize_stage(const InferenceInput& input);
    InferenceOutput context_stage(InferenceOutput& intermediate);
    InferenceOutput synthesis_stage(InferenceOutput& intermediate);
    InferenceOutput routing_stage(InferenceOutput& intermediate);
    InferenceOutput decode_stage(InferenceOutput& intermediate);
    
    std::vector<int8_t> sample_next_token(
        const std::vector<double>& probabilities,
        double temperature
    );
};

std::unique_ptr<InferencePipeline> create_inference_pipeline(
    const InferencePipelineConfig& config
);

} // namespace q_mini_wasm_v2::core::inference