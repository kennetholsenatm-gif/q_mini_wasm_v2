#include <iostream>
#include <vector>
#include <chrono>
#include <cassert>

#include "../core/inference/latency_profiler.hpp"
#include "../core/inference/entanglement_token.hpp"
#include "../core/inference/geometric_context.hpp"
#include "../core/inference/householder_synthesizer.hpp"
#include "../core/inference/inference_pipeline.hpp"
#include "../core/stabilizer/tableau.hpp"
#include "../core/moe/router.hpp"

using namespace q_mini_wasm_v2::core::inference;
using namespace q_mini_wasm_v2::core::stabilizer;
using namespace q_mini_wasm_v2::core::moe;
using namespace q_mini_wasm_v2::core::ternary;

void test_latency_profiler() {
    std::cout << "=== Testing LatencyProfiler ===" << std::endl;
    
    LatencyProfiler profiler(1.0);
    
    profiler.start_run();
    profiler.checkpoint("stage1");
    profiler.checkpoint("stage2");
    profiler.end_run();
    
    auto stats = profiler.get_stats();
    std::cout << "Total runs: " << stats.sample_count << std::endl;
    std::cout << "Avg latency: " << stats.avg_ms << " ms" << std::endl;
    std::cout << "Meets target: " << (stats.meets_sub_millisecond_target ? "YES" : "NO") << std::endl;
    
    assert(stats.sample_count == 1);
    std::cout << "LatencyProfiler test PASSED" << std::endl;
}

void test_entanglement_token() {
    std::cout << "\n=== Testing EntanglementTokenManager ===" << std::endl;
    
    EntanglementTokenManager::TokenConfig config;
    config.vocab_size = 100;
    config.embedding_dim = 8;
    config.max_sequence_length = 32;
    config.entanglement_depth = 4;
    
    auto manager = create_token_manager(config);
    
    std::vector<size_t> input_ids = {1, 2, 3, 4, 5};
    auto sequence = manager->tokenize(input_ids);
    
    std::cout << "Sequence length: " << sequence.sequence_length << std::endl;
    std::cout << "Entanglement entropy: " << sequence.entanglement_entropy << std::endl;
    std::cout << "Number of tokens: " << sequence.tokens.size() << std::endl;
    
    assert(sequence.tokens.size() == 5);
    assert(sequence.sequence_length == 5);
    
    size_t measured = manager->measure_token(sequence.tokens[0]);
    std::cout << "Measured token: " << measured << std::endl;
    
    std::cout << "EntanglementTokenManager test PASSED" << std::endl;
}

void test_geometric_context() {
    std::cout << "\n=== Testing GeometricContextWindow ===" << std::endl;
    
    GeometricContextWindow::ContextConfig config;
    config.max_context_length = 16;
    config.multivector_dim = 8;
    config.manifold_tolerance = 1e-6;
    config.enable_manifold_normalization = true;
    
    auto context = create_geometric_context(config);
    
    auto state = context->initialize_state(10);
    
    std::vector<Trit> token1 = {Trit::POSITIVE, Trit::ZERO, Trit::NEGATIVE, Trit::POSITIVE};
    auto mv1 = context->tokens_to_multivectors({token1})[0];
    
    state = context->update_context(state, mv1);
    
    std::cout << "State length: " << state.current_length << std::endl;
    std::cout << "Total norm: " << state.total_geometric_norm << std::endl;
    
    assert(state.current_length == 1);
    
    auto ctx_vec = context->extract_context_vector(state, 0);
    std::cout << "Context vector size: " << ctx_vec.size() << std::endl;
    
    std::cout << "GeometricContextWindow test PASSED" << std::endl;
}

void test_householder_synthesizer() {
    std::cout << "\n=== Testing HouseholderSynthesizer ===" << std::endl;
    
    HouseholderSynthesizer::SynthesisConfig config;
    config.max_qutrits = 8;
    config.target_precision = 0.1;
    config.max_reflection_depth = 10;
    config.enable_parallel_synthesis = false;
    
    auto synthesizer = create_householder_synthesizer(config);
    
    auto tableau = create_tableau(8);
    
    std::vector<int8_t> target_state = {0, 1, 2, 0, 1, 0, 2, 1};
    auto result = synthesizer->synthesize_target_state(target_state, *tableau);
    
    std::cout << "Gate count: " << result.gate_count << std::endl;
    std::cout << "Synthesis time: " << result.synthesis_time_ms << " ms" << std::endl;
    std::cout << "Approximation error: " << result.approximation_error << std::endl;
    std::cout << "Meets latency target: " << (result.meets_latency_target ? "YES" : "NO") << std::endl;
    
    assert(result.gate_count > 0);
    
    auto reflections = synthesizer->decompose_to_reflections(target_state);
    std::cout << "Number of reflections: " << reflections.size() << std::endl;
    
    std::cout << "HouseholderSynthesizer test PASSED" << std::endl;
}

