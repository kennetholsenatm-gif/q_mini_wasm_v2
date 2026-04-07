#include "unified_router.hpp"
#include "../qgnn/graph_native.hpp"
#include <algorithm>
#include <numeric>
#include <cmath>
#include <random>
#include <chrono>

namespace q_mini_wasm_v2::core::moe {

// ============================================================================
// Constructor / Destructor
// ============================================================================

UnifiedMoERouter::UnifiedMoERouter(const UnifiedMoEConfig& config)
    : config_(config)
    , load_stats_{}
    , stats_{}
{
    if (!config_.validate()) {
        throw std::invalid_argument("Invalid UnifiedMoEConfig provided");
    }
    
    // Initialize expert storage
    experts_.resize(config_.total_experts);
    specializations_.resize(config_.total_experts);
    expert_request_counts_.resize(config_.total_experts, 0);
    
    // Initialize specializations with random ternary values
    std::mt19937 rng(42);  // Deterministic seed
    std::uniform_int_distribution<int> ternary_dist(-1, 1);
    
    for (size_t i = 0; i < config_.total_experts; ++i) {
        specializations_[i].resize(config_.specialization_dim);
        for (auto& val : specializations_[i]) {
            val = static_cast<ternary::Trit>(ternary_dist(rng));
        }
    }
    
    // Build hierarchical clusters for large scale
    if (config_.topology == UnifiedMoEConfig::TopologyType::HIERARCHICAL ||
        config_.use_hierarchical_selection) {
        BuildClusters();
    }
    
    // Initialize entanglement topology
    InitializeEntanglement();
    
    // Initialize statistics
    stats_ = RouterStats{};
}

UnifiedMoERouter::~UnifiedMoERouter() = default;

// ============================================================================
// Core Routing
// ============================================================================

UnifiedMoERouter::RoutingResult UnifiedMoERouter::Route(
    const std::vector<ternary::Trit>& input
) {
    auto start = std::chrono::high_resolution_clock::now();
    
    RoutingResult result;
    
    // Select routing strategy based on scale
    if (config_.use_hierarchical_selection && config_.total_experts > 128) {
        result = HierarchicalRoute(input);
        result.used_hierarchical = true;
    } else {
        // Direct tropical routing
        auto logits = ComputeTropicalLogits(input);
        
        // Convert to float for Top-K
        std::vector<float> float_logits(logits.size());
        for (size_t i = 0; i < logits.size(); ++i) {
            float_logits[i] = static_cast<float>(logits[i]);
        }
        
        // Apply load balancing if enabled
        if (config_.enable_load_balancing) {
            auto stats = GetLoadStats();
            float_logits = ApplyLoadBalancing(float_logits, stats);
        }
        
        // Select Top-K
        result.selected_experts = SelectTopK(float_logits, config_.active_experts);
        result.routing_weights = std::vector<float>(config_.active_experts);
        
        // Compute weights from logits
        for (size_t i = 0; i < result.selected_experts.size(); ++i) {
            size_t expert = result.selected_experts[i];
            result.routing_weights[i] = std::exp(float_logits[expert] / 10.0f);  // Softmax-like
        }
        
        // Normalize weights
        float sum = std::accumulate(result.routing_weights.begin(), 
                                     result.routing_weights.end(), 0.0f);
        if (sum > 0) {
            for (auto& w : result.routing_weights) {
                w /= sum;
            }
        }
        
        result.used_hierarchical = false;
    }
    
    // Update statistics
    UpdateLoadStats(result.selected_experts);
    
    // Compute energy (simplified model)
    result.energy_consumed = static_cast<ternary::EnergyTrit>(
        1 + result.selected_experts.size() / 8
    );
    
    // Compute latency
    auto end = std::chrono::high_resolution_clock::now();
    result.latency_us = std::chrono::duration_cast<std::chrono::microseconds>(
        end - start
    ).count();
    
    // Compute load balance score
    auto stats = GetLoadStats();
    result.load_balance_score = stats.imbalance_score;
    
    // Update global stats
    stats_.total_routings++;
    stats_.total_tokens_routed += 1;
    
    return result;
}

std::vector<UnifiedMoERouter::RoutingResult> UnifiedMoERouter::RouteBatch(
    const std::vector<std::vector<ternary::Trit>>& inputs
) {
    std::vector<RoutingResult> results;
    results.reserve(inputs.size());
    
    for (const auto& input : inputs) {
        results.push_back(Route(input));
    }
    
    return results;
}

// ============================================================================
// Hierarchical Routing (for 243-expert scale)
// ============================================================================

UnifiedMoERouter::RoutingResult UnifiedMoERouter::HierarchicalRoute(
    const std::vector<ternary::Trit>& input
) {
    RoutingResult result;
    
    // Level 1: Select clusters
    std::vector<float> cluster_scores(clusters_.size(), 0.0f);
    
    for (size_t c = 0; c < clusters_.size(); ++c) {
        // Compute similarity to cluster centroid using tropical inner product
        int32_t tropical_sim = 0;
        size_t min_dim = std::min(input.size(), clusters_[c].centroid.size());
        
        for (size_t i = 0; i < min_dim; ++i) {
            tropical_sim += static_cast<int32_t>(input[i]) * 
                          static_cast<int32_t>(clusters_[c].centroid[i]);
        }
        
        cluster_scores[c] = static_cast<float>(tropical_sim);
    }
    
    // Select top clusters (typically 2-4 for 243 experts)
    size_t num_clusters_select = std::max(size_t(2), config_.active_experts / 4);
    std::vector<size_t> selected_clusters = SelectTopK(cluster_scores, num_clusters_select);
    
    // Level 2: Route to experts within selected clusters
    std::vector<std::pair<float, size_t>> expert_scores;
    
    for (size_t cluster_idx : selected_clusters) {
        for (size_t expert_id : clusters_[cluster_idx].expert_ids) {
            // Compute routing score for this expert
            float score = 0.0f;
            size_t min_dim = std::min(input.size(), specializations_[expert_id].size());
            
            for (size_t i = 0; i < min_dim; ++i) {
                score += static_cast<float>(
                    static_cast<int32_t>(input[i]) * 
                    static_cast<int32_t>(specializations_[expert_id][i])
                );
            }
            
            // Add load balancing penalty
            if (config_.enable_load_balancing) {
                float load_penalty = static_cast<float>(expert_request_counts_[expert_id]) * 0.01f;
                score -= load_penalty;
            }
            
            expert_scores.emplace_back(score, expert_id);
        }
    }
    
    // Sort by score and select top-K
    std::sort(expert_scores.begin(), expert_scores.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });
    
