#pragma once

#include <vector>
#include <string>
#include <filesystem>
#include <mutex>
#include <set>
#include "../ternary/trit.hpp"

namespace q_mini_wasm_v2::core::training {

/**
 * @brief Topological persistence pair (from persistent homology)
 */
struct PersistencePair {
    float birth;
    float death;
    uint32_t dimension;  // 0=component, 1=cycle, 2=void
};

/**
 * @brief Topological sample with homology tracking
 * 
 * Captures not just raw data but its topological context:
 * - Which experts were activated (graph nodes)
 * - Topological features (cycles, holes, voids)
 * - Embedding in continuous space for similarity
 */
struct TopologicalSample {
    std::vector<ternary::Trit> data;
    std::string source_api;
    std::string domain;
    int64_t timestamp;
    
    // TOPOLOGICAL GEOMETRY
    std::set<size_t> activated_experts;  // Graph nodes this sample activated
    std::vector<double> embedding;       // Continuous embedding coordinates
    std::vector<PersistencePair> homology_barcode;  // Persistent homology
    
    // Graph topology at time of storage
    float graph_density_at_storage = 0.0f;
    float avg_beta_1_at_storage = 0.0f;
    uint32_t num_experts_at_storage = 0;
    uint32_t num_edges_at_storage = 0;
};

// Backward compatibility: StoredSample alias
using StoredSample = TopologicalSample;

// Persistent dataset storage - accumulate samples across runs
class DatasetStorage {
public:
    struct StorageTopology {
        uint32_t num_experts = 0;
        uint32_t num_edges = 0;
        float graph_density = 0.0f;
        float avg_beta_1 = 0.0f;
        float max_beta_1 = 0.0f;
        uint32_t generation = 0;
    };

    explicit DatasetStorage(const std::string& data_dir);
    
    // Save samples to disk (appends to existing dataset)
    void save_samples(const std::vector<StoredSample>& samples);
    
    // Load samples from disk
    std::vector<StoredSample> load_samples(size_t max_count = 0); // 0 = all
    
    // Get total stored sample count
    size_t get_stored_count() const;
    
    // Check if dataset exists and has data
    bool has_data() const;
    
    // Get dataset directory path
    std::string get_data_dir() const { return data_dir_; }
    
    // Get/set topology snapshot for this storage
    StorageTopology get_topology() const;
    void set_topology(const StorageTopology& topo);
    
    // Query samples by topological features
    std::vector<StoredSample> find_by_expert_activation(size_t expert_id, size_t max_count = 100);
    std::vector<StoredSample> find_by_beta_1_range(float min_beta, float max_beta, size_t max_count = 100);

private:
    std::string data_dir_;
    std::string index_file_;
    std::string topology_file_;  // Stores graph topology snapshot
    mutable std::mutex mutex_;
    
    void ensure_directory() const;
    std::string get_chunk_path(size_t chunk_id) const;
    size_t get_next_chunk_id() const;
    
    // Internal versions without locking (caller must hold lock)
    size_t get_next_chunk_id_nolock() const;
    size_t get_stored_count_nolock() const;
};

} // namespace
