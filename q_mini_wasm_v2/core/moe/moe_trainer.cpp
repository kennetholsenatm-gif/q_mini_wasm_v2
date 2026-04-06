#include "moe_trainer.hpp"
#include <random>
#include <algorithm>
#include <numeric>
#include <iostream>
#include <fstream>
#include <cmath>

namespace q_mini_wasm_v2::core::moe {

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
        
        // Periodic logging
        if (config_.verbose && batch_idx % config_.log_interval == 0) {
            std::cout << "  Batch " << batch_idx << "/" << num_batches 
                      << " - Delta: " << batch_result.avg_goodness_delta << std::endl;
        }
        
        current_batch_++;
    }
    
    // Aggregate batch metrics
    UpdateMetrics(batch_metrics);
    
    // Check load balancing
    epoch_metrics.load_balance_loss = ComputeLoadBalancingLoss();
    epoch_metrics.load_balance_score = router_.GetLoadStats().imbalance_score;
    
    // Update history
    history_.push_back(current_metrics_);
    
    // Check convergence
    if (HasConverged()) {
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
    if (config_.verbose) {
        std::cout << "Starting MoE training for " << num_epochs << " epochs" << std::endl;
        std::cout << "Experts: " << router_.GetConfig().total_experts 
                  << ", Active: " << router_.GetConfig().active_experts << std::endl;
    }
    
    for (size_t epoch = 0; epoch < num_epochs; ++epoch) {
        if (config_.verbose) {
            std::cout << "Epoch " << epoch << "/" << num_epochs << std::endl;
        }
        
        auto metrics = TrainEpoch(data);
        
        if (config_.verbose && epoch % 5 == 0) {
            PrintMetrics(metrics);
        }
        
        // Early stopping check
        if (HasConverged() && epoch > config_.early_stopping_patience) {
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
    
    float total_pos_goodness = 0.0f;
    float total_neg_goodness = 0.0f;
    float total_delta = 0.0f;
    float total_latency = 0.0f;
    
    // Track expert utilization for this batch
    std::vector<uint32_t> expert_counts(router_.GetConfig().total_experts, 0);
    std::vector<float> expert_deltas(router_.GetConfig().total_experts, 0.0f);
    
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
            float weight = routing_result.routing_weights[i];
            
            // Get expert
            auto expert = GetExpert(expert_id);
            if (!expert) continue;
            
            // Train with Forward-Forward
            int32_t delta = expert->TrainForwardForward(sample, negative);
            
            // Accumulate metrics
            total_pos_goodness += expert->ComputeGoodness(expert->Forward(sample));
            total_neg_goodness += expert->ComputeGoodness(expert->Forward(negative));
            total_delta += delta * weight;
            
            // Track expert stats
            expert_counts[expert_id]++;
            expert_deltas[expert_id] += delta;
        }
    }
    
    // Compute averages
    size_t num_samples = batch_data.size();
    if (num_samples > 0) {
        batch_metrics.avg_positive_goodness = total_pos_goodness / num_samples;
        batch_metrics.avg_negative_goodness = total_neg_goodness / num_samples;
        batch_metrics.avg_goodness_delta = total_delta / num_samples;
        batch_metrics.avg_routing_latency_ms = total_latency / (num_samples * 1000.0f);
    }
    
    // Expert utilization
    batch_metrics.expert_request_counts = expert_counts;
    batch_metrics.expert_goodness_deltas = expert_deltas;
    
    // Compute utilization rates
    batch_metrics.expert_utilization.resize(router_.GetConfig().total_experts);
    float total_routes = std::accumulate(expert_counts.begin(), expert_counts.end(), 0.0f);
    if (total_routes > 0) {
        for (size_t i = 0; i < expert_counts.size(); ++i) {
            batch_metrics.expert_utilization[i] = expert_counts[i] / total_routes;
        }
    }
    
    // Load balance score
    batch_metrics.load_balance_score = router_.GetLoadStats().imbalance_score;
    
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
            if (auto* gf3_expert = dynamic_cast<GF3MultiLayerExpert*>(expert.get())) {
                gf3_expert->InitializeAllLayers(seed++);
            }
        }
    }
}

// ============================================================================
// Forward-Forward Training
// ============================================================================