    size_t k = std::min(config_.active_experts, expert_scores.size());
    result.selected_experts.reserve(k);
    result.routing_weights.reserve(k);
    
    for (size_t i = 0; i < k; ++i) {
        result.selected_experts.push_back(expert_scores[i].second);
        result.routing_weights.push_back(std::exp(expert_scores[i].first / 10.0f));
    }
    
    // Normalize weights
    float sum = std::accumulate(result.routing_weights.begin(), 
                                 result.routing_weights.end(), 0.0f);
    if (sum > 0) {
        for (auto& w : result.routing_weights) {
            w /= sum;
        }
    }
    
    return result;
}

void UnifiedMoERouter::BuildClusters() {
    clusters_.clear();
    
    size_t experts_per_cluster = config_.cluster_size;
    size_t num_clusters = (config_.total_experts + experts_per_cluster - 1) / experts_per_cluster;
    
    clusters_.reserve(num_clusters);
    
    for (size_t c = 0; c < num_clusters; ++c) {
        Cluster cluster;
        cluster.cluster_id = c;
        
        size_t start_idx = c * experts_per_cluster;
        size_t end_idx = std::min(start_idx + experts_per_cluster, config_.total_experts);
        
        for (size_t i = start_idx; i < end_idx; ++i) {
            cluster.expert_ids.push_back(i);
        }
        
        // Initialize centroid as average of member specializations
        cluster.centroid.resize(config_.specialization_dim, ternary::Trit::ZERO);
        
        for (size_t expert_id : cluster.expert_ids) {
            for (size_t j = 0; j < config_.specialization_dim; ++j) {
                cluster.centroid[j] = static_cast<ternary::Trit>(
                    static_cast<int8_t>(cluster.centroid[j]) +
                    static_cast<int8_t>(specializations_[expert_id][j])
                );
            }
        }
        
        // Average (divide by count)
        for (auto& val : cluster.centroid) {
            val = static_cast<ternary::Trit>(
                static_cast<int8_t>(val) / static_cast<int8_t>(cluster.expert_ids.size())
            );
        }
        
        clusters_.push_back(std::move(cluster));
    }
}

