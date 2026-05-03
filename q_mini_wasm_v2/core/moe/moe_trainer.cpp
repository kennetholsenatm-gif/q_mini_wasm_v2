#include "moe_trainer.hpp"
#include "gf3_layers.hpp"
#include "../ternary/packing.hpp"
#include <random>
#include <algorithm>
#include <numeric>
#include <iostream>
#include <fstream>

namespace q_mini_wasm_v2::core::moe {

namespace {

void trits_to_ff_pack5(const std::vector<ternary::Trit>& trits, size_t input_dim, std::vector<uint8_t>& out) {
    std::vector<int8_t> lanes(input_dim, 0);
    for (size_t i = 0; i < trits.size() && i < input_dim; ++i) {
        lanes[i] = static_cast<int8_t>(trits[i]);
    }
    q::ternary::pack_batch_t5(lanes, out);
}

} // namespace

// ============================================================================
// Constructor
// ============================================================================

MoETrainer::MoETrainer(
    UnifiedMoERouter& router,
    const MoETrainingConfig& config
)
    : router_(router)
    , config_(config)
    , experts_(router.GetConfig().total_experts)
{
    // Initialize expert networks for all registered experts
    for (size_t i = 0; i < router_.GetConfig().total_experts; ++i) {
        // Create default expert configuration
        ExpertNetwork::ExpertConfig expert_config;
        expert_config.input_dim = router_.GetConfig().routing_dim;
        expert_config.output_dim = router_.GetConfig().routing_dim;
        expert_config.hidden_dim = 128;
        expert_config.num_layers = 2;
        
        // Create and store expert
        RegisterExpert(i, std::make_shared<GF3MultiLayerExpert>(expert_config));
    }
}

// ============================================================================
// Training Loop
// ============================================================================

MoETrainingMetrics MoETrainer::TrainEpoch(
    const std::vector<std::vector<ternary::Trit>>& data
) {
    MoETrainingMetrics epoch_metrics;
    epoch_metrics.epoch = current_epoch_;
    
    // Shuffle data for this epoch
    std::vector<size_t> indices(data.size());
    std::iota(indices.begin(), indices.end(), 0);
    
    std::random_device rd;
    std::mt19937 g(rd() + current_epoch_);
    std::shuffle(indices.begin(), indices.end(), g);
    
    // Process batches
    size_t num_batches = (data.size() + config_.training_batch_size - 1) / config_.training_batch_size;
    std::vector<MoETrainingMetrics> batch_metrics;
    
    for (size_t batch_idx = 0; batch_idx < num_batches; ++batch_idx) {
        size_t start = batch_idx * config_.training_batch_size;
        size_t end = std::min(start + config_.training_batch_size, data.size());
        
        // Extract batch
        std::vector<std::vector<ternary::Trit>> batch_data;
        batch_data.reserve(end - start);
        for (size_t i = start; i < end; ++i) {
            batch_data.push_back(data[indices[i]]);
        }
        
        // Train batch
        auto batch_result = TrainBatch(batch_data);
        batch_metrics.push_back(batch_result);
        
    // Periodic logging (fixed-point output)
        if (config_.verbose == ternary::Trit::POSITIVE && batch_idx % config_.log_interval == 0) {
            std::cout << "  Batch " << batch_idx << "/" << num_batches 
                      << " - Delta: " << batch_result.avg_goodness_delta_fixed << std::endl;
        }
        
        current_batch_++;
    }
    
    // Aggregate batch metrics
    UpdateMetrics(batch_metrics);
    
    // Check load balancing
    epoch_metrics.load_balance_loss_fixed = ComputeLoadBalancingLoss();
    epoch_metrics.load_balance_score_fixed = router_.GetLoadStats().imbalance_score_fixed;
    
    // Update history
    history_.push_back(current_metrics_);
    
    // Check convergence
    if (HasConverged() == ternary::Trit::POSITIVE) {
        std::cout << "Training converged at epoch " << current_epoch_ << std::endl;
    }
    
    current_epoch_++;
    
    // Periodic rebalance
    if (current_epoch_ % config_.rebalance_interval == 0) {
        RebalanceLoads();
    }
    
    return current_metrics_;
}

MoETrainingMetrics MoETrainer::Train(
    const std::vector<std::vector<ternary::Trit>>& data,
    size_t num_epochs
) {
    if (config_.verbose == ternary::Trit::POSITIVE) {
            std::cout << "Starting MoE training for " << num_epochs << " epochs" << std::endl;
            std::cout << "Experts: " << router_.GetConfig().total_experts 
                      << ", Active: " << router_.GetConfig().active_experts << std::endl;
        }
    
    for (size_t epoch = 0; epoch < num_epochs; ++epoch) {
        if (config_.verbose == ternary::Trit::POSITIVE) {
                std::cout << "Epoch " << epoch << "/" << num_epochs << std::endl;
        }
        
        auto metrics = TrainEpoch(data);
        
        if (config_.verbose == ternary::Trit::POSITIVE && epoch % 5 == 0) {
            PrintMetrics(metrics);
        }
        
        // Early stopping check
        if (HasConverged() == ternary::Trit::POSITIVE && epoch > config_.early_stopping_patience) {
                std::cout << "Early stopping at epoch " << epoch << std::endl;
                break;
        }
    }
    
    return current_metrics_;
}

MoETrainingMetrics MoETrainer::TrainBatch(
    const std::vector<std::vector<ternary::Trit>>& batch_data
) {
    MoETrainingMetrics batch_metrics;
    batch_metrics.batch = current_batch_;
    
    int32_t total_pos_goodness = 0;
    int32_t total_neg_goodness = 0;
    int32_t total_delta = 0;
    int32_t total_latency = 0;
    
    // Track expert utilization for this batch
    std::vector<uint32_t> expert_counts(router_.GetConfig().total_experts, 0);
    std::vector<int32_t> expert_deltas(router_.GetConfig().total_experts, 0);
    
    // Process each sample in batch
    for (const auto& sample : batch_data) {
        // Route to experts
        auto routing_result = router_.Route(sample);
        total_latency += routing_result.latency_us;
        
        // Generate negative sample
        auto negative = GenerateNegativeSample(sample);
        
        // Train selected experts
        for (size_t i = 0; i < routing_result.selected_experts.size(); ++i) {
            size_t expert_id = routing_result.selected_experts[i];
            int32_t weight = (i < routing_result.routing_weights_fixed.size()) ? 
                              routing_result.routing_weights_fixed[i] : 1000;
            
            // Get expert
            auto expert = GetExpert(expert_id);
            if (!expert) continue;

            const size_t in_d = expert->GetConfig().input_dim;
            std::vector<uint8_t> pos_pack;
            std::vector<uint8_t> neg_pack;
            trits_to_ff_pack5(sample, in_d, pos_pack);
            trits_to_ff_pack5(negative, in_d, neg_pack);

            // Train with Forward-Forward
            int32_t delta = expert->TrainForwardForward(pos_pack, neg_pack);
            
            // Accumulate metrics (fixed-point)
            total_pos_goodness += expert->ComputeGoodness(expert->Forward(sample));
            total_neg_goodness += expert->ComputeGoodness(expert->Forward(negative));
            total_delta += (delta * weight) / 1000;
            
            // Track expert stats
            expert_counts[expert_id]++;
            expert_deltas[expert_id] += delta;
        }
    }
    
    // Compute averages (fixed-point)
    size_t num_samples = batch_data.size();
    if (num_samples > 0) {
        batch_metrics.avg_positive_goodness_fixed = total_pos_goodness / static_cast<int32_t>(num_samples);
        batch_metrics.avg_negative_goodness_fixed = total_neg_goodness / static_cast<int32_t>(num_samples);
        batch_metrics.avg_goodness_delta_fixed = total_delta / static_cast<int32_t>(num_samples);
        batch_metrics.avg_routing_latency_ms = total_latency / static_cast<int32_t>(num_samples);
    }
    
    // Expert utilization
    batch_metrics.expert_request_counts = expert_counts;
    batch_metrics.expert_goodness_deltas_fixed = expert_deltas;
    
    // Compute utilization rates in fixed-point (scale 1000)
    batch_metrics.expert_utilization_fixed.resize(router_.GetConfig().total_experts);
    uint32_t total_routes = std::accumulate(expert_counts.begin(), expert_counts.end(), 0u);
    if (total_routes > 0) {
        for (size_t i = 0; i < expert_counts.size(); ++i) {
            batch_metrics.expert_utilization_fixed[i] = 
                static_cast<int32_t>((expert_counts[i] * 1000) / total_routes);
        }
    }
    
    // Load balance score (fixed-point from LoadStats)
    batch_metrics.load_balance_score_fixed = router_.GetLoadStats().imbalance_score_fixed;
    
    return batch_metrics;
}

// ============================================================================
// Expert Management
// ============================================================================

void MoETrainer::RegisterExpert(size_t expert_id, std::shared_ptr<ExpertNetwork> expert) {
    if (expert_id < experts_.size()) {
        experts_[expert_id] = expert;
        router_.RegisterExpert(expert_id, expert);
    }
}

std::shared_ptr<ExpertNetwork> MoETrainer::GetExpert(size_t expert_id) {
    if (expert_id < experts_.size()) {
        return experts_[expert_id];
    }
    return nullptr;
}

void MoETrainer::InitializeExperts(int seed) {
    for (auto& expert : experts_) {
        if (expert) {
            // Initialize with Forward-Forward learner if available
            static_cast<GF3MultiLayerExpert*>(expert.get())->InitializeAllLayers(seed++);
        }
    }
}

// ============================================================================
// Forward-Forward Training
// ============================================================================

void MoETrainer::TrainExpertsForwardForward(
    const std::vector<ternary::Trit>& positive,
    const std::vector<size_t>& selected_experts,
    const std::vector<int32_t>& routing_weights_fixed
) {
    // Generate negative sample once for all experts
    auto negative = GenerateNegativeSample(positive);
    
    // Train each selected expert
    for (size_t i = 0; i < selected_experts.size(); ++i) {
        size_t expert_id = selected_experts[i];
        int32_t weight = (i < routing_weights_fixed.size()) ? routing_weights_fixed[i] : 1000;
        
        auto expert = GetExpert(expert_id);
        if (!expert) continue;

        const size_t in_d = expert->GetConfig().input_dim;
        std::vector<uint8_t> pos_pack;
        std::vector<uint8_t> neg_pack;
        trits_to_ff_pack5(positive, in_d, pos_pack);
        trits_to_ff_pack5(negative, in_d, neg_pack);

        // Scale learning by routing weight (fixed-point: weight is scale 1000)
        int32_t scaled_lr = (config_.learning_rate * weight) / 1000;
        if (scaled_lr == 0) scaled_lr = 1;

        // Train
        expert->TrainForwardForward(pos_pack, neg_pack);
    }
}

int32_t MoETrainer::ComputeCombinedLoss(
    const std::vector<MoETrainingMetrics>& batch_metrics
) {
    int32_t total_ff_loss = 0;
    
    for (const auto& metrics : batch_metrics) {
        // Forward-Forward loss: negative of goodness delta (we want to maximize)
        total_ff_loss -= metrics.avg_goodness_delta_fixed;
    }
    
    // Add load balancing loss (already in fixed-point)
    int32_t lb_loss = ComputeLoadBalancingLoss();
    
    // Combine: total = ff_loss + (alpha * lb_loss) / 1000
    return total_ff_loss + (config_.load_balance_alpha_fixed * lb_loss) / 1000;
}

// ============================================================================
// Load Balancing
// ============================================================================

int32_t MoETrainer::ComputeLoadBalancingLoss() {
    auto stats = router_.GetLoadStats();
    
    // Compute variance from uniform distribution (fixed-point)
    // expected_rate = 1000 / total_experts (in fixed-point scale 1000)
    int32_t expected_rate = 1000 / static_cast<int32_t>(router_.GetConfig().total_experts);
    int32_t variance = 0;
    
    for (int32_t rate : stats.utilization_rates_fixed) {
        int32_t diff = rate - expected_rate;
        variance += (diff * diff) / 1000;  // Keep scale at 1000
    }
    
    variance /= static_cast<int32_t>(router_.GetConfig().total_experts);
    
    // Return loss = variance * alpha / 1000
    return (variance * config_.load_balance_alpha_fixed) / 1000;
}

void MoETrainer::ApplyLoadBalancing() {
    router_.RebalanceLoads();
}

void MoETrainer::RebalanceLoads() {
    router_.RebalanceLoads();
}

// ============================================================================
// Monitoring
// ============================================================================

ternary::Trit MoETrainer::HasConverged() {
    if (current_metrics_.avg_goodness_delta_fixed < config_.min_goodness_delta_threshold_fixed) {
        epochs_without_improvement_++;
        
        if (epochs_without_improvement_ >= config_.early_stopping_patience) {
            return ternary::Trit::POSITIVE;
        }
    } else {
        epochs_without_improvement_ = 0;
        
        // Update best metrics
        if (current_metrics_.avg_goodness_delta_fixed > best_metrics_.avg_goodness_delta_fixed) {
            best_metrics_ = current_metrics_;
        }
    }
    
    return ternary::Trit::ZERO;
}

MoETrainingMetrics MoETrainer::GetBestMetrics() const {
    return best_metrics_;
}

void MoETrainer::PrintMetrics(const MoETrainingMetrics& metrics) const {
    std::cout << "=== Epoch " << metrics.epoch << " Metrics ===" << std::endl;
    std::cout << "  Goodness Delta: " << metrics.avg_goodness_delta_fixed << std::endl;
    std::cout << "  Positive Goodness: " << metrics.avg_positive_goodness_fixed << std::endl;
    std::cout << "  Negative Goodness: " << metrics.avg_negative_goodness_fixed << std::endl;
    std::cout << "  Load Balance Score: " << metrics.load_balance_score_fixed << std::endl;
    std::cout << "  Avg Routing Latency: " << metrics.avg_routing_latency_ms << " ms" << std::endl;
    std::cout << "  Active Experts: " << router_.GetConfig().active_experts << std::endl;
}

// ============================================================================
// Helper Methods
// ============================================================================

std::vector<ternary::Trit> MoETrainer::GenerateNegativeSample(
    const std::vector<ternary::Trit>& positive
) {
    static std::mt19937 rng(std::random_device{}());
    
    std::vector<ternary::Trit> negative = positive;
    
    // Corrupt ~10% of values
    std::uniform_int_distribution<size_t> pos_dist(0, positive.size() - 1);
    std::uniform_int_distribution<int> val_dist(-1, 1);
    
    size_t num_corruptions = std::max(size_t(1), positive.size() / 10);
    
    for (size_t i = 0; i < num_corruptions; ++i) {
        size_t pos = pos_dist(rng);
        negative[pos] = static_cast<ternary::Trit>(val_dist(rng));
    }
    
    return negative;
}

void MoETrainer::UpdateMetrics(const std::vector<MoETrainingMetrics>& batch_metrics) {
    if (batch_metrics.empty()) return;
    
    // Average across batches (fixed-point)
    int32_t total_delta = 0;
    int32_t total_pos = 0;
    int32_t total_neg = 0;
    int32_t total_lb = 0;
    int32_t total_latency = 0;
    
    for (const auto& batch : batch_metrics) {
        total_delta += batch.avg_goodness_delta_fixed;
        total_pos += batch.avg_positive_goodness_fixed;
        total_neg += batch.avg_negative_goodness_fixed;
        total_lb += batch.load_balance_score_fixed;
        total_latency += batch.avg_routing_latency_ms;
    }
    
    size_t n = batch_metrics.size();
    current_metrics_.epoch = current_epoch_;
    current_metrics_.avg_goodness_delta_fixed = total_delta / static_cast<int32_t>(n);
    current_metrics_.avg_positive_goodness_fixed = total_pos / static_cast<int32_t>(n);
    current_metrics_.avg_negative_goodness_fixed = total_neg / static_cast<int32_t>(n);
    current_metrics_.load_balance_score_fixed = total_lb / static_cast<int32_t>(n);
    current_metrics_.avg_routing_latency_ms = total_latency / static_cast<int32_t>(n);
    current_metrics_.load_balance_loss_fixed = ComputeLoadBalancingLoss();
    current_metrics_.expert_utilization_fixed = batch_metrics.back().expert_utilization_fixed;
    current_metrics_.expert_request_counts = batch_metrics.back().expert_request_counts;
}

int32_t MoETrainer::ComputeExpertDiversity() const {
    // Compute pairwise dissimilarity between expert specializations using GF(3) arithmetic
    // Lower similarity = higher diversity = better
    // 
    // Uses tropical (max-plus) inner product instead of floating point dot product
    // to maintain constitutional GF(3) purity
    
    const auto& utilization = current_metrics_.expert_utilization_fixed;
    const auto& request_counts = current_metrics_.expert_request_counts;
    const auto& goodness_deltas = current_metrics_.expert_goodness_deltas_fixed;
    
    size_t num_experts = router_.GetConfig().total_experts;
    if (num_experts < 2) return 1000;  // Single expert is trivially diverse (max)
    
    // Build expert specialization vectors
    // Each expert is represented by: [utilization, request_count, goodness_delta]
    // All values scaled to ternary-compatible integers (fixed-point: 1000 = 1.0)
    std::vector<std::vector<int32_t>> expert_vectors(num_experts);
    for (size_t i = 0; i < num_experts; ++i) {
        // Already in fixed-point or integer form
        int32_t util = (i < utilization.size()) ? utilization[i] : 0;
        int32_t req = (i < request_counts.size()) ? static_cast<int32_t>(request_counts[i]) : 0;
        int32_t delta = (i < goodness_deltas.size()) ? goodness_deltas[i] : 0;
        
        expert_vectors[i] = {util, req, delta};
    }
    
    // Compute pairwise tropical dissimilarity
    // Uses max-plus algebra: dissimilarity = max(|a-b|) across all dimensions
    // This is GF(3) compliant - uses only integer subtraction and max operations
    int32_t total_dissimilarity = 0;
    size_t pair_count = 0;
    
    for (size_t i = 0; i < num_experts; ++i) {
        for (size_t j = i + 1; j < num_experts; ++j) {
            const auto& v1 = expert_vectors[i];
            const auto& v2 = expert_vectors[j];
            
            // Tropical dissimilarity: max of absolute differences
            int32_t max_diff = 0;
            for (size_t k = 0; k < v1.size(); ++k) {
                int32_t diff = v1[k] - v2[k];
                if (diff < 0) diff = -diff;  // Absolute value
                if (diff > max_diff) max_diff = diff;
            }
            
            total_dissimilarity += max_diff;
            pair_count++;
        }
    }
    
    // Return fixed-point diversity score (1000 = 1.0 = maximum diversity)
    // Max possible dissimilarity per pair is ~2000 (scaled values)
    int32_t max_possible = 2000;  // Maximum expected dissimilarity
    int32_t diversity = 0;
    if (pair_count > 0) {
        int32_t avg_dissimilarity = total_dissimilarity / static_cast<int32_t>(pair_count);
        // Scale to 0-1000 range
        diversity = (avg_dissimilarity * 1000) / max_possible;
        if (diversity > 1000) diversity = 1000;
    }
    
    return diversity;
}

// ============================================================================
// Checkpointing
// ============================================================================

void MoETrainer::SaveCheckpoint(const std::string& path) const {
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Failed to open checkpoint file: " + path);
    }
    
