#pragma once

#include "unified_router.hpp"
#include "expert_network.hpp"
#include "../learning/forward_forward.hpp"
#include <vector>
#include <memory>
#include <functional>

namespace q_mini_wasm_v2::core::moe {

/**
 * @brief Training configuration for 243-expert MoE
 */
struct MoETrainingConfig {
    // Forward-Forward settings
    int32_t learning_rate = 1;              // GF(3) learning rate (typically ±1)
    size_t negative_samples_per_positive = 1; // N negative samples per positive
    uint32_t training_batch_size = 64;     // Tokens per batch
    
    // Load balancing
    float load_balance_alpha = 0.01f;       // Load balancing loss weight
    size_t rebalance_interval = 100;        // Rebalance every N batches
    
    // Expert capacity
    size_t tokens_per_expert = 128;        // Max tokens per expert per batch
    float capacity_factor = 1.25f;         // Capacity buffer
    
    // Convergence
    float min_goodness_delta_threshold = 0.1f; // Stop if delta below this
    size_t max_epochs = 100;               // Maximum training epochs
    size_t early_stopping_patience = 10;     // Epochs without improvement
    
    // Monitoring
    bool verbose = true;
    size_t log_interval = 10;              // Log every N batches
};

/**
 * @brief Training metrics for MoE
 */
struct MoETrainingMetrics {
    uint32_t epoch = 0;
    uint32_t batch = 0;
    
    // Forward-Forward metrics
    float avg_positive_goodness = 0.0f;
    float avg_negative_goodness = 0.0f;
    float avg_goodness_delta = 0.0f;
    
    // Load balancing
    float load_balance_loss = 0.0f;
    float load_balance_score = 0.0f;
    std::vector<float> expert_utilization;
    
    // Routing
    float avg_routing_latency_ms = 0.0f;
    float avg_confidence = 0.0f;
    
    // Expert-specific
    std::vector<uint32_t> expert_request_counts;
    std::vector<float> expert_goodness_deltas;
};

/**
 * @brief Batch of training data
 */
struct TrainingBatch {
    std::vector<std::vector<ternary::Trit>> inputs;
    std::vector<std::vector<ternary::Trit>> targets;  // Optional for supervised
    std::vector<size_t> expert_assignments;  // Which experts should process each sample
};

/**
 * @brief MoE Trainer - End-to-end training with Forward-Forward
 * 
 * Implements the training loop for 243-expert MoE:
 * 1. Route inputs to experts using unified router
 * 2. Train selected experts with Forward-Forward
 * 3. Apply load balancing auxiliary loss
 * 4. Monitor convergence and expert specialization
 */
class MoETrainer {
public:
    MoETrainer(
        UnifiedMoERouter& router,
        const MoETrainingConfig& config
    );
    
    ~MoETrainer() = default;

    // ========================================================================
    // Training Loop
    // ========================================================================
    
    /**
     * @brief Train for one epoch
     * 
     * @param data Training data (positive samples)
     * @return Metrics for this epoch
     */
    MoETrainingMetrics TrainEpoch(
        const std::vector<std::vector<ternary::Trit>>& data
    );
    
    /**
     * @brief Train for multiple epochs
     * 
     * @param data Training data
     * @param num_epochs Number of epochs to train
     * @return Final metrics
     */
    MoETrainingMetrics Train(
        const std::vector<std::vector<ternary::Trit>>& data,
        size_t num_epochs
    );
    
    /**
     * @brief Train single batch
     */
    MoETrainingMetrics TrainBatch(
        const std::vector<std::vector<ternary::Trit>>& batch_data
    );

    // ========================================================================
    // Expert Management
    // ========================================================================
    
    /**
     * @brief Register expert network with trainer
     */
    void RegisterExpert(size_t expert_id, std::shared_ptr<ExpertNetwork> expert);
    
    /**
     * @brief Get expert by ID
     */
    std::shared_ptr<ExpertNetwork> GetExpert(size_t expert_id);
    
    /**
     * @brief Initialize all experts with random weights
     */
    void InitializeExperts(int seed = 42);

    // ========================================================================
    // Forward-Forward Training
    // ========================================================================
    
    /**
     * @brief Train selected experts with Forward-Forward
     * 
     * For each sample:
     * 1. Route to top-K experts
     * 2. Generate negative sample (corruption)
     * 3. Train each selected expert: maximize(pos_goodness - neg_goodness)
     * 4. Aggregate expert updates
     */
    void TrainExpertsForwardForward(
        const std::vector<ternary::Trit>& positive,
        const std::vector<size_t>& selected_experts,
        const std::vector<float>& routing_weights
    );
    
    /**
     * @brief Compute combined loss (FF goodness + load balancing)
     */
    float ComputeCombinedLoss(
        const std::vector<MoETrainingMetrics>& batch_metrics
    );

    // ========================================================================
    // Load Balancing
    // ========================================================================
    
    /**
     * @brief Compute load balancing loss for current state
     */
    float ComputeLoadBalancingLoss();
    
    /**
     * @brief Apply load balancing penalty to routing
     */
    void ApplyLoadBalancing();
    
    /**
     * @brief Rebalance expert loads (reset counters)
     */
    void RebalanceLoads();

    // ========================================================================
    // Monitoring
    // ========================================================================
    
    /**
     * @brief Get current training metrics
     */
    MoETrainingMetrics GetMetrics() const { return current_metrics_; }
    
    /**
     * @brief Get training history
     */
    const std::vector<MoETrainingMetrics>& GetHistory() const { return history_; }
    
    /**
     * @brief Check if training has converged
     */
    bool HasConverged() const;
    
    /**
     * @brief Get best epoch metrics
     */
    MoETrainingMetrics GetBestMetrics() const;
    
    /**
     * @brief Print current metrics
     */
    void PrintMetrics(const MoETrainingMetrics& metrics) const;

    // ========================================================================
    // Checkpointing
    // ========================================================================
    
    /**
     * @brief Save training checkpoint
     */
    void SaveCheckpoint(const std::string& path) const;
    
    /**
     * @brief Load training checkpoint
     */
    void LoadCheckpoint(const std::string& path);

private:
    UnifiedMoERouter& router_;
    MoETrainingConfig config_;
    
    // Expert networks
    std::vector<std::shared_ptr<ExpertNetwork>> experts_;
    
    // Metrics
    MoETrainingMetrics current_metrics_;
    std::vector<MoETrainingMetrics> history_;
    MoETrainingMetrics best_metrics_;
    size_t epochs_without_improvement_ = 0;
    
    // Training state
    uint32_t current_epoch_ = 0;
    uint32_t current_batch_ = 0;
    
    // Helper methods
    std::vector<ternary::Trit> GenerateNegativeSample(
        const std::vector<ternary::Trit>& positive
    );
    
    void UpdateMetrics(const std::vector<MoETrainingMetrics>& batch_results);
    
    float ComputeExpertDiversity() const;
};

/**
 * @brief Factory function
 */
std::unique_ptr<MoETrainer> CreateMoETrainer(
    UnifiedMoERouter& router,
    const MoETrainingConfig& config
);

} // namespace q_mini_wasm_v2::core::moe