// ============================================================================
// Tropical Geometry
// ============================================================================

std::vector<int32_t> UnifiedMoERouter::ComputeTropicalLogits(
    const std::vector<ternary::Trit>& input
) {
    std::vector<int32_t> logits(config_.total_experts, 0);
    
    for (size_t e = 0; e < config_.total_experts; ++e) {
        // Tropical inner product with expert specialization
        int32_t score = 0;
        size_t min_dim = std::min(input.size(), specializations_[e].size());
        
        for (size_t i = 0; i < min_dim; ++i) {
            score += static_cast<int32_t>(input[i]) * 
                     static_cast<int32_t>(specializations_[e][i]);
        }
        
        logits[e] = score;
    }
    
    return logits;
}

int32_t UnifiedMoERouter::TropicalInnerProduct(
    const std::vector<int32_t>& a,
    const std::vector<int32_t>& b
) {
    if (a.size() != b.size()) {
        throw std::invalid_argument("Vector sizes must match");
    }
    
    int32_t result = -2147483647;  // Min int
    
    for (size_t i = 0; i < a.size(); ++i) {
        int32_t product = TropicalMultiply(a[i], b[i]);
        result = TropicalAdd(result, product);
    }
    
    return result == -2147483647 ? 0 : result;
}

// ============================================================================
// Entanglement Topologies
// ============================================================================

void UnifiedMoERouter::InitializeEntanglement() {
    switch (config_.topology) {
        case UnifiedMoEConfig::TopologyType::RING:
            CreateRingTopology();
            break;
        case UnifiedMoEConfig::TopologyType::SMALL_WORLD:
            CreateSmallWorldTopology(config_.small_world_rewiring_prob, config_.small_world_k);
            break;
        case UnifiedMoEConfig::TopologyType::HIERARCHICAL:
            CreateHierarchicalTopology(config_.cluster_size);
            break;
        default:
            CreateRingTopology();
    }
}

void UnifiedMoERouter::CreateRingTopology() {
    entanglement_edges_.clear();
    entanglement_edges_.reserve(config_.total_experts * 2);
    
    for (size_t i = 0; i < config_.total_experts; ++i) {
        size_t next = (i + 1) % config_.total_experts;
        
        // Bidirectional ring
        entanglement_edges_.push_back({i, next, 1.0f});
        entanglement_edges_.push_back({next, i, 1.0f});
    }
}

void UnifiedMoERouter::CreateSmallWorldTopology(double rewiring_prob, size_t k) {
    entanglement_edges_.clear();
    
    // Start with regular ring lattice (k/2 neighbors on each side)
    for (size_t i = 0; i < config_.total_experts; ++i) {
        for (size_t j = 1; j <= k / 2; ++j) {
            size_t neighbor = (i + j) % config_.total_experts;
            entanglement_edges_.push_back({i, neighbor, 1.0f});
        }
    }
    
    // Rewire with probability p
    std::mt19937 rng(42);
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    std::uniform_int_distribution<size_t> node_dist(0, config_.total_experts - 1);
    
    for (auto& edge : entanglement_edges_) {
        if (dist(rng) < rewiring_prob) {
            // Rewire: change target to random node
            size_t new_target = node_dist(rng);
            if (new_target != edge.from) {
                edge.to = new_target;
                edge.strength = 0.7f;  // Weaker for long-range connections
            }
        }
    }
}

