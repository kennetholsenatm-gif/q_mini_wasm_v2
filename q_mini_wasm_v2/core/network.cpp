#include "network.hpp"
#include <iostream>
#include <stdexcept>
#include <algorithm>

namespace q_mini_wasm_v2::core {

TernaryNeuralNetwork::TernaryNeuralNetwork(const NetworkConfig& config)
    : config_(config)
{
    std::cerr << "[Network] Starting initialization..." << std::endl;
    
    // Initialize Data Ingestion Pipeline
    std::cerr << "[Network] Creating DataIngestionPipeline..." << std::endl;
    pipeline_ = std::make_unique<ingestion::DataIngestionPipeline>(config.input_dim, config.hash_dim);
    std::cerr << "[Network] DataIngestionPipeline created" << std::endl;
    
    std::cerr << "[Network] Creating CliffordShadow..." << std::endl;
    shadow_ = std::make_unique<ingestion::CliffordShadow>(config.input_dim, 3); // 3 layers of cooling
    std::cerr << "[Network] CliffordShadow created" << std::endl;

    // Initialize MoE Router
    std::cerr << "[Network] Creating MoERouter..." << std::endl;
    moe::ExpertConfig router_config{
        config.total_experts,
        config.active_experts,
        config.routing_qutrits
    };
    router_ = std::make_unique<moe::MoERouter>(router_config);
    std::cerr << "[Network] MoERouter created" << std::endl;

    // Store expert config for lazy initialization (MoE: only create experts when actually used)
    expert_config_.num_layers = config.num_layers;
    expert_config_.neurons_per_layer = config.neurons_per_layer;
    expert_config_.learning_rate_shift = static_cast<int>(config.learning_rate_shift);
    expert_config_.sparsity = 0.05f;  // 5% tropical sparse edges
    expert_config_.lazy_init = true;  // create weights only on first use
    // Resize to total_experts slots, but all empty (lazy init)
    experts_.resize(config.total_experts);
    std::cerr << "[INIT] MoE Router ready: " << config.total_experts << " expert slots, "
              << config.active_experts << " active per forward pass. (Lazy initialization enabled)" << std::endl;

    // Initialize Steane Code
    std::cerr << "[Network] Creating QutritSteaneCode..." << std::endl;
    steane_ = std::make_unique<steane::QutritSteaneCode>();
    std::cerr << "[Network] QutritSteaneCode created" << std::endl;

    // Initialize Runtime Orchestrator
    std::cerr << "[Network] Creating RuntimeOrchestrator..." << std::endl;
    runtime::RuntimeConfig runtime_config{
        config.worker_threads,
        1024,   // max queue size
        true,   // enable async
        config.enable_flash_cim   // enable flash cim
    };
    orchestrator_ = std::make_unique<runtime::RuntimeOrchestrator>(runtime_config);
    std::cerr << "[Network] RuntimeOrchestrator created - Network init complete!" << std::endl;
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
    std::cout << "data: {\"status\": \"progress\", \"step\": \"Starting Ternary Neural Network FF Training...\"}\n\n" << std::flush;

    for (size_t epoch = 0; epoch < epochs; ++epoch) {
        // Prepare preprocessed discrete datasets
        std::vector<std::vector<ternary::Trit>> discrete_positive;
        for (const auto& sample : positive_data) {
            discrete_positive.push_back(preprocess(sample));
        }

        // Layer by layer training with MOE routing
        for (size_t layer = 0; layer < config_.num_layers; ++layer) {
            // Process each sample with MOE routing
            for (const auto& sample : discrete_positive) {
                // Route sample to select top-K experts
                auto expert_indices = router_->route_topk(sample);
                
                // Train only the selected experts (MOE routing)
                for (size_t expert_idx : expert_indices) {
                    auto* expert = ensure_expert(expert_idx);
                    if (!expert) continue;

                    // Generate negative samples via NPID (Non-Parametric Instance Discrimination)
                    std::vector<std::vector<ternary::Trit>> single_sample_positive = {sample};
                    auto discrete_negative = expert->generate_negative_samples(single_sample_positive);
                    
                    // Submit training task asynchronously to the Runtime Orchestrator
                    auto future_goodness = orchestrator_->submit_ff_training(
                        *expert,
                        layer,
                        single_sample_positive,
                        discrete_negative
                    );

                    // Wait for the layer training to finish and log
                    auto goodness = future_goodness.get();
                    
                    // Output JSON for the Go server to stream
                    // We map delta to loss, and positive_goodness to reward, negative_goodness to entropy
                    std::cout << "data: {\"status\": \"epoch\", \"epoch\": " << epoch + 1
                              << ", \"layer\": " << layer
                              << ", \"expert\": " << expert_idx
                              << ", \"loss\": " << goodness.delta 
                              << ", \"reward\": " << goodness.positive_goodness 
                              << ", \"entropy\": " << goodness.negative_goodness 
                              << ", \"batch_size\": 1}\n\n" << std::flush;
                }
            }
        }
    }
    
    // Polling simulation for Steane Codes to ensure fault-tolerance integrity
    if (config_.enable_steane) {
        std::cout << "data: {\"status\": \"progress\", \"step\": \"Running Steane [7,1,3] Fault-Tolerance Parity Poll...\"}\n\n" << std::flush;
        std::vector<ternary::Trit> dummy_phys(7, ternary::Trit::ZERO);
        auto steane_future = orchestrator_->submit_steane_polling(*steane_, dummy_phys);
        int error_loc = steane_future.get();
        if (error_loc != -1) {
            std::cout << "data: {\"status\": \"progress\", \"step\": \"Steane Code corrected error at physical qutrit " << error_loc << "\"}\n\n" << std::flush;
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
        auto* expert = ensure_expert(expert_idx);
        if (!expert) continue;

        auto expert_out = expert->forward(discrete_input);
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

bool TernaryNeuralNetwork::has_expert(size_t expert_id) const {
    if (expert_id >= experts_.size()) return false;
    return experts_[expert_id].has_value();
}

std::vector<size_t> TernaryNeuralNetwork::get_initialized_expert_ids() const {
    std::vector<size_t> ids;
    ids.reserve(experts_.size() / 10);  // Reserve roughly 10% capacity
    for (size_t i = 0; i < experts_.size(); ++i) {
        if (experts_[i].has_value()) {
            ids.push_back(i);
        }
    }
    return ids;
}

learning::ForwardForwardLearner* TernaryNeuralNetwork::ensure_expert(size_t expert_id) {
    if (expert_id >= experts_.size()) return nullptr;
    
    // Lazy initialization: create expert on first access
    if (!experts_[expert_id].has_value()) {
        experts_[expert_id] = std::make_unique<learning::ForwardForwardLearner>(expert_config_);
        std::cerr << "[INIT] Lazy-created expert " << (expert_id + 1) << "/" << experts_.size() << std::endl;
    }
    return experts_[expert_id].value().get();
}

learning::ForwardForwardLearner* TernaryNeuralNetwork::get_expert(size_t expert_id) {
    return ensure_expert(expert_id);
}

void TernaryNeuralNetwork::train_on_experts(const std::vector<double>& sample,
                                             const std::vector<size_t>& expert_indices,
                                             size_t epochs) {
    // Preprocess the single sample
    auto discrete_sample = preprocess(sample);

    for (size_t epoch = 0; epoch < epochs; ++epoch) {
        // Layer by layer training
        for (size_t layer = 0; layer < config_.num_layers; ++layer) {
            // Train ONLY the specified domain experts (not using router!)
            for (size_t expert_idx : expert_indices) {
                auto* expert = ensure_expert(expert_idx);
                if (!expert) continue;

                // Generate negative samples for this expert
                std::vector<std::vector<ternary::Trit>> single_sample_positive = {discrete_sample};
                auto discrete_negative = expert->generate_negative_samples(single_sample_positive);

                // Submit training task to orchestrator
                auto future_goodness = orchestrator_->submit_ff_training(
                    *expert,
                    layer,
                    single_sample_positive,
                    discrete_negative
                );

                // Wait for training to complete
                auto goodness = future_goodness.get();

                // Output domain-specialist training info
                std::cout << "data: {\"status\": \"specialist_train\", \"epoch\": " << epoch + 1
                          << ", \"layer\": " << layer
                          << ", \"expert\": " << expert_idx
                          << ", \"loss\": " << goodness.delta
                          << ", \"reward\": " << goodness.positive_goodness
                          << ", \"entropy\": " << goodness.negative_goodness
                          << "}\n\n" << std::flush;
            }
        }
    }
}

} // namespace q_mini_wasm_v2::core
