#pragma once

#include <vector>
#include <memory>
#include "ingestion/absmean_quantizer.hpp"
#include "ingestion/clifford_shadow.hpp"
#include "moe/router.hpp"
#include "learning/forward_forward.hpp"
#include "steane/qutrit_steane.hpp"
#include "../runtime/orchestrator.hpp"

namespace q_mini_wasm_v2::core {

/**
 * @brief Configuration for the Ternary Neural Network
 */
struct NetworkConfig {
    size_t input_dim;
    size_t shadow_dim;
    size_t hash_dim;
    size_t num_layers;
    size_t neurons_per_layer;
    size_t total_experts;
    size_t active_experts;
    size_t routing_qutrits;
    uint32_t learning_rate_shift;
    size_t worker_threads;
    bool enable_steane;
    bool enable_flash_cim;
};

/**
 * @brief Ternary Neural Network Orchestrator
 * 
 * Unifies the QMINIWASM Framework:
 * - Data Ingestion (Absmean + Clifford Shadows + TLSH)
 * - Mixture of Experts (MoE) Entanglement Routing
 * - Forward-Forward Learning with Entanglement Entropy Goodness
 * - Fault-tolerant runtime orchestration via Steane Code polling
 */
class TernaryNeuralNetwork {
public:
    explicit TernaryNeuralNetwork(const NetworkConfig& config);
    ~TernaryNeuralNetwork();

    /**
     * @brief Train the network using Forward-Forward Algorithm
     * @param positive_data Array of continuous input vectors (positive samples)
     * @param epochs Number of FF passes
     */
    void train(const std::vector<std::vector<double>>& positive_data, size_t epochs = 1);

    /**
     * @brief Infer (predict) using the trained network
     * @param input Continuous input vector
     * @return Output ternary activations from the final layer
     */
    std::vector<ternary::Trit> infer(const std::vector<double>& input);

private:
    NetworkConfig config_;

    std::unique_ptr<ingestion::DataIngestionPipeline> pipeline_;
    std::unique_ptr<ingestion::CliffordShadow> shadow_;
    std::unique_ptr<moe::MoERouter> router_;
    
    // Each expert is a FF Learner
    std::vector<std::unique_ptr<learning::ForwardForwardLearner>> experts_;
    
    std::unique_ptr<steane::QutritSteaneCode> steane_;
    std::unique_ptr<runtime::RuntimeOrchestrator> orchestrator_;

    /**
     * @brief Preprocess continuous data into discrete ternary hash
     */
    std::vector<ternary::Trit> preprocess(const std::vector<double>& input);
};

} // namespace q_mini_wasm_v2::core