void UnifiedMoERouter::CreateHierarchicalTopology(size_t cluster_size) {
    entanglement_edges_.clear();
    
    // Ensure clusters exist
    if (clusters_.empty()) {
        BuildClusters();
    }
    
    // Intra-cluster: dense connections
    for (const auto& cluster : clusters_) {
        for (size_t i = 0; i < cluster.expert_ids.size(); ++i) {
            for (size_t j = i + 1; j < cluster.expert_ids.size(); ++j) {
                entanglement_edges_.push_back({
                    cluster.expert_ids[i],
                    cluster.expert_ids[j],
                    1.0f  // Strong intra-cluster
                });
            }
        }
    }
    
    // Inter-cluster: ring between cluster centroids
    for (size_t c = 0; c < clusters_.size(); ++c) {
        size_t next_c = (c + 1) % clusters_.size();
        
        // Connect first expert of each cluster
        if (!clusters_[c].expert_ids.empty() && !clusters_[next_c].expert_ids.empty()) {
            entanglement_edges_.push_back({
                clusters_[c].expert_ids[0],
                clusters_[next_c].expert_ids[0],
                0.5f  // Weaker inter-cluster
            });
        }
    }
}

// ============================================================================
// Load Balancing (Fixed-Point Versions)
// ============================================================================

int32_t UnifiedMoERouter::ComputeLoadBalanceLossFixed(const LoadStats& stats) {
    if (stats.total_requests == 0) {
        return 0;
    }
    
    // Fixed-point: expected_rate = 1000 / total_experts (scale 1000)
    int32_t expected_rate_fixed = 1000 / static_cast<int32_t>(config_.total_experts);
    int32_t loss = 0;
    
    for (float rate : stats.utilization_rates) {
        // Convert rate to fixed-point (rate is already 0-1, so scale by 1000)
        int32_t rate_fixed = static_cast<int32_t>(rate * 1000);
        int32_t diff = rate_fixed - expected_rate_fixed;
        // Accumulate squared difference, divide by 1000 to prevent overflow
        loss += (diff * diff) / 1000;
    }
    
    // Apply load_balance_alpha (assume it's already in appropriate scale)
    return loss * config_.load_balance_alpha / 1000;
}

std::vector<int32_t> UnifiedMoERouter::ApplyLoadBalancingFixed(
    const std::vector<int32_t>& logits,
    const LoadStats& stats
) {
    std::vector<int32_t> balanced = logits;
    
    for (size_t i = 0; i < balanced.size(); ++i) {
        // Penalize over-utilized experts
        // Fixed-point: expected_rate = 1000 / total_experts
        int32_t expected_rate_fixed = 1000 / static_cast<int32_t>(config_.total_experts);
        int32_t rate_fixed = static_cast<int32_t>(stats.utilization_rates[i] * 1000);
        int32_t penalty = (rate_fixed - expected_rate_fixed) * 10; // 10.0f in fixed-point
        balanced[i] -= penalty;
    }
    
    return balanced;
}

std::vector<size_t> UnifiedMoERouter::LLEPRouteFixed(
    const std::vector<int32_t>& logits,
    size_t k
) {
    // Least-Loaded Expert Parallelism - fixed-point version
    std::vector<std::pair<int32_t, size_t>> scored;
    scored.reserve(config_.total_experts);
    
    for (size_t i = 0; i < config_.total_experts; ++i) {
        // Score = logit - load_penalty (fixed-point: 0.01f = 10/1000)
        int32_t load_penalty = static_cast<int32_t>(expert_request_counts_[i]) * 10 / 1000;
        scored.emplace_back(logits[i] - load_penalty, i);
    }
    
    std::sort(scored.begin(), scored.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });
    
    std::vector<size_t> selected;
    selected.reserve(k);
    
    for (size_t i = 0; i < k && i < scored.size(); ++i) {
        selected.push_back(scored[i].second);
    }
    
    return selected;
}

