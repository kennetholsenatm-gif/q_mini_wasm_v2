#include "self_organizing_expert.hpp"
#include "../gf3/tropical.hpp"
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
    , activation_count_fixed(0)           // GF(3): Integer count
    , accumulated_goodness_fixed(0)      // GF(3): Tropical goodness
    , local_beta_0(1)
    , local_beta_1(0)
    , last_activation(std::chrono::steady_clock::now())
    , learner(nullptr)
    , learner_config_(config)  // Store config for lazy init
    , learner_initialized_(false)
{
    std::cerr << "[DynamicExpert] Constructor start for id=" << id_ << std::endl;
    
    // Safety check for config
    size_t neuron_count = config.neurons_per_layer > 0 ? config.neurons_per_layer : 64;
    std::cerr << "[DynamicExpert] neuron_count=" << neuron_count << std::endl;
    
    // Initialize centroid with valid size (lightweight) - GF(3): use integer 0
    std::cerr << "[DynamicExpert] Resizing centroid..." << std::endl;
    centroid_fixed.resize(neuron_count, 0);  // GF(3): Integer zero
    std::cerr << "[DynamicExpert] Centroid resized to " << centroid_fixed.size() << std::endl;
    
    // LAZY: Don't create learner here - only when first used
    // This prevents 8192 experts x 64 layers initialization hang
    std::cerr << "[DynamicExpert] Constructor complete - learner will be created lazily" << std::endl;
}

void DynamicExpert::ensure_learner_initialized() {
    if (!learner_initialized_ && !learner) {
        if (learner_config_.num_layers > 0 && learner_config_.neurons_per_layer > 0) {
            std::cerr << "[DynamicExpert] Lazy initializing ForwardForwardLearner for id=" << id << std::endl;
            learner = std::make_unique<learning::ForwardForwardLearner>(learner_config_);
            std::cerr << "[DynamicExpert] ForwardForwardLearner created lazily" << std::endl;
        }
        learner_initialized_ = true;
    }
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
    
    std::cout << "data: {\"status\": \"debug\", \"step\": \"create_expert: centroid size=" << expert->centroid_fixed.size() << "\"}\n\n" << std::flush;
    
    // Initialize centroid with GF(3) ternary perturbation for diversity
    std::cout << "data: {\"status\": \"debug\", \"step\": \"create_expert: init trits\"}\n\n" << std::flush;
    
    // GF(3): Use discrete ternary distribution instead of float normal_distribution
    // Distribution: 40% -1, 20% 0, 40% +1 (balanced ternary)
    std::mt19937 rng(static_cast<unsigned int>(id));
    std::discrete_distribution<int> trit_dist({40, 20, 40}); // Maps to 0=-1, 1=0, 2=+1
    
    std::cout << "data: {\"status\": \"debug\", \"step\": \"create_expert: filling centroid\"}\n\n" << std::flush;
    for (size_t i = 0; i < expert->centroid_fixed.size(); ++i) {
        // Convert discrete output to Trit: 0->-1, 1->0, 2->+1
        expert->centroid_fixed[i] = static_cast<int8_t>(trit_dist(rng)) - 1;
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
    // Compute distances to all expert centroids using GF(3) tropical distance
    std::vector<std::pair<int64_t, size_t>> distances;  // GF(3): Integer distance
    
    {
        std::lock_guard<std::mutex> lock(experts_mutex_);
        distances.reserve(experts_.size());
        
        for (const auto& [id, expert] : experts_) {
            int64_t dist = gf3::tropical_manhattan_distance(sample.embedding_fixed, expert->centroid_fixed);
            distances.push_back({dist, id});
        }
    }
    
    // Sort by distance (ascending) - tropical distance is already integer
    // MSVC debug STL asserts when middle == last in partial_sort.
    size_t k = std::min(top_k, distances.size());
    if (k >= distances.size()) {
        std::sort(
            distances.begin(),
            distances.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; }
        );
    } else {
        std::partial_sort(
            distances.begin(),
            distances.begin() + k,
            distances.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; }
        );
    }
    
    // Return top-k nearest expert IDs
    std::vector<size_t> result;
    for (size_t i = 0; i < top_k && i < distances.size(); ++i) {
        result.push_back(distances[i].second);
    }
    return result;
}

