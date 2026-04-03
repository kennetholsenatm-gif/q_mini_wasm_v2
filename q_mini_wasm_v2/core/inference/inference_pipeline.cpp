#include "inference_pipeline.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <random>

namespace q_mini_wasm_v2::core::inference {

InferencePipeline::InferencePipeline(const InferencePipelineConfig& config)
    : config_(config) {
    token_manager_ = create_token_manager(config.token_config);
    context_window_ = create_geometric_context(config.context_config);
    synthesizer_ = create_householder_synthesizer(config.synthesis_config);
    profiler_ = std::make_unique<LatencyProfiler>(config.target_latency_ms);
    moe_router_ = moe::create_moe_router(config.moe_config);
    shared_tableau_ = stabilizer::create_tableau(
        config.token_config.embedding_dim * config.token_config.max_sequence_length
    );
}

InferencePipeline::~InferencePipeline() = default;

InferenceOutput InferencePipeline::tokenize_stage(const InferenceInput& input) {
    profiler_->checkpoint("tokenize_start");
    
    InferenceOutput output;
    output.tokens_generated = 0;
    
    auto sequence = token_manager_->tokenize(input.token_ids);
    
    for (const auto& token : sequence.tokens) {
        output.output_tokens.push_back(token.token_id);
        output.token_probabilities.push_back(token.coherence);
    }
    
    profiler_->checkpoint("tokenize_end");
    return output;
}

InferenceOutput InferencePipeline::context_stage(InferenceOutput& intermediate) {
    profiler_->checkpoint("context_start");
    
    auto state = context_window_->initialize_state(intermediate.output_tokens.size());
    
    for (size_t i = 0; i < intermediate.output_tokens.size(); ++i) {
        auto embedding = token_manager_->get_embedding(intermediate.output_tokens[i]);
        auto mv = context_window_->tokens_to_multivectors({embedding})[0];
        state = context_window_->update_context(state, mv);
    }
    
    state = context_window_->apply_manifold_normalization(state);
    
    profiler_->checkpoint("context_end");
    return intermediate;
}

InferenceOutput InferencePipeline::synthesis_stage(InferenceOutput& intermediate) {
    profiler_->checkpoint("synthesis_start");
    
    std::vector<int8_t> target_state;
    for (size_t i = 0; i < std::min(intermediate.output_tokens.size(), config_.synthesis_config.max_qutrits); ++i) {
        target_state.push_back(static_cast<int8_t>(intermediate.output_tokens[i] % 3));
    }
    
    auto result = synthesizer_->synthesize_target_state(target_state, *shared_tableau_);
    
    intermediate.tokenization_latency_ms = result.synthesis_time_ms;
    
    profiler_->checkpoint("synthesis_end");
    return intermediate;
}

InferenceOutput InferencePipeline::routing_stage(InferenceOutput& intermediate) {
    profiler_->checkpoint("routing_start");
    
    std::vector<ternary::Trit> input_trits;
    for (size_t i = 0; i < std::min(intermediate.output_tokens.size(), config_.token_config.embedding_dim); ++i) {
        int val = static_cast<int>(intermediate.output_tokens[i] % 3) - 1;
        input_trits.push_back(static_cast<ternary::Trit>(val));
    }
    
    auto selected_experts = moe_router_->route_topk(input_trits);
    
    profiler_->checkpoint("routing_end");
    return intermediate;
}

InferenceOutput InferencePipeline::decode_stage(InferenceOutput& intermediate) {
    profiler_->checkpoint("decode_start");
    
    std::vector<double> probabilities(intermediate.token_probabilities.size());
    double sum = 0.0;
    for (size_t i = 0; i < probabilities.size(); ++i) {
        probabilities[i] = std::exp(intermediate.token_probabilities[i]);
        sum += probabilities[i];
    }
    
    if (sum > 0) {
        for (auto& p : probabilities) {
            p /= sum;
        }
    }
    
    intermediate.token_probabilities = probabilities;
    
    profiler_->checkpoint("decode_end");
    return intermediate;
}

InferenceOutput InferencePipeline::infer(const InferenceInput& input) {
    profiler_->start_run();
    
    auto output = tokenize_stage(input);
    
    if (input.use_geometric_context) {
        output = context_stage(output);
    }
    
    output = synthesis_stage(output);
    output = routing_stage(output);
    output = decode_stage(output);
    
    output.tokens_generated = output.output_tokens.size();
    
    profiler_->end_run();
    
    auto stats = profiler_->get_stats();
    output.total_latency_ms = stats.avg_ms;
    output.meets_latency_target = profiler_->meets_target();
    
    return output;
}

void InferencePipeline::warmup(size_t num_iterations) {
    InferenceInput warmup_input;
    warmup_input.token_ids = {0, 1, 2, 3, 4};
    warmup_input.max_output_length = 5;
    warmup_input.temperature = 1.0;
    warmup_input.use_entanglement = true;
    warmup_input.use_geometric_context = true;
    
    for (size_t i = 0; i < num_iterations; ++i) {
        infer(warmup_input);
    }
    
    profiler_->reset();
}

LatencyProfiler::LatencyStats InferencePipeline::get_performance_stats() const {
    return profiler_->get_stats();
}

std::unique_ptr<InferencePipeline> create_inference_pipeline(
    const InferencePipelineConfig& config
) {
    return std::make_unique<InferencePipeline>(config);
}

} // namespace q_mini_wasm_v2::core::inference