std::vector<size_t> UnifiedMoERouter::SelectTopK(const std::vector<int32_t>& logits_fixed, size_t k) {
    // Fixed-point version of SelectTopK
    std::vector<std::pair<int32_t, size_t>> indexed;
    indexed.reserve(logits_fixed.size());
    
    for (size_t i = 0; i < logits_fixed.size(); ++i) {
        indexed.emplace_back(logits_fixed[i], i);
    }
    
    // Partial sort to find top k
    if (k < indexed.size()) {
        std::partial_sort(indexed.begin(), indexed.begin() + k, indexed.end(),
            [](const auto& a, const auto& b) { return a.first > b.first; });
    } else {
        std::sort(indexed.begin(), indexed.end(),
            [](const auto& a, const auto& b) { return a.first > b.first; });
        k = indexed.size();
    }
    
    std::vector<size_t> result;
    result.reserve(k);
    for (size_t i = 0; i < k; ++i) {
        result.push_back(indexed[i].second);
    }
    
    return result;
}

float UnifiedMoERouter::ComputeLoadBalanceLoss(const LoadStats& stats) {
    if (stats.total_requests == 0) {
        return 0.0f;
    }
    
    float expected_rate = 1.0f / config_.total_experts;
    float loss = 0.0f;
    
    for (float rate : stats.utilization_rates) {
        float diff = rate - expected_rate;
        loss += diff * diff;
    }
    
    return loss * config_.load_balance_alpha;
}

std::vector<float> UnifiedMoERouter::ApplyLoadBalancing(
    const std::vector<float>& logits,
    const LoadStats& stats
) {
    std::vector<float> balanced = logits;
    
    for (size_t i = 0; i < balanced.size(); ++i) {
        // Penalize over-utilized experts
        float penalty = (stats.utilization_rates[i] - 1.0f / config_.total_experts) * 10.0f;
        balanced[i] -= penalty;
    }
    
    return balanced;
}

std::vector<size_t> UnifiedMoERouter::LLEPRoute(
    const std::vector<float>& logits,
    size_t k
) {
    // Least-Loaded Expert Parallelism
    std::vector<std::pair<float, size_t>> scored;
    scored.reserve(config_.total_experts);
    
    for (size_t i = 0; i < config_.total_experts; ++i) {
        // Score = logit - load_penalty
        float load_penalty = static_cast<float>(expert_request_counts_[i]) * 0.01f;
        scored.emplace_back(logits[i] - load_penalty, i);
    }
    
    std::sort(scored.begin(), scored.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });
    
    std::vector<size_t> selected;
    selected.reserve(k);
    
    for (size_t i = 0; i < k && i < scored.size(); ++i) {
        selected.push_back(scored[i].second);
    }
    
    return selected;
}

void UnifiedMoERouter::UpdateLoadStats(const std::vector<size_t>& selected_experts) {
    for (size_t expert : selected_experts) {
        expert_request_counts_[expert]++;
    }
    
    load_stats_.total_requests++;
    load_stats_.request_counts = std::vector<size_t>(
        expert_request_counts_.begin(), expert_request_counts_.end()
    );
    
    // Compute utilization rates
    load_stats_.utilization_rates.resize(config_.total_experts);
    for (size_t i = 0; i < config_.total_experts; ++i) {
        load_stats_.utilization_rates[i] = 
            static_cast<float>(expert_request_counts_[i]) / load_stats_.total_requests;
    }
    
    // Compute imbalance score (std dev of utilization)
    float mean = 1.0f / config_.total_experts;
    float variance = 0.0f;
    for (float rate : load_stats_.utilization_rates) {
        variance += (rate - mean) * (rate - mean);
    }
    variance /= config_.total_experts;
    load_stats_.imbalance_score = std::sqrt(variance);
}

