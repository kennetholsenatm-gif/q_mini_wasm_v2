#pragma once

#include <vector>
#include <memory>
#include <optional>
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
     * @brief Train on specific expert indices (topic-aware specialization)
     * @param sample Single input sample to train
     * @param expert_indices Specific expert indices to train (domain specialists)
     * @param epochs Number of FF passes
     */
    void train_on_experts(const std::vector<double>& sample, 
                          const std::vector<size_t>& expert_indices, 
                          size_t epochs = 1);

    /**
     * @brief Infer (predict) using the trained network
     * @param input Continuous input vector
     * @return Output ternary activations from the final layer
     */
    std::vector<ternary::Trit> infer(const std::vector<double>& input);

    /**
     * @brief Check if expert exists without triggering lazy creation
     * @param expert_id Expert index
     * @return true if expert has been initialized
     */
    bool has_expert(size_t expert_id) const;
    
    /**
     * @brief Get expert by ID for weight serialization - lazy creates if needed
     * @param expert_id Expert index
     * @return Pointer to expert's ForwardForwardLearner, or nullptr if invalid
     */
    learning::ForwardForwardLearner* get_expert(size_t expert_id);
    
    // Get list of all initialized expert IDs (for checkpointing without triggering lazy init)
    std::vector<size_t> get_initialized_expert_ids() const;

private:
    NetworkConfig config_;

    std::unique_ptr<ingestion::DataIngestionPipeline> pipeline_;
    std::unique_ptr<ingestion::CliffordShadow> shadow_;
    std::unique_ptr<moe::MoERouter> router_;
    
    // Each expert is a FF Learner - LAZY INITIALIZED (MoE optimization)
    std::vector<std::optional<std::unique_ptr<learning::ForwardForwardLearner>>> experts_;
    learning::FFConfig expert_config_;  // Config for lazy creation
    
    // Lazy initialize an expert when first accessed
    learning::ForwardForwardLearner* ensure_expert(size_t expert_id);
    
    std::unique_ptr<steane::QutritSteaneCode> steane_;
    std::unique_ptr<runtime::RuntimeOrchestrator> orchestrator_;

    /**
     * @brief Preprocess continuous data into discrete ternary hash
     */
    std::vector<ternary::Trit> preprocess(const std::vector<double>& input);
};

} // namespace q_mini_wasm_v2::core
