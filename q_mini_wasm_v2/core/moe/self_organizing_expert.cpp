#include "self_organizing_expert.hpp"
#include <algorithm>
#include <numeric>
#include <cmath>
#include <queue>
#include <random>
#include <iostream>
#include <mutex>

namespace q_mini_wasm_v2::core::moe {

// ====================================================================
// DynamicExpert Implementation
// ====================================================================

DynamicExpert::DynamicExpert(size_t id_, const learning::FFConfig& config)
    : id(id_)
    , generation(0)
    , activation_count(0.0)
    , accumulated_goodness(0.0)
    , local_beta_0(1)
    , local_beta_1(0)
    , last_activation(std::chrono::steady_clock::now())
    , learner(nullptr)
{
    std::cerr << "[DynamicExpert] Constructor start for id=" << id_ << std::endl;
    
    // Safety check for config
    size_t neuron_count = config.neurons_per_layer > 0 ? config.neurons_per_layer : 64;
    std::cerr << "[DynamicExpert] neuron_count=" << neuron_count << std::endl;
    
    // Initialize centroid with valid size
    std::cerr << "[DynamicExpert] Resizing centroid..." << std::endl;
    centroid.resize(neuron_count, 0.0);
    std::cerr << "[DynamicExpert] Centroid resized to " << centroid.size() << std::endl;
    
    // Create learner only if config is valid
    if (config.num_layers > 0 && neuron_count > 0) {
        std::cerr << "[DynamicExpert] Creating ForwardForwardLearner (lazy_init=" << config.lazy_init << ")..." << std::endl;
        learner = std::make_unique<learning::ForwardForwardLearner>(config);
        std::cerr << "[DynamicExpert] ForwardForwardLearner created" << std::endl;
    }
    std::cerr << "[DynamicExpert] Constructor complete" << std::endl;
}

// ====================================================================
// SelfOrganizingExpertManager Implementation
// ====================================================================

SelfOrganizingExpertManager::SelfOrganizingExpertManager(
    const Config& config, 
    const learning::FFConfig& learner_config
) : config_(config), learner_config_(learner_config), next_expert_id_(0), split_count_(0), merge_count_(0), max_buffer_size_(1000) {
    std::cout << "data: {\"status\": \"debug\", \"step\": \"SOM constructor starting, initial_experts=" << config_.initial_experts << "\"}\n\n" << std::flush;
    
    // Create initial experts
    for (size_t i = 0; i < config_.initial_experts; ++i) {
        std::cout << "data: {\"status\": \"debug\", \"step\": \"Creating initial expert " << i << "\"}\n\n" << std::flush;
        size_t id = create_expert(/*parent_generation=*/0);
        if (id == static_cast<size_t>(-1)) {
            std::cout << "data: {\"status\": \"error\", \"step\": \"Failed to create expert " << i << "\"}\n\n" << std::flush;
            break;
        }
    }
    
    std::cout << "data: {\"status\": \"debug\", \"step\": \"Rebuilding centroid cache with " << experts_.size() << " experts\"}\n\n" << std::flush;
    rebuild_centroid_cache();
    
    std::cout << "data: {\"status\": \"debug\", \"step\": \"SOM constructor complete\"}\n\n" << std::flush;
}

size_t SelfOrganizingExpertManager::create_expert(size_t parent_generation) {
    std::cout << "data: {\"status\": \"debug\", \"step\": \"create_expert: checking capacity\"}\n\n" << std::flush;
    
    if (experts_.size() >= config_.max_experts_hard_cap) {
        std::cout << "data: {\"status\": \"debug\", \"step\": \"create_expert: at capacity\"}\n\n" << std::flush;
        return static_cast<size_t>(-1);  // At capacity
    }
    
    std::cout << "data: {\"status\": \"debug\", \"step\": \"create_expert: getting id\"}\n\n" << std::flush;
    size_t id = next_expert_id_++;
    
    std::cout << "data: {\"status\": \"debug\", \"step\": \"create_expert: creating DynamicExpert with id=" << id << "\"}\n\n" << std::flush;
    auto expert = std::make_unique<DynamicExpert>(id, learner_config_);
    
    std::cout << "data: {\"status\": \"debug\", \"step\": \"create_expert: setting generation\"}\n\n" << std::flush;
    expert->generation = parent_generation + 1;
    
    std::cout << "data: {\"status\": \"debug\", \"step\": \"create_expert: centroid size=" << expert->centroid.size() << "\"}\n\n" << std::flush;
    
    // Initialize centroid with small random perturbation for diversity
    std::cout << "data: {\"status\": \"debug\", \"step\": \"create_expert: init rng\"}\n\n" << std::flush;
    std::mt19937 rng(static_cast<unsigned int>(id));
    std::normal_distribution<double> dist(0.0, 0.1);
    
    std::cout << "data: {\"status\": \"debug\", \"step\": \"create_expert: filling centroid\"}\n\n" << std::flush;
    for (size_t i = 0; i < expert->centroid.size(); ++i) {
        expert->centroid[i] = dist(rng);
    }
    
    std::cout << "data: {\"status\": \"debug\", \"step\": \"create_expert: inserting to map\"}\n\n" << std::flush;
    
    // Save values before move
    size_t saved_id = id;
    size_t saved_gen = expert->generation;
    
    experts_[id] = std::move(expert);
    
    // Log expert creation for visibility
    std::cout << "data: {\"status\": \"expert_created\", \"expert_id\": " << saved_id 
              << ", \"generation\": " << saved_gen 
              << ", \"total_experts\": " << experts_.size() << "}\n\n" << std::flush;
    
    return saved_id;
}

std::vector<size_t> SelfOrganizingExpertManager::route_sample(
    const TopologicalSample& sample, 
    size_t top_k
) {
    // Compute distances to all expert centroids (thread-safe)
    std::vector<std::pair<double, size_t>> distances;
    
    {
        std::lock_guard<std::mutex> lock(experts_mutex_);
        distances.reserve(experts_.size());
        
        for (const auto& [id, expert] : experts_) {
            double dist = centroid_distance(sample.embedding, expert->centroid);
            distances.push_back({dist, id});
        }
    }
    
    // Sort by distance (ascending)
    std::partial_sort(
        distances.begin(),
        distances.begin() + std::min(top_k, distances.size()),
        distances.end(),
        [](const auto& a, const auto& b) { return a.first < b.first; }
    );
    
    // Return top-k nearest expert IDs
    std::vector<size_t> result;
    for (size_t i = 0; i < top_k && i < distances.size(); ++i) {
        result.push_back(distances[i].second);
    }
    return result;
}

size_t SelfOrganizingExpertManager::process_sample(
    const std::vector<double>& raw_data,
    const std::vector<ternary::Trit>& discrete_data
) {
    // Create topological sample
    TopologicalSample sample;
    sample.data = discrete_data;
    sample.embedding = compute_sample_embedding(raw_data);
    sample.timestamp = std::chrono::steady_clock::now();
    
    // Route to nearest experts (thread-safe)
    auto expert_ids = route_sample(sample, /*top_k=*/3);  // Route to 3 nearest
    sample.assigned_experts = std::set<size_t>(expert_ids.begin(), expert_ids.end());
    
    // Train each assigned expert (thread-safe)
    size_t activated_count = 0;
    std::lock_guard<std::mutex> lock(experts_mutex_);
    for (size_t expert_id : expert_ids) {
        auto* expert = get_expert(expert_id);
        if (!expert) continue;
        
        // Update expert statistics
        expert->activation_count += 1.0;
        expert->last_activation = std::chrono::steady_clock::now();
        activated_count++;
        
        // Update centroid (EMA: exponential moving average)
        double alpha = 0.01;  // Learning rate for centroid
        for (size_t i = 0; i < expert->centroid.size() && i < sample.embedding.size(); ++i) {
            expert->centroid[i] = (1 - alpha) * expert->centroid[i] + alpha * sample.embedding[i];
        }
        
        // Train the expert's FF learner
        std::vector<std::vector<ternary::Trit>> positive = {discrete_data};
        auto negative = expert->learner->generate_negative_samples(positive);
        
        for (size_t layer = 0; layer < learner_config_.num_layers; ++layer) {
            auto goodness = expert->learner->train_layer(layer, positive, negative);
            expert->accumulated_goodness += goodness.positive_goodness;
        }
    }
    
    // Add to buffer for topology analysis
    sample_buffer_.push_back(std::move(sample));
    
    // Trigger topology update if buffer is full
    if (sample_buffer_.size() >= max_buffer_size_) {
        update_topology();
        flush_old_samples();
    }
    
    return activated_count;
}

void SelfOrganizingExpertManager::update_topology() {
    if (sample_buffer_.empty()) return;
    
    // Compute Betti numbers from recent samples
    float max_edge = 2.0f;  // Normalized embedding space
    auto betti = compute_betti_numbers(sample_buffer_, max_edge);
    
    // Split experts with high β₁ (complex topology)
    std::vector<size_t> experts_to_split;
    for (const auto& [id, expert] : experts_) {
        if (expert->should_split(config_.split_beta1_threshold)) {
            experts_to_split.push_back(id);
        }
    }
    
    for (size_t expert_id : experts_to_split) {
        if (experts_.size() >= config_.max_experts_hard_cap) break;
        
        auto* expert = get_expert(expert_id);
        if (!expert) continue;
        
        // Only split if expert is performing well
        if (expert->average_goodness() >= config_.split_goodness_threshold) {
            split_expert(expert_id);
        }
    }
    
    // Optimize graph density
    optimize_graph_density();
    
    // Rebuild centroid cache
    rebuild_centroid_cache();
}

std::vector<size_t> SelfOrganizingExpertManager::split_expert(size_t expert_id) {
    auto* parent = get_expert(expert_id);
    if (!parent) return {};
    
    // Create 2-4 child experts based on β₁ magnitude
    size_t num_children = std::min(4u, std::max(2u, parent->local_beta_1 / 3));
    std::vector<size_t> child_ids;
    
    // Log split start
    std::cout << "data: {\"status\": \"expert_splitting\", \"parent_id\": " << expert_id
              << ", \"beta_1\": " << parent->local_beta_1
              << ", \"num_children\": " << num_children << "}\n\n" << std::flush;
    
    for (size_t i = 0; i < num_children; ++i) {
        size_t child_id = create_expert(parent->generation);
        if (child_id == static_cast<size_t>(-1)) break;
        
        auto* child = get_expert(child_id);
        
        // Perturb centroid around parent's centroid
        std::mt19937 rng(child_id);
        std::normal_distribution<double> dist(0.0, 0.5);
        for (size_t j = 0; j < child->centroid.size() && j < parent->centroid.size(); ++j) {
            child->centroid[j] = parent->centroid[j] + dist(rng);
        }
        
        // Connect child to parent and siblings
        child->connected_experts.insert(expert_id);
        parent->connected_experts.insert(child_id);
        
        for (size_t sibling_id : child_ids) {
            auto* sibling = get_expert(sibling_id);
            if (sibling) {
                child->connected_experts.insert(sibling_id);
                sibling->connected_experts.insert(child_id);
            }
        }
        
        child_ids.push_back(child_id);
    }
    
    split_count_++;
    
    // Mark parent as "retired" - it stops accepting new samples
    // but keeps its weights for inference
    parent->activation_count = -1.0;  // Negative = retired
    
    // Log split completion
    std::cout << "data: {\"status\": \"expert_split_complete\", \"parent_id\": " << expert_id
              << ", \"children\": [";
    for (size_t i = 0; i < child_ids.size(); ++i) {
        if (i > 0) std::cout << ", ";
        std::cout << child_ids[i];
    }
    std::cout << "], \"total_splits\": " << split_count_ << "}\n\n" << std::flush;
    
    return child_ids;
}

void SelfOrganizingExpertManager::optimize_graph_density() {
    double current_density = get_graph_density();
    
    if (current_density < config_.min_graph_density) {
        add_edges_for_density();
    } else if (current_density > config_.max_graph_density) {
        prune_weak_edges();
    }
}

void SelfOrganizingExpertManager::add_edges_for_density() {
    // Add edges between nearest neighbors
    for (const auto& [id_a, expert_a] : experts_) {
        if (expert_a->activation_count < 0) continue;  // Skip retired
        
        // Find 3 nearest neighbors without edges
        std::vector<std::pair<double, size_t>> candidates;
        for (const auto& [id_b, expert_b] : experts_) {
            if (id_a == id_b) continue;
            if (expert_b->activation_count < 0) continue;
            if (expert_a->connected_experts.count(id_b)) continue;  // Already connected
            
            double dist = centroid_distance(expert_a->centroid, expert_b->centroid);
            candidates.push_back({dist, id_b});
        }
        
        // Sort by distance
        std::partial_sort(
            candidates.begin(),
            candidates.begin() + std::min(size_t(3), candidates.size()),
            candidates.end()
        );
        
        // Add edge to nearest
        if (!candidates.empty()) {
            size_t nearest_id = candidates[0].second;
            expert_a->connected_experts.insert(nearest_id);
            auto* nearest = get_expert(nearest_id);
            if (nearest) {
                nearest->connected_experts.insert(id_a);
            }
        }
    }
}

void SelfOrganizingExpertManager::prune_weak_edges() {
    // Remove edges where centroids have drifted apart
    for (const auto& [id, expert] : experts_) {
        if (expert->activation_count < 0) continue;
        
        std::vector<size_t> to_remove;
        for (size_t neighbor_id : expert->connected_experts) {
            auto* neighbor = get_expert(neighbor_id);
            if (!neighbor || neighbor->activation_count < 0) {
                to_remove.push_back(neighbor_id);
                continue;
            }
            
            double dist = centroid_distance(expert->centroid, neighbor->centroid);
            if (dist > 3.0) {  // Threshold: remove if drifted apart
                to_remove.push_back(neighbor_id);
            }
        }
        
        for (size_t remove_id : to_remove) {
            expert->connected_experts.erase(remove_id);
            auto* neighbor = get_expert(remove_id);
            if (neighbor) {
                neighbor->connected_experts.erase(id);
            }
        }
    }
}

SelfOrganizingExpertManager::BettiNumbers 
SelfOrganizingExpertManager::compute_betti_numbers(
    const std::vector<TopologicalSample>& samples, 
    float max_edge_length
) {
    if (samples.size() < 3) {
        return {1, 0, 0, 0.0f};
    }
    
    // Build Vietoris-Rips complex
    // 0-simplices: samples
    // 1-simplices: edges between samples within max_edge_length
    // 2-simplices: triangles (3 mutually connected samples)
    
    size_t n = samples.size();
    std::vector<std::vector<bool>> adjacency(n, std::vector<bool>(n, false));
    
    // Build adjacency matrix
    size_t num_edges = 0;
    for (size_t i = 0; i < n; ++i) {
        for (size_t j = i + 1; j < n; ++j) {
            double dist = centroid_distance(samples[i].embedding, samples[j].embedding);
            if (dist < max_edge_length) {
                adjacency[i][j] = adjacency[j][i] = true;
                num_edges++;
            }
        }
    }
    
    // Count triangles (2-simplices)
    size_t num_triangles = 0;
    for (size_t i = 0; i < n; ++i) {
        for (size_t j = i + 1; j < n; ++j) {
            if (!adjacency[i][j]) continue;
            for (size_t k = j + 1; k < n; ++k) {
                if (adjacency[i][k] && adjacency[j][k]) {
                    num_triangles++;
                }
            }
        }
    }
    
    // Compute connected components (β₀) using BFS
    std::vector<bool> visited(n, false);
    uint32_t beta_0 = 0;
    
    for (size_t start = 0; start < n; ++start) {
        if (visited[start]) continue;
        
        beta_0++;
        std::queue<size_t> q;
        q.push(start);
        visited[start] = true;
        
        while (!q.empty()) {
            size_t u = q.front(); q.pop();
            for (size_t v = 0; v < n; ++v) {
                if (adjacency[u][v] && !visited[v]) {
                    visited[v] = true;
                    q.push(v);
                }
            }
        }
    }
    
    // Estimate β₁ using Euler characteristic: χ = V - E + F = β₀ - β₁ + β₂
    // For planar graphs: β₁ = E - V + 1 (assuming connected)
    int32_t euler_char = static_cast<int32_t>(n) - static_cast<int32_t>(num_edges) + static_cast<int32_t>(num_triangles);
    uint32_t beta_2 = 0;  // Conservative estimate
    uint32_t beta_1 = (beta_0 > 0) ? (num_edges - num_edges/3 - n + beta_0) : 0;
    
    // Alternative: use rank-nullity on boundary operators (simplified)
    // β₁ = |E| - rank(∂₁) + rank(∂₂)
    // For this approximation, use cycle count heuristic
    if (beta_1 > num_edges) beta_1 = num_edges / 2;
    
    // Graph density
    size_t max_edges = n * (n - 1) / 2;
    float density = max_edges > 0 ? static_cast<float>(num_edges) / max_edges : 0.0f;
    
    return {beta_0, beta_1, beta_2, density};
}

// Helper methods

double SelfOrganizingExpertManager::centroid_distance(
    const std::vector<double>& a, 
    const std::vector<double>& b
) {
    if (a.size() != b.size()) return std::numeric_limits<double>::infinity();
    
    double sum_sq = 0.0;
    for (size_t i = 0; i < a.size(); ++i) {
        double diff = a[i] - b[i];
        sum_sq += diff * diff;
    }
    return std::sqrt(sum_sq);
}

std::vector<double> SelfOrganizingExpertManager::compute_sample_embedding(
    const std::vector<double>& raw_data
) {
    // Simple embedding: normalize and pad/truncate to learner config size
    std::vector<double> embedding = raw_data;
    
    // Normalize
    double norm = 0.0;
    for (double x : embedding) {
        norm += x * x;
    }
    norm = std::sqrt(norm);
    if (norm > 0) {
        for (double& x : embedding) {
            x /= norm;
        }
    }
    
    // Pad or truncate to match centroid dimension
    size_t target_dim = learner_config_.neurons_per_layer;
    if (embedding.size() < target_dim) {
        embedding.resize(target_dim, 0.0);
    } else if (embedding.size() > target_dim) {
        embedding.resize(target_dim);
    }
    
    return embedding;
}

void SelfOrganizingExpertManager::flush_old_samples() {
    // Keep most recent half of buffer
    size_t keep = max_buffer_size_ / 2;
    if (sample_buffer_.size() > keep) {
        sample_buffer_.erase(sample_buffer_.begin(), sample_buffer_.end() - keep);
    }
}

void SelfOrganizingExpertManager::rebuild_centroid_cache() {
    centroids_cache_.clear();
    for (const auto& [id, expert] : experts_) {
        if (expert->activation_count >= 0) {  // Skip retired
            centroids_cache_.push_back({id, expert->centroid});
        }
    }
}

DynamicExpert* SelfOrganizingExpertManager::get_expert(size_t id) {
    auto it = experts_.find(id);
    return (it != experts_.end()) ? it->second.get() : nullptr;
}

size_t SelfOrganizingExpertManager::get_edge_count() const {
    size_t count = 0;
    for (const auto& [id, expert] : experts_) {
        count += expert->connected_experts.size();
    }
    return count / 2;  // Each edge counted twice
}

double SelfOrganizingExpertManager::get_graph_density() const {
    size_t n = get_expert_count();
    if (n < 2) return 0.0;
    
    size_t max_edges = n * (n - 1) / 2;
    if (max_edges == 0) return 0.0;
    
    return static_cast<double>(get_edge_count()) / max_edges;
}

SelfOrganizingExpertManager::TopologyStats 
SelfOrganizingExpertManager::get_stats() const {
    TopologyStats stats;
    stats.num_experts = get_expert_count();
    stats.num_edges = get_edge_count();
    stats.graph_density = get_graph_density();
    stats.split_count_total = split_count_;
    stats.merge_count_total = merge_count_;
    
    // Compute average β₁
    double sum_beta_1 = 0.0;
    stats.max_beta_1 = 0;
    stats.generations_max = 0;
    
    for (const auto& [id, expert] : experts_) {
        sum_beta_1 += expert->local_beta_1;
        stats.max_beta_1 = std::max(stats.max_beta_1, expert->local_beta_1);
        stats.generations_max = std::max(stats.generations_max, expert->generation);
    }
    
    stats.avg_beta_1 = stats.num_experts > 0 ? sum_beta_1 / stats.num_experts : 0.0;
    
    return stats;
}

} // namespace q_mini_wasm_v2::core::moe