UnifiedMoERouter::LoadStats UnifiedMoERouter::GetLoadStats() const {
    return load_stats_;
}

void UnifiedMoERouter::RebalanceLoads() {
    // Reset counters to allow natural rebalancing
    for (auto& count : expert_request_counts_) {
        count = count / 2;  // Decay by half
    }
}

std::vector<size_t> UnifiedMoERouter::HierarchicalSelect(
    const std::vector<ternary::Trit>& input,
    size_t k
) {
    if (clusters_.empty()) {
        BuildClusters();
    }
    
    // Level 1: Select best cluster using tropical inner product with centroids
    std::vector<int32_t> cluster_scores(clusters_.size(), 0);
    
    for (size_t c = 0; c < clusters_.size(); ++c) {
        // Compute tropical inner product between input and cluster centroid
        int32_t score = TropicalInnerProduct(
            std::vector<int32_t>(input.begin(), input.end()),
            std::vector<int32_t>(clusters_[c].centroid.begin(), clusters_[c].centroid.end())
        );
        cluster_scores[c] = score;
    }
    
    // Select top cluster
    size_t best_cluster = 0;
    int32_t best_score = cluster_scores[0];
    for (size_t c = 1; c < clusters_.size(); ++c) {
        if (cluster_scores[c] > best_score) {
            best_score = cluster_scores[c];
            best_cluster = c;
        }
    }
    
    // Level 2: Select top-K experts within best cluster using entanglement-aware scoring
    const auto& cluster = clusters_[best_cluster];
    std::vector<std::pair<float, size_t>> expert_scores;
    
    for (size_t expert_id : cluster.expert_ids) {
        // Base score from tropical logits
        int32_t base_score = 0;
        size_t min_dim = std::min(input.size(), specializations_[expert_id].size());
        for (size_t i = 0; i < min_dim; ++i) {
            base_score += static_cast<int32_t>(input[i]) * 
                         static_cast<int32_t>(specializations_[expert_id][i]);
        }
        
        // Add entanglement bonus from entanglement topology
        float entanglement_bonus = 0.0f;
        for (const auto& edge : entanglement_edges_) {
            if (edge.from == expert_id || edge.to == expert_id) {
                entanglement_bonus += edge.strength * 0.1f; // Small boost from entangled neighbors
            }
        }
        
        // Performance-optimized: penalize heavily loaded experts
        float load_penalty = static_cast<float>(expert_request_counts_[expert_id]) * 0.01f;
        
        float final_score = static_cast<float>(base_score) + entanglement_bonus - load_penalty;
        expert_scores.emplace_back(final_score, expert_id);
    }
    
    // Sort by score and select top-K
    std::sort(expert_scores.begin(), expert_scores.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });
    
    std::vector<size_t> selected;
    selected.reserve(k);
    
    for (size_t i = 0; i < k && i < expert_scores.size(); ++i) {
        selected.push_back(expert_scores[i].second);
    }
    
    return selected;
}

void UnifiedMoERouter::RecomputeClusterCentroids() {
    for (auto& cluster : clusters_) {
        // Reset centroid
        cluster.centroid.assign(config_.specialization_dim, ternary::Trit::ZERO);
        
        // Sum all member specializations
        for (size_t expert_id : cluster.expert_ids) {
            for (size_t j = 0; j < config_.specialization_dim; ++j) {
                cluster.centroid[j] = static_cast<ternary::Trit>(
                    static_cast<int8_t>(cluster.centroid[j]) +
                    static_cast<int8_t>(specializations_[expert_id][j])
                );
            }
        }
        
        // Average by dividing by member count
        if (!cluster.expert_ids.empty()) {
            for (auto& val : cluster.centroid) {
                val = static_cast<ternary::Trit>(
                    static_cast<int8_t>(val) / static_cast<int8_t>(cluster.expert_ids.size())
                );
            }
        }
    }
}