size_t SelfOrganizingExpertManager::process_sample(
    const std::vector<int32_t>& raw_data_fixed,
    const std::vector<ternary::Trit>& discrete_data
) {
    // Create topological sample
    TopologicalSample sample;
    sample.data = discrete_data;
    sample.embedding_fixed = compute_sample_embedding_fixed(raw_data_fixed);
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
        expert->activation_count_fixed++;  // GF(3): Integer increment
        expert->last_activation = std::chrono::steady_clock::now();
        activated_count++;
        
        // Update centroid using GF(3) tropical averaging (no float EMA)
        // Instead of: centroid = (1-alpha)*centroid + alpha*sample
        // We use: centroid = tropical::add(centroid, sample) with ternary weights
        for (size_t i = 0; i < expert->centroid_fixed.size() && i < sample.embedding_fixed.size(); ++i) {
            // Simple tropical update: add sample value (limited range -3 to +3)
            int8_t new_val = expert->centroid_fixed[i] + sample.embedding_fixed[i];
            // Clamp to valid ternary range
            if (new_val > 3) new_val = 3;
            if (new_val < -3) new_val = -3;
            expert->centroid_fixed[i] = new_val;
        }
        
        // Train the expert's FF learner
        std::vector<std::vector<ternary::Trit>> positive = {discrete_data};
        auto negative = expert->learner->generate_negative_samples(positive);
        
        for (size_t layer = 0; layer < learner_config_.num_layers; ++layer) {
            auto goodness = expert->learner->train_layer(layer, positive, negative);
            expert->accumulated_goodness_fixed += goodness.positive_goodness;
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
    // GF(3): Use integer max_edge instead of float (scale factor 1000)
    int32_t max_edge = 2000;  // Equivalent to 2.0f in fixed-point (scale 1000)
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
        if (expert->average_goodness_fixed() >= config_.split_goodness_threshold) {
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
        
        // Perturb centroid around parent's centroid using GF(3) ternary corruption
        // Instead of float normal_distribution, use ternary flips
        for (size_t j = 0; j < child->centroid_fixed.size() && j < parent->centroid_fixed.size(); ++j) {
            // Simple perturbation: randomly flip trit with 30% probability
            // Use child_id + j as deterministic "random" source
            uint32_t hash = (static_cast<uint32_t>(child_id) * 31 + static_cast<uint32_t>(j)) % 10;
            if (hash < 3) {  // 30% chance to perturb
                // Flip: -1 <-> +1, 0 stays 0
                if (parent->centroid_fixed[j] == -1) child->centroid_fixed[j] = 1;
                else if (parent->centroid_fixed[j] == 1) child->centroid_fixed[j] = -1;
                else child->centroid_fixed[j] = 0;
            } else {
                child->centroid_fixed[j] = parent->centroid_fixed[j];  // Copy without perturbation
            }
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
    parent->activation_count_fixed = -1;  // Negative = retired (GF(3): use int, not float)
    
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
    // GF(3): Compare integer edge counts directly, not float density
    uint64_t edge_count = get_edge_count();
    uint64_t expert_count = get_expert_count();
    
    if (expert_count < 2) return;  // No edges possible
    
    uint64_t max_edges = expert_count * (expert_count - 1) / 2;
    if (max_edges == 0) return;
    
    // Use integer comparison: edge_count * 1000 / max_edges gives per-mille density
    uint64_t density_permille = (edge_count * 1000) / max_edges;
    uint64_t min_density_permille = static_cast<uint64_t>(config_.min_graph_density * 1000);
    uint64_t max_density_permille = static_cast<uint64_t>(config_.max_graph_density * 1000);
    
    if (density_permille < min_density_permille) {
        add_edges_for_density();
    } else if (density_permille > max_density_permille) {
        prune_weak_edges();
    }
}

void SelfOrganizingExpertManager::add_edges_for_density() {
    // Add edges between nearest neighbors
    for (const auto& [id_a, expert_a] : experts_) {
        if (expert_a->activation_count_fixed < 0) continue;  // Skip retired
        
        // Find 3 nearest neighbors without edges using GF(3) tropical distance
        std::vector<std::pair<int64_t, size_t>> candidates;
        for (const auto& [id_b, expert_b] : experts_) {
            if (id_a == id_b) continue;
            if (expert_b->activation_count_fixed < 0) continue;
            if (expert_a->connected_experts.count(id_b)) continue;  // Already connected
            
            int64_t dist = gf3::tropical_manhattan_distance(expert_a->centroid_fixed, expert_b->centroid_fixed);
            candidates.push_back({dist, id_b});
        }
        
        // Sort by distance (ascending)
        // MSVC debug STL asserts when middle == last in partial_sort.
        size_t k = std::min(size_t(3), candidates.size());
        if (k >= candidates.size()) {
            std::sort(
                candidates.begin(),
                candidates.end()
            );
        } else {
            std::partial_sort(
                candidates.begin(),
                candidates.begin() + k,
                candidates.end()
            );
        }
        
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

SelfOrganizingExpertManager::TopologyStats 
SelfOrganizingExpertManager::get_stats() const {
    TopologyStats stats;
    stats.num_experts = get_expert_count();
    stats.num_edges = get_edge_count();
    stats.graph_density_fixed = get_graph_density_fixed();
    stats.split_count_total = split_count_;
    stats.merge_count_total = merge_count_;
    
    // Compute average β₁
    double sum_beta_1 = 0.0;
    stats.max_beta_1 = 0;
    stats.generations_max = 0;
    
    uint64_t total_beta_1 = 0;
    uint64_t active_count = 0;
    uint64_t max_generation = 0;
    
    for (const auto& [id, expert] : experts_) {
        total_beta_1 += expert->local_beta_1;
        if (expert->centroid_fixed.size() > 0) {
            max_generation = std::max(max_generation, expert->generation);
        }
        if (expert->activation_count_fixed >= 0) {
            active_count++;
        }
        sum_beta_1 += expert->local_beta_1;
        stats.max_beta_1 = std::max(stats.max_beta_1, expert->local_beta_1);
        stats.generations_max = std::max(stats.generations_max, expert->generation);
    }
    
    stats.avg_beta_1_fixed = static_cast<int32_t>(stats.num_experts > 0 ? (sum_beta_1 * 1000.0) / stats.num_experts : 0.0);
    
    return stats;
}

std::string SelfOrganizingExpertManager::export_topology_json() const {
    std::lock_guard<std::mutex> lock(experts_mutex_);
    
    std::ostringstream json;
    json << "{\"nodes\":[";
    
    // Export nodes (experts)
    bool first_node = true;
    for (const auto& [id, expert] : experts_) {
        if (!first_node) json << ",";
        first_node = false;
        
        // Generate a pseudo-position for visualization (simple layout)
        // In a real implementation, you'd use force-directed layout or similar
        double x = (id % 10) * 50.0 + 100.0;
        double y = (id / 10) * 50.0 + 100.0;
        
        json << "{\"id\":" << id << ",";
        json << "\"generation\":" << expert->generation << ",";
        json << "\"activation_count\":" << expert->activation_count_fixed << ",";
        json << "\"beta_1\":" << expert->local_beta_1 << ",";
        json << "\"goodness\":" << expert->average_goodness_fixed() << ",";
        json << "\"x\":" << x << ",";
        json << "\"y\":" << y << "}";
    }
    
    json << "],\"edges\":[";
    
    // Export edges (connections)
    bool first_edge = true;
    for (const auto& [id, expert] : experts_) {
        for (size_t connected_id : expert->connected_experts) {
            // Avoid duplicate edges (only output if id < connected_id)
            if (id < connected_id) {
                if (!first_edge) json << ",";
                first_edge = false;
                
                json << "{\"source\":" << id << ",";
                json << "\"target\":" << connected_id << "}";
            }
        }
    }
    
    json << "],\"stats\":{\"num_experts\":" << get_expert_count() << ",";
    json << "\"num_edges\":" << get_edge_count() << ",";
    json << "\"graph_density\": " << get_graph_density_fixed() << ",";
    json << "\"avg_beta_1_fixed\": " << get_stats().avg_beta_1_fixed << ",";
    json << "\"generations_max\": " << get_stats().generations_max << "}";
    
    json << "}}";
    
    return json.str();
}

// ============================================================================
// Missing Implementations
// ============================================================================

DynamicExpert* SelfOrganizingExpertManager::get_expert(size_t id) {
    std::lock_guard<std::mutex> lock(experts_mutex_);
    auto it = experts_.find(id);
    if (it != experts_.end()) {
        return it->second.get();
    }
    return nullptr;
}

size_t SelfOrganizingExpertManager::get_edge_count() const {
    std::lock_guard<std::mutex> lock(experts_mutex_);
    size_t count = 0;
    for (const auto& [id, expert] : experts_) {
        count += expert->connected_experts.size();
    }
    return count / 2;  // Each edge counted twice
}

int32_t SelfOrganizingExpertManager::get_graph_density_fixed() const {
    size_t edge_count = get_edge_count();
    size_t expert_count = get_expert_count();
    if (expert_count < 2) return 0;
    uint64_t max_edges = expert_count * (expert_count - 1) / 2;
    if (max_edges == 0) return 0;
    return static_cast<int32_t>((edge_count * 1000) / max_edges);
}

size_t SelfOrganizingExpertManager::merge_experts(size_t expert_a, size_t expert_b) {
    std::lock_guard<std::mutex> lock(experts_mutex_);
    
    auto* a = get_expert(expert_a);
    auto* b = get_expert(expert_b);
    if (!a || !b) return static_cast<size_t>(-1);
    
    // Merge b into a: combine centroids using tropical average
    for (size_t i = 0; i < a->centroid_fixed.size() && i < b->centroid_fixed.size(); ++i) {
        a->centroid_fixed[i] = (a->centroid_fixed[i] + b->centroid_fixed[i]) / 2;
    }
    
    // Transfer connections from b to a
    for (size_t connected_id : b->connected_experts) {
        if (connected_id != expert_a) {
            a->connected_experts.insert(connected_id);
        }
    }
    
    // Remove b from all other experts' connections
    for (auto& [id, expert] : experts_) {
        expert->connected_experts.erase(expert_b);
    }
    
    // Mark b as merged (retired)
    b->activation_count_fixed = -1;
    
    merge_count_++;
    return expert_a;
}

void SelfOrganizingExpertManager::prune_weak_edges() {
    std::lock_guard<std::mutex> lock(experts_mutex_);
    
    for (auto& [id, expert] : experts_) {
        if (expert->activation_count_fixed < 0) continue;
        
        // Remove edges to retired experts or where distance is too high
        std::vector<size_t> to_remove;
        for (size_t connected_id : expert->connected_experts) {
            auto* other = get_expert(connected_id);
            if (!other || other->activation_count_fixed < 0) {
                to_remove.push_back(connected_id);
            }
        }
        
        for (size_t rid : to_remove) {
            expert->connected_experts.erase(rid);
        }
    }
}

SelfOrganizingExpertManager::BettiNumbers 
SelfOrganizingExpertManager::compute_betti_numbers(
    const std::vector<TopologicalSample>& samples, 
    int32_t max_edge_length_fixed
) {
    BettiNumbers result{1, 0, 0, 0};  // Default: 1 component, 0 cycles
    
    if (samples.empty()) return result;
    
    // Simplified Betti estimation based on sample diversity
    std::set<std::vector<int8_t>> unique_embeddings;
    for (const auto& sample : samples) {
        std::vector<int8_t> embedding;
        for (int32_t val : sample.embedding_fixed) {
            embedding.push_back(static_cast<int8_t>(std::clamp(val, -3, 3)));
        }
        unique_embeddings.insert(embedding);
    }
    
    // Estimate β₀ as number of unique clusters
    result.beta_0 = static_cast<uint32_t>(std::min(size_t(10), unique_embeddings.size()));
    
    // Estimate β₁ based on sample count relative to unique embeddings
    // High ratio suggests cycles in the data
    if (samples.size() > unique_embeddings.size() * 2) {
        result.beta_1 = static_cast<uint32_t>(samples.size() / unique_embeddings.size());
    }
    
    return result;
}

int32_t SelfOrganizingExpertManager::centroid_distance_fixed(
    const std::vector<int32_t>& a, 
    const std::vector<int32_t>& b
) {
    return static_cast<int32_t>(gf3::tropical_manhattan_distance(a, b));
}

std::vector<int32_t> SelfOrganizingExpertManager::compute_sample_embedding_fixed(
    const std::vector<int32_t>& raw_data_fixed
) {
    // Simple projection: truncate or pad to match expected size
    size_t target_size = 64;  // Default embedding size
    std::vector<int32_t> embedding;
    embedding.reserve(target_size);
    
    for (size_t i = 0; i < target_size; ++i) {
        if (i < raw_data_fixed.size()) {
            // Normalize to ternary range [-3, 3]
            int32_t val = raw_data_fixed[i];
            embedding.push_back(std::clamp(val, -3, 3));
        } else {
            embedding.push_back(0);
        }
    }
    
    return embedding;
}

void SelfOrganizingExpertManager::flush_old_samples() {
    // Remove oldest 50% of samples to make room for new ones
    size_t to_remove = sample_buffer_.size() / 2;
    if (to_remove > 0) {
        sample_buffer_.erase(sample_buffer_.begin(), sample_buffer_.begin() + to_remove);
    }
}

void SelfOrganizingExpertManager::rebuild_centroid_cache() {
    std::lock_guard<std::mutex> lock(experts_mutex_);
    centroids_cache_fixed_.clear();
    for (const auto& [id, expert] : experts_) {
        if (expert->activation_count_fixed >= 0) {
            centroids_cache_fixed_.push_back({id, expert->centroid_fixed});
        }
    }
}

std::vector<size_t> SelfOrganizingExpertManager::get_active_expert_ids() const {
    std::lock_guard<std::mutex> lock(experts_mutex_);
    std::vector<size_t> active;
    for (const auto& [id, expert] : experts_) {
        if (expert->activation_count_fixed >= 0) {
            active.push_back(id);
        }
    }
    return active;
}

std::vector<size_t> SelfOrganizingExpertManager::get_recently_created_experts(size_t n) const {
    std::lock_guard<std::mutex> lock(experts_mutex_);
    std::vector<size_t> recent;
    // Experts are created with increasing IDs, so higher IDs are more recent
    // Collect all IDs and sort in descending order
    std::vector<size_t> all_ids;
    all_ids.reserve(experts_.size());
    for (const auto& [id, expert] : experts_) {
        all_ids.push_back(id);
    }
    std::sort(all_ids.rbegin(), all_ids.rend());
    for (size_t i = 0; i < n && i < all_ids.size(); ++i) {
        recent.push_back(all_ids[i]);
    }
    return recent;
}

} // namespace q_mini_wasm_v2::core::moe