void test_inference_pipeline() {
    std::cout << "\n=== Testing InferencePipeline ===" << std::endl;
    
    InferencePipelineConfig config;
    
    config.token_config.vocab_size = 100;
    config.token_config.embedding_dim = 8;
    config.token_config.max_sequence_length = 32;
    config.token_config.entanglement_depth = 4;
    
    config.context_config.max_context_length = 16;
    config.context_config.multivector_dim = 8;
    config.context_config.manifold_tolerance = 1e-6;
    config.context_config.enable_manifold_normalization = true;
    
    config.synthesis_config.max_qutrits = 8;
    config.synthesis_config.target_precision = 0.1;
    config.synthesis_config.max_reflection_depth = 10;
    config.synthesis_config.enable_parallel_synthesis = false;
    
    config.moe_config.total_experts = 8;
    config.moe_config.active_experts = 2;
    config.moe_config.routing_qutrits = 4;
    
    config.target_latency_ms = 1.0;
    config.num_experts_to_activate = 2;
    
    auto pipeline = create_inference_pipeline(config);
    
    pipeline->warmup(3);
    
    InferenceInput input;
    input.token_ids = {1, 2, 3, 4, 5};
    input.max_output_length = 5;
    input.temperature = 1.0;
    input.use_entanglement = true;
    input.use_geometric_context = true;
    
    auto output = pipeline->infer(input);
    
    std::cout << "Output tokens: " << output.output_tokens.size() << std::endl;
    std::cout << "Total latency: " << output.total_latency_ms << " ms" << std::endl;
    std::cout << "Meets latency target: " << (output.meets_latency_target ? "YES" : "NO") << std::endl;
    
    assert(output.output_tokens.size() > 0);
    
    auto stats = pipeline->get_performance_stats();
    std::cout << "P95 latency: " << stats.p95_ms << " ms" << std::endl;
    std::cout << "P99 latency: " << stats.p99_ms << " ms" << std::endl;
    
    std::cout << "InferencePipeline test PASSED" << std::endl;
}

void test_sub_millisecond_latency() {
    std::cout << "\n=== Testing Sub-Millisecond Latency Target ===" << std::endl;
    
    InferencePipelineConfig config;
    
    config.token_config.vocab_size = 50;
    config.token_config.embedding_dim = 4;
    config.token_config.max_sequence_length = 16;
    config.token_config.entanglement_depth = 2;
    
    config.context_config.max_context_length = 8;
    config.context_config.multivector_dim = 4;
    config.context_config.manifold_tolerance = 1e-6;
    config.context_config.enable_manifold_normalization = true;
    
    config.synthesis_config.max_qutrits = 4;
    config.synthesis_config.target_precision = 0.1;
    config.synthesis_config.max_reflection_depth = 5;
    config.synthesis_config.enable_parallel_synthesis = false;
    
    config.moe_config.total_experts = 4;
    config.moe_config.active_experts = 2;
    config.moe_config.routing_qutrits = 2;
    
    config.target_latency_ms = 1.0;
    config.num_experts_to_activate = 2;
    
    auto pipeline = create_inference_pipeline(config);
    pipeline->warmup(5);
    
    const size_t num_runs = 100;
    
    InferenceInput input;
    input.token_ids = {0, 1, 2};
    input.max_output_length = 3;
    input.temperature = 1.0;
    input.use_entanglement = true;
    input.use_geometric_context = true;
    
    for (size_t i = 0; i < num_runs; ++i) {
        pipeline->infer(input);
    }
    
    auto stats = pipeline->get_performance_stats();
    
    std::cout << "Total runs: " << stats.sample_count << std::endl;
    std::cout << "Min latency: " << stats.min_ms << " ms" << std::endl;
    std::cout << "Avg latency: " << stats.avg_ms << " ms" << std::endl;
    std::cout << "Max latency: " << stats.max_ms << " ms" << std::endl;
    std::cout << "P50 latency: " << stats.p50_ms << " ms" << std::endl;
    std::cout << "P95 latency: " << stats.p95_ms << " ms" << std::endl;
    std::cout << "P99 latency: " << stats.p99_ms << " ms" << std::endl;
    std::cout << "Meets sub-ms target: " << (stats.meets_sub_millisecond_target ? "YES" : "NO") << std::endl;
    
    std::cout << "Sub-millisecond latency test PASSED" << std::endl;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "QMINIWASM Inference Pipeline Test Suite" << std::endl;
    std::cout << "========================================" << std::endl;
    
    test_latency_profiler();
    test_entanglement_token();
    test_geometric_context();
    test_householder_synthesizer();
    test_inference_pipeline();
    test_sub_millisecond_latency();
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "ALL TESTS PASSED" << std::endl;
    std::cout << "========================================" << std::endl;
    
    return 0;
}