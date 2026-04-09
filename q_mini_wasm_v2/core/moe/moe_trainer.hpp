#pragma once

#include "unified_router.hpp"
#include "expert_network.hpp"
#include "../learning/forward_forward.hpp"
#include <vector>
#include <string>
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
    int32_t load_balance_alpha_fixed = 10;       // 0.01 in fixed-point (10/1000)
    size_t rebalance_interval = 100;        // Rebalance every N batches
    
    // Expert capacity
    size_t tokens_per_expert = 128;        // Max tokens per expert per batch
    int32_t capacity_factor_fixed = 1250;  // 1.25 in fixed-point (1250/1000)
    
    // Convergence
    int32_t min_goodness_delta_threshold_fixed = 100; // 0.1 in fixed-point (100/1000)
    size_t max_epochs = 100;               // Maximum training epochs
    size_t early_stopping_patience = 10;     // Epochs without improvement
    
    // Monitoring
    ternary::Trit verbose = ternary::Trit::POSITIVE;
    size_t log_interval = 10;              // Log every N batches
};

/**
 * @brief Training metrics for MoE
 */
struct MoETrainingMetrics {
    uint32_t epoch = 0;
    uint32_t batch = 0;
    
    // Forward-Forward metrics (fixed-point: 1000 = 1.0)
    int32_t avg_positive_goodness_fixed = 0;
    int32_t avg_negative_goodness_fixed = 0;
    int32_t avg_goodness_delta_fixed = 0;
    
    // Load balancing (fixed-point)
    int32_t load_balance_loss_fixed = 0;
    int32_t load_balance_score_fixed = 0;
    std::vector<int32_t> expert_utilization_fixed;
    
    // Routing (fixed-point)
    int32_t avg_routing_latency_ms = 0;  // Integer milliseconds
    int32_t avg_confidence_fixed = 0;  // Fixed-point: 1000 = 1.0
    
    // Expert-specific
    std::vector<uint32_t> expert_request_counts;
    std::vector<int32_t> expert_goodness_deltas_fixed;
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
        const std::vector<int32_t>& routing_weights_fixed
    );
    
    /**
     * @brief Compute combined loss (FF goodness + load balancing)
     */
    int32_t ComputeCombinedLoss(
        const std::vector<MoETrainingMetrics>& batch_metrics
    );

    // ========================================================================
    // Load Balancing
    // ========================================================================
    
    /**
     * @brief Compute load balancing loss for current state (fixed-point)
     */
    int32_t ComputeLoadBalancingLoss();
    
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
    ternary::Trit HasConverged();  // Non-const as it modifies internal state
    
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
    mutable MoETrainingMetrics best_metrics_;
    mutable size_t epochs_without_improvement_ = 0;
    
    // Training state
    uint32_t current_epoch_ = 0;
    uint32_t current_batch_ = 0;
    
    // Helper methods
    std::vector<ternary::Trit> GenerateNegativeSample(
        const std::vector<ternary::Trit>& positive
    );
    
    void UpdateMetrics(const std::vector<MoETrainingMetrics>& batch_results);
    
    /**
     * @brief Compute expert diversity using tropical (max-plus) arithmetic
     * @return Diversity score as fixed-point integer (1000 = 1.0, higher = more diverse)
     * 
     * Uses GF(3) compliant tropical inner product instead of floating point cosine similarity.
     * Returns fixed-point representation to avoid float return type.
     */
    int32_t ComputeExpertDiversity() const;
};

/**
 * @brief Factory function
 */
std::unique_ptr<MoETrainer> CreateMoETrainer(
    UnifiedMoERouter& router,
    const MoETrainingConfig& config
);

} // namespace q_mini_wasm_v2::core::moe
