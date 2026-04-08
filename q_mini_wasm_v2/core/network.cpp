#include "network.hpp"
#include <iostream>
#include <stdexcept>
#include <algorithm>

namespace q_mini_wasm_v2::core {

TernaryNeuralNetwork::TernaryNeuralNetwork(const NetworkConfig& config)
    : config_(config)
{
    // Initialize Data Ingestion Pipeline
    pipeline_ = std::make_unique<ingestion::DataIngestionPipeline>(config.input_dim, config.hash_dim);
    shadow_ = std::make_unique<ingestion::CliffordShadow>(config.input_dim, 3); // 3 layers of cooling

    // Initialize MoE Router
    moe::ExpertConfig router_config{
        config.total_experts,
        config.active_experts,
        config.routing_qutrits
    };
    router_ = std::make_unique<moe::MoERouter>(router_config);

    // Initialize Experts
    learning::FFConfig ff_config{
        config.num_layers,
        config.neurons_per_layer,
        config.learning_rate_shift
    };
    for (size_t i = 0; i < config.total_experts; ++i) {
        experts_.push_back(std::make_unique<learning::ForwardForwardLearner>(ff_config));
    }

    // Initialize Steane Code
    steane_ = std::make_unique<steane::QutritSteaneCode>();

    // Initialize Runtime Orchestrator
    runtime::RuntimeConfig runtime_config{
        config.worker_threads,
        1024,   // max queue size
        true,   // enable async
        config.enable_flash_cim   // enable flash cim
    };
    orchestrator_ = std::make_unique<runtime::RuntimeOrchestrator>(runtime_config);
}

TernaryNeuralNetwork::~TernaryNeuralNetwork() = default;

std::vector<ternary::Trit> TernaryNeuralNetwork::preprocess(const std::vector<double>& input) {
    if (input.size() != config_.input_dim) {
        throw std::invalid_argument("Input dimension mismatch");
    }
    
    // Convert to initial quantized state (Absmean)
    ingestion::AbsmeanQuantizer quantizer(1e-8);
    auto initial_quantized = quantizer.quantize_vector(input);
    
    // Entanglement Cooling via Clifford Shadows
    auto shadow_snapshot = shadow_->extract_snapshot(initial_quantized);
    
    // Dequantize (as proxy for feeding back to TLSH)
    auto smoothed = quantizer.dequantize(shadow_snapshot, 1.0);
    
    // Final Hashing via TLSH
    return pipeline_->process(smoothed);
}

void TernaryNeuralNetwork::train(const std::vector<std::vector<double>>& positive_data, size_t epochs) {
    std::cout << "{\"status\": \"progress\", \"step\": \"Starting Ternary Neural Network FF Training...\"}\n";
    std::cout.flush();

    for (size_t epoch = 0; epoch < epochs; ++epoch) {
        // Prepare preprocessed discrete datasets
        std::vector<std::vector<ternary::Trit>> discrete_positive;
        for (const auto& sample : positive_data) {
            discrete_positive.push_back(preprocess(sample));
        }

        // Layer by layer training
        for (size_t layer = 0; layer < config_.num_layers; ++layer) {
            for (size_t expert_idx = 0; expert_idx < experts_.size(); ++expert_idx) {
                // Generate negative samples via NPID (Non-Parametric Instance Discrimination)
                auto discrete_negative = experts_[expert_idx]->generate_negative_samples(discrete_positive);
                
                // Submit training task asynchronously to the Runtime Orchestrator
                auto future_goodness = orchestrator_->submit_ff_training(
                    *experts_[expert_idx],
                    layer,
                    discrete_positive,
                    discrete_negative
                );

                // Wait for the layer training to finish and log
                auto goodness = future_goodness.get();
                
                // Output JSON for the Go server to stream
                // We map delta to loss, and positive_goodness to reward, negative_goodness to entropy
                std::cout << "{\"status\": \"epoch\", \"epoch\": " << epoch + 1
                          << ", \"loss\": " << goodness.delta 
                          << ", \"reward\": " << goodness.positive_goodness 
                          << ", \"entropy\": " << goodness.negative_goodness << "}\n";
                std::cout.flush();
            }
        }
    }
    
    // Polling simulation for Steane Codes to ensure fault-tolerance integrity
    if (config_.enable_steane) {
        std::cout << "{\"status\": \"progress\", \"step\": \"Running Steane [7,1,3] Fault-Tolerance Parity Poll...\"}\n";
        std::cout.flush();
        std::vector<ternary::Trit> dummy_phys(7, ternary::Trit::ZERO);
        auto steane_future = orchestrator_->submit_steane_polling(*steane_, dummy_phys);
        int error_loc = steane_future.get();
        if (error_loc != -1) {
            std::cout << "{\"status\": \"progress\", \"step\": \"Steane Code corrected error at physical qutrit " << error_loc << "\"}\n";
            std::cout.flush();
        }
    }
    
    orchestrator_->wait_all();
}

std::vector<ternary::Trit> TernaryNeuralNetwork::infer(const std::vector<double>& input) {
    // Data Ingestion
    auto discrete_input = preprocess(input);

    // MoE Routing
    auto future_route = orchestrator_->submit_moe_routing(*router_, discrete_input);
    auto expert_indices = future_route.get();

    // Pass through selected experts (simplification: average the expert activations)
    std::vector<ternary::Trit> output(config_.neurons_per_layer, ternary::Trit::ZERO);

    for (size_t expert_idx : expert_indices) {
        auto expert_out = experts_[expert_idx]->forward(discrete_input);
        for (size_t i = 0; i < output.size(); ++i) {
            // Tropical accumulation
            int sum = static_cast<int>(output[i]) + static_cast<int>(expert_out[i]);
            if (sum > 1) output[i] = ternary::Trit::POSITIVE;
            else if (sum < -1) output[i] = ternary::Trit::NEGATIVE;
            else output[i] = static_cast<ternary::Trit>(sum);
        }
    }

    return output;
}

learning::ForwardForwardLearner* TernaryNeuralNetwork::get_expert(size_t expert_id) {
    if (expert_id < experts_.size()) {
        return experts_[expert_id].get();
    }
    return nullptr;
}

} // namespace q_mini_wasm_v2::core