    // Write config
    file.write(reinterpret_cast<const char*>(&config_), sizeof(config_));
    
    // Write training state
    file.write(reinterpret_cast<const char*>(&current_epoch_), sizeof(current_epoch_));
    file.write(reinterpret_cast<const char*>(&current_batch_), sizeof(current_batch_));
    
    // Write metrics
    file.write(reinterpret_cast<const char*>(&current_metrics_), sizeof(current_metrics_));
    
    // Write best metrics
    file.write(reinterpret_cast<const char*>(&best_metrics_), sizeof(best_metrics_));
    
    // Note: Expert weights would need individual serialization
    // This is a simplified checkpoint
    
    file.close();
}

void MoETrainer::LoadCheckpoint(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Failed to open checkpoint file: " + path);
    }
    
    // Read config (verify compatibility)
    MoETrainingConfig loaded_config;
    file.read(reinterpret_cast<char*>(&loaded_config), sizeof(loaded_config));
    
    // Read training state
    file.read(reinterpret_cast<char*>(&current_epoch_), sizeof(current_epoch_));
    file.read(reinterpret_cast<char*>(&current_batch_), sizeof(current_batch_));
    
    // Read metrics
    file.read(reinterpret_cast<char*>(&current_metrics_), sizeof(current_metrics_));
    
    // Read best metrics
    file.read(reinterpret_cast<char*>(&best_metrics_), sizeof(best_metrics_));
    
    file.close();
}

// ============================================================================
// Factory
// ============================================================================

std::unique_ptr<MoETrainer> CreateMoETrainer(
    UnifiedMoERouter& router,
    const MoETrainingConfig& config
) {
    return std::make_unique<MoETrainer>(router, config);
}

} // namespace q_mini_wasm_v2::core::moe
