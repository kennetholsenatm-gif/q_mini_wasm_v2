#pragma once

#include <vector>
#include <memory>
#include <map>
#include <unordered_map>
#include <set>
#include <chrono>
#include <mutex>
#include "../ternary/trit.hpp"
#include "../learning/forward_forward.hpp"
#include "../qgnn/fast_betti_estimator.hpp"

namespace q_mini_wasm_v2::core::moe {

/**
 * @brief Dynamic Expert Node with topological self-organization
 * 
 * Self-organizing expert that can:
 * - Split when Betti number β₁ (cycles) exceeds threshold
 * - Merge when graph density is too low
 * - Adapt connectivity based on data topology
 */
struct DynamicExpert {
    size_t id;
    size_t generation;  // How many times this expert lineage has split
    double activation_count;
    double accumulated_goodness;
    std::vector<double> centroid;  // Geometric center in embedding space
    std::set<size_t> connected_experts;  // Graph edges
    std::chrono::steady_clock::time_point last_activation;
    
    // Betti numbers for this expert's local topology
    uint32_t local_beta_0;  // Connected components within expert's domain
    uint32_t local_beta_1;  // Cycles/holes (high = complex structure → split)
    
    // Forward-Forward learner
    std::unique_ptr<learning::ForwardForwardLearner> learner;
    
    // Config storage for lazy initialization
    learning::FFConfig learner_config_;
    bool learner_initialized_;
    
    DynamicExpert(size_t id_, const learning::FFConfig& config);
    
    // Lazy initialization - creates learner only when first used
    void ensure_learner_initialized();
    
    double average_goodness() const {
        return activation_count > 0 ? accumulated_goodness / activation_count : 0.0;
    }
    
    // Check if expert should split based on topological complexity
    bool should_split(double beta1_threshold = 5.0) const {
        return local_beta_1 > beta1_threshold && generation < 10;  // Max 10 generations
    }
    
    // Check if expert is underutilized and should merge
    bool should_merge(double min_activation_rate = 0.001) const {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::minutes>(now - last_activation).count();
        return elapsed > 60 && activation_count < min_activation_rate * elapsed;  // Idle for 1hr+
    }
};

/**
 * @brief Topological Data Sample with homology tracking
 */
struct TopologicalSample {
    std::vector<ternary::Trit> data;
    std::vector<double> embedding;  // Continuous embedding for topology
    std::chrono::steady_clock::time_point timestamp;
    std::set<size_t> assigned_experts;  // Which experts processed this
    
    // Persistent homology barcode
    struct PersistencePair {
        float birth;
        float death;
        uint32_t dimension;  // 0=component, 1=cycle, 2=void
    };
    std::vector<PersistencePair> homology_barcode;
};

/**
 * @brief Self-Organizing Expert Manager
 * 
 * Dynamically creates/destroys experts based on:
 * - Betti numbers β₀, β₁, β₂ from data topology
 * - Graph density (edges / max_edges)
 * - Persistent homology barcodes
 * - Expert utilization and performance
 * 
 * NO FIXED MAXIMUM - experts grow/shrink with data complexity
 */
class SelfOrganizingExpertManager {
public:
    struct Config {
        // Splitting thresholds
        double split_beta1_threshold = 5.0;      // Split when β₁ > 5 cycles
        double split_goodness_threshold = 0.8;   // Only split well-performing experts
        size_t max_generation_depth = 10;        // Prevent infinite splitting
        
        // Merging thresholds
        double merge_idle_threshold_min = 60.0;  // Merge after 60 min idle
        double merge_min_activation_rate = 0.001; // Merge if < 0.1% activation rate
        
        // Graph density targets
        double target_graph_density = 0.15;      // 15% connectivity (sparse but connected)
        double min_graph_density = 0.05;         // Below 5% = too sparse, add edges
        double max_graph_density = 0.40;         // Above 40% = too dense, remove edges
        
        // Initial experts
        size_t initial_experts = 8;              // Start with 8 root experts
        size_t max_experts_hard_cap = 10000;     // Safety cap (can be removed)
    };
    
    explicit SelfOrganizingExpertManager(const Config& config, const learning::FFConfig& learner_config);
    
    // ====================================================================
    // Core Operations
    // ====================================================================
    
    /**
     * @brief Route sample to experts based on topological similarity
     * 
     * Uses Betti numbers to find experts with matching topological structure
     */
    std::vector<size_t> route_sample(const TopologicalSample& sample, size_t top_k);
    
    /**
     * @brief Train specific experts on a sample
     */
    void train_experts(const TopologicalSample& sample, const std::vector<size_t>& expert_ids);
    