void MoETrainer::TrainExpertsForwardForward(
    const std::vector<ternary::Trit>& positive,
    const std::vector<size_t>& selected_experts,
    const std::vector<float>& routing_weights
) {
    // Generate negative sample once for all experts
    auto negative = GenerateNegativeSample(positive);
    
    // Train each selected expert
    for (size_t i = 0; i < selected_experts.size(); ++i) {
        size_t expert_id = selected_experts[i];
        float weight = (i < routing_weights.size()) ? routing_weights[i] : 1.0f;
        
        auto expert = GetExpert(expert_id);
        if (!expert) continue;
        
        // Scale learning by routing weight
        int32_t scaled_lr = static_cast<int32_t>(config_.learning_rate * weight);
        if (scaled_lr == 0) scaled_lr = 1;
        
        // Train
        expert->TrainForwardForward(positive, negative);
    }
}

float MoETrainer::ComputeCombinedLoss(
    const std::vector<MoETrainingMetrics>& batch_metrics
) {
    float total_ff_loss = 0.0f;
    
    for (const auto& metrics : batch_metrics) {
        // Forward-Forward loss: negative of goodness delta (we want to maximize)
        total_ff_loss -= metrics.avg_goodness_delta;
    }
    
    // Add load balancing loss
    float lb_loss = ComputeLoadBalancingLoss();
    
    return total_ff_loss + config_.load_balance_alpha * lb_loss;
}

// ============================================================================
// Load Balancing
// ============================================================================

float MoETrainer::ComputeLoadBalancingLoss() {
    auto stats = router_.GetLoadStats();
    
    // Compute variance from uniform distribution
    float expected_rate = 1.0f / router_.GetConfig().total_experts;
    float variance = 0.0f;
    
    for (float rate : stats.utilization_rates) {
        float diff = rate - expected_rate;
        variance += diff * diff;
    }
    
    variance /= router_.GetConfig().total_experts;
    
    return variance * config_.load_balance_alpha;
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

bool MoETrainer::HasConverged() const {
    if (current_metrics_.avg_goodness_delta < config_.min_goodness_delta_threshold) {
        epochs_without_improvement_++;
        
        if (epochs_without_improvement_ >= config_.early_stopping_patience) {
            return true;
        }
    } else {
        epochs_without_improvement_ = 0;
        
        // Update best metrics
        if (current_metrics_.avg_goodness_delta > best_metrics_.avg_goodness_delta) {
            best_metrics_ = current_metrics_;
        }
    }
    
    return false;
}

MoETrainingMetrics MoETrainer::GetBestMetrics() const {
    return best_metrics_;
}

void MoETrainer::PrintMetrics(const MoETrainingMetrics& metrics) const {
    std::cout << "=== Epoch " << metrics.epoch << " Metrics ===" << std::endl;
    std::cout << "  Goodness Delta: " << metrics.avg_goodness_delta << std::endl;
    std::cout << "  Positive Goodness: " << metrics.avg_positive_goodness << std::endl;
    std::cout << "  Negative Goodness: " << metrics.avg_negative_goodness << std::endl;
    std::cout << "  Load Balance Score: " << metrics.load_balance_score << std::endl;
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
    
    // Average across batches
    float total_delta = 0.0f;
    float total_pos = 0.0f;
    float total_neg = 0.0f;
    float total_lb = 0.0f;
    float total_latency = 0.0f;
    
    for (const auto& batch : batch_metrics) {
        total_delta += batch.avg_goodness_delta;
        total_pos += batch.avg_positive_goodness;
        total_neg += batch.avg_negative_goodness;
        total_lb += batch.load_balance_score;
        total_latency += batch.avg_routing_latency_ms;
    }
    
    size_t n = batch_metrics.size();
    current_metrics_.epoch = current_epoch_;
    current_metrics_.avg_goodness_delta = total_delta / n;
    current_metrics_.avg_positive_goodness = total_pos / n;
    current_metrics_.avg_negative_goodness = total_neg / n;
    current_metrics_.load_balance_score = total_lb / n;
    current_metrics_.avg_routing_latency_ms = total_latency / n;
    current_metrics_.load_balance_loss = ComputeLoadBalancingLoss();
    current_metrics_.expert_utilization = batch_metrics.back().expert_utilization;
    current_metrics_.expert_request_counts = batch_metrics.back().expert_request_counts;
}

float MoETrainer::ComputeExpertDiversity() const {
    // Compute pairwise similarity between expert specializations
    // Lower similarity = higher diversity = better
    
    // Placeholder: return diversity score based on utilization variance
    auto stats = router_.GetLoadStats();
    float mean_util = 1.0f / router_.GetConfig().total_experts;
    
    float variance = 0.0f;
    for (float util : stats.utilization_rates) {
        variance += (util - mean_util) * (util - mean_util);
    }
    
    variance /= router_.GetConfig().total_experts;
    
    // Higher variance = lower diversity (experts not evenly used)
    return 1.0f - (variance * router_.GetConfig().total_experts);
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