// ============================================================================
// Dynamic Expert Scaling
// ============================================================================

size_t UnifiedMoERouter::AdjustExpertScale(uint32_t current_load) {
    // Dynamic scaling based on system load (0-100)
    const size_t MIN_EXPERTS = std::max(size_t(1), config_.active_experts / 2);
    const size_t MAX_EXPERTS = std::min(config_.total_experts, config_.active_experts * 2);
    
    size_t target_experts = config_.active_experts;
    
    if (current_load > 80) {
        // High load: scale up
        target_experts = std::min(config_.active_experts + 4, MAX_EXPERTS);
    } else if (current_load > 60) {
        // Medium-high load: moderate scale up
        target_experts = std::min(config_.active_experts + 2, MAX_EXPERTS);
    } else if (current_load < 20) {
        // Low load: scale down for efficiency
        target_experts = std::max(config_.active_experts - 2, MIN_EXPERTS);
    }
    
    config_.active_experts = target_experts;
    return target_experts;
}

// ============================================================================
// Utility Methods
// ============================================================================

std::vector<size_t> UnifiedMoERouter::SelectTopK(const std::vector<float>& logits, size_t k) {
    // Use partial sort for efficiency with large N
    std::vector<std::pair<float, size_t>> indexed;
    indexed.reserve(logits.size());
    
    for (size_t i = 0; i < logits.size(); ++i) {
        indexed.emplace_back(logits[i], i);
    }
    
    // Partial sort to find top k
    if (k < indexed.size()) {
        std::partial_sort(indexed.begin(), indexed.begin() + k, indexed.end(),
            [](const auto& a, const auto& b) { return a.first > b.first; });
    } else {
        std::sort(indexed.begin(), indexed.end(),
            [](const auto& a, const auto& b) { return a.first > b.first; });
        k = indexed.size();
    }
    
    std::vector<size_t> result;
    result.reserve(k);
    for (size_t i = 0; i < k; ++i) {
        result.push_back(indexed[i].second);
    }
    
    return result;
}

bool UnifiedMoERouter::Validate243Config() const {
    bool valid = config_.total_experts == 243;
    valid &= config_.use_hierarchical_selection;
    valid &= config_.topology == UnifiedMoEConfig::TopologyType::HIERARCHICAL ||
             config_.topology == UnifiedMoEConfig::TopologyType::SMALL_WORLD;
    valid &= config_.active_experts <= 32;  // Reasonable K for 243 experts
    return valid;
}

size_t UnifiedMoERouter::EstimateMemoryUsage() const {
    // Rough estimation
    size_t bytes = 0;
    
    // Experts and specializations
    bytes += config_.total_experts * config_.specialization_dim * sizeof(ternary::Trit);
    
    // Entanglement edges (sparse)
    bytes += entanglement_edges_.size() * sizeof(EntanglementEdge);
    
    // Clusters
    bytes += clusters_.size() * config_.specialization_dim * sizeof(ternary::Trit);
    
    return bytes;
}

UnifiedMoERouter::RouterStats UnifiedMoERouter::GetStats() const {
    return stats_;
}

void UnifiedMoERouter::ResetStats() {
    stats_ = RouterStats{};
}

// ============================================================================
// Factory Functions
// ============================================================================

std::unique_ptr<UnifiedMoERouter> CreateUnifiedRouter(const UnifiedMoEConfig& config) {
    return std::make_unique<UnifiedMoERouter>(config);
}

std::unique_ptr<UnifiedMoERouter> Create243ExpertRouter() {
    auto config = UnifiedMoEConfig::LargeScale();
    return std::make_unique<UnifiedMoERouter>(config);
}

} // namespace q_mini_wasm_v2::core::moe