    /**
     * @brief Process new data and trigger self-organization
     * 
     * This is the main entry point - it:
     * 1. Computes Betti numbers for the sample
     * 2. Routes to appropriate experts
     * 3. Trains those experts
     * 4. Triggers topology update (split/merge/graph adjustment)
     */
    size_t process_sample(const std::vector<double>& raw_data, const std::vector<ternary::Trit>& discrete_data);
    
    // ====================================================================
    // Self-Organization Operations
    // ====================================================================
    
    /**
     * @brief Compute Betti numbers from recent samples and update topology
     * 
     * Called periodically or when buffer is full. This is where the magic happens:
     * - β₁ (cycles) > threshold → Split expert into sub-experts
     * - Graph density too low → Add edges between related experts
     * - Graph density too high → Prune edges
     * - Idle experts → Merge
     */
    void update_topology();
    
    /**
     * @brief Create a new expert with given parent generation
     */
    size_t create_expert(size_t parent_generation);
    
    /**
     * @brief Split an expert into 2-4 sub-experts when topology is too complex
     * 
     * High β₁ indicates the expert is handling disjoint topics - split them
     */
    std::vector<size_t> split_expert(size_t expert_id);
    
    /**
     * @brief Merge two experts that are redundant (low utilization, similar centroids)
     */
    size_t merge_experts(size_t expert_a, size_t expert_b);
    
    /**
     * @brief Adjust graph connectivity based on target density
     */
    void optimize_graph_density();
    
    /**
     * @brief Add edges between experts with similar centroids (increase density)
     */
    void add_edges_for_density();
    
    /**
     * @brief Remove edges between weakly connected experts (decrease density)
     */
    void prune_weak_edges();
    
    // ====================================================================
    // Betti Number Computation
    // ====================================================================
    
    /**
     * @brief Compute Betti numbers from a set of samples using simplicial complex
     * 
     * Uses Vietoris-Rips complex construction and GF(3) rank calculation
     */
    struct BettiNumbers {
        uint32_t beta_0;  // Connected components
        uint32_t beta_1;  // 1-cycles (loops/holes) - high = complex structure
        uint32_t beta_2;  // 2-voids (cavities)
        float graph_density;
    };
    
    BettiNumbers compute_betti_numbers(const std::vector<TopologicalSample>& samples, float max_edge_length);
    
    // ====================================================================
    // Queries
    // ====================================================================
    
    size_t get_expert_count() const { return experts_.size(); }
    size_t get_edge_count() const;
    double get_graph_density() const;
    std::vector<size_t> get_active_expert_ids() const;
    std::vector<size_t> get_recently_created_experts(size_t n = 10) const;
    
    DynamicExpert* get_expert(size_t id);
    
    // Statistics
    struct TopologyStats {
        size_t num_experts;
        size_t num_edges;
        double graph_density;
        double avg_beta_1;
        uint32_t max_beta_1;
        size_t split_count_total;
        size_t merge_count_total;
        size_t generations_max;
    };
    TopologyStats get_stats() const;
    
    /**
     * @brief Export topology as JSON for visualization
     * Returns nodes (experts) and edges (connections) for D3.js/Canvas rendering
     */
    std::string export_topology_json() const;
    
private:
    Config config_;
    learning::FFConfig learner_config_;
    
    // Thread safety
    mutable std::mutex experts_mutex_;
    
    // Expert storage - key is expert ID (monotonically increasing)
    std::unordered_map<size_t, std::unique_ptr<DynamicExpert>> experts_;
    size_t next_expert_id_ = 0;
    
    // Recent samples buffer for topology analysis
    std::vector<TopologicalSample> sample_buffer_;
    size_t max_buffer_size_ = 10000;
    
    // Split/merge tracking
    size_t split_count_ = 0;
    size_t merge_count_ = 0;
    
    // Centroid index for fast nearest-neighbor lookup (simplified: linear scan)
    std::vector<std::pair<size_t, std::vector<double>>> centroids_cache_;
    
    // Fast Betti estimation for 64+ experts (Chebyshev polynomial + Monte Carlo)
    // Falls back to exact extraction for smaller graphs
    q::qgnn::HybridBettiExtractor hybrid_betti_extractor_;
    
    // Helpers
    double centroid_distance(const std::vector<double>& a, const std::vector<double>& b);
    std::vector<double> compute_sample_embedding(const std::vector<double>& raw_data);
    void flush_old_samples();
    void rebuild_centroid_cache();
};

} // namespace q_mini_wasm_v2::core::moe
