#include "dataset_storage.hpp"
#include <fstream>
#include <iostream>
#include <algorithm>

namespace q_mini_wasm_v2::core::training {

DatasetStorage::DatasetStorage(const std::string& data_dir)
    : data_dir_(data_dir)
    , index_file_(data_dir + "/dataset_index.bin")
    , topology_file_(data_dir + "/topology_snapshot.bin")
{
    ensure_directory();
}

void DatasetStorage::ensure_directory() const {
    std::filesystem::create_directories(data_dir_);
}

std::string DatasetStorage::get_chunk_path(size_t chunk_id) const {
    return data_dir_ + "/chunk_" + std::to_string(chunk_id) + ".bin";
}

struct ChunkHeader {
    uint32_t magic = 0x44415441; // "DATA"
    uint32_t version = 1;
    uint32_t num_samples = 0;
    uint64_t timestamp = 0;
};

struct SampleHeader {
    uint32_t data_size = 0;  // Number of trits
    uint32_t api_len = 0;
    uint32_t domain_len = 0;
    int64_t timestamp = 0;
    
    // TOPOLOGICAL GEOMETRY (new in v2)
    uint32_t num_activated_experts = 0;
    uint32_t embedding_dim = 0;
    uint32_t homology_pairs = 0;
    
    // Graph topology snapshot at storage time
    float graph_density = 0.0f;
    float avg_beta_1 = 0.0f;
    uint32_t num_experts_snapshot = 0;
    uint32_t num_edges_snapshot = 0;
};

size_t DatasetStorage::get_next_chunk_id() const {
    // Internal version - no lock (caller must hold lock or be safe)
    size_t max_id = 0;
    if (std::filesystem::exists(index_file_)) {
        std::ifstream f(index_file_, std::ios::binary);
        if (f.is_open()) {
            uint32_t stored_next = 0;
            f.read(reinterpret_cast<char*>(&stored_next), sizeof(stored_next));
            max_id = stored_next;
        }
    }
    return max_id;
}

size_t DatasetStorage::get_next_chunk_id_nolock() const {
    // Same as above but explicitly for internal use when lock is held
    size_t max_id = 0;
    if (std::filesystem::exists(index_file_)) {
        std::ifstream f(index_file_, std::ios::binary);
        if (f.is_open()) {
            uint32_t stored_next = 0;
            f.read(reinterpret_cast<char*>(&stored_next), sizeof(stored_next));
            max_id = stored_next;
        }
    }
    return max_id;
}

size_t DatasetStorage::get_stored_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return get_stored_count_nolock();
}

size_t DatasetStorage::get_stored_count_nolock() const {
    // Internal version - no lock (caller must hold lock)
    size_t total = 0;
    if (std::filesystem::exists(index_file_)) {
        std::ifstream f(index_file_, std::ios::binary);
        if (f.is_open()) {
            f.seekg(sizeof(uint32_t)); // Skip next_chunk_id
            uint64_t stored_total = 0;
            f.read(reinterpret_cast<char*>(&stored_total), sizeof(stored_total));
            total = stored_total;
        }
    }
    return total;
}

bool DatasetStorage::has_data() const {
    return get_stored_count() > 0;
}

void DatasetStorage::save_samples(const std::vector<TopologicalSample>& samples) {
    if (samples.empty()) return;
    
    std::lock_guard<std::mutex> lock(mutex_);
    ensure_directory();
    
    size_t chunk_id = get_next_chunk_id_nolock();
    std::string chunk_path = get_chunk_path(chunk_id);
    
    std::ofstream f(chunk_path, std::ios::binary);
    if (!f.is_open()) {
        std::cerr << "[DatasetStorage] Failed to open " << chunk_path << std::endl;
        return;
    }
    
    // Write chunk header (v2 for topological format)
    ChunkHeader header;
    header.version = 2;  // Topological geometry format
    header.num_samples = static_cast<uint32_t>(samples.size());
    header.timestamp = std::chrono::system_clock::now().time_since_epoch().count();
    f.write(reinterpret_cast<const char*>(&header), sizeof(header));
    
    // Write each sample with topological geometry
    for (const auto& sample : samples) {
        SampleHeader sh;
        sh.data_size = static_cast<uint32_t>(sample.data.size());
        sh.api_len = static_cast<uint32_t>(sample.source_api.size());
        sh.domain_len = static_cast<uint32_t>(sample.domain.size());
        sh.timestamp = sample.timestamp;
        
        // Topological fields
        sh.num_activated_experts = static_cast<uint32_t>(sample.activated_experts.size());
        sh.embedding_dim = static_cast<uint32_t>(sample.embedding.size());
        sh.homology_pairs = static_cast<uint32_t>(sample.homology_barcode.size());
        sh.graph_density = sample.graph_density_at_storage;
        sh.avg_beta_1 = sample.avg_beta_1_at_storage;
        sh.num_experts_snapshot = sample.num_experts_at_storage;
        sh.num_edges_snapshot = sample.num_edges_at_storage;
        
        f.write(reinterpret_cast<const char*>(&sh), sizeof(sh));
        
        // Write trit data
        for (auto t : sample.data) {
            int8_t val = static_cast<int8_t>(t);
            f.write(reinterpret_cast<const char*>(&val), sizeof(val));
        }
        
        // Write strings
        f.write(sample.source_api.data(), sample.source_api.size());
        f.write(sample.domain.data(), sample.domain.size());
        
        // Write topological geometry
        // Activated experts (graph nodes)
        for (auto expert_id : sample.activated_experts) {
            uint64_t id = static_cast<uint64_t>(expert_id);
            f.write(reinterpret_cast<const char*>(&id), sizeof(id));
        }
        
        // Embedding vector (continuous coordinates)
        for (auto coord : sample.embedding) {
            double val = coord;
            f.write(reinterpret_cast<const char*>(&val), sizeof(val));
        }
        
        // Homology barcode (persistent homology)
        for (const auto& pair : sample.homology_barcode) {
            f.write(reinterpret_cast<const char*>(&pair.birth), sizeof(pair.birth));
            f.write(reinterpret_cast<const char*>(&pair.death), sizeof(pair.death));
            f.write(reinterpret_cast<const char*>(&pair.dimension), sizeof(pair.dimension));
        }
    }
    
    f.close();
    
    // Update index
    uint32_t next_chunk = static_cast<uint32_t>(chunk_id + 1);
    uint64_t total_samples = get_stored_count_nolock() + samples.size();
    
    std::ofstream idx_f(index_file_, std::ios::binary);
    if (idx_f.is_open()) {
        idx_f.write(reinterpret_cast<const char*>(&next_chunk), sizeof(next_chunk));
        idx_f.write(reinterpret_cast<const char*>(&total_samples), sizeof(total_samples));
    }
    
    std::cout << "[DatasetStorage] Saved " << samples.size() << " samples to chunk " 
              << chunk_id << " (total: " << total_samples << ")" << std::endl;
}

std::vector<TopologicalSample> DatasetStorage::load_samples(size_t max_count) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<TopologicalSample> result;
    
    size_t next_chunk = get_next_chunk_id();
    if (next_chunk == 0) return result;
    
    for (size_t chunk_id = 0; chunk_id < next_chunk; ++chunk_id) {
        std::string chunk_path = get_chunk_path(chunk_id);
        if (!std::filesystem::exists(chunk_path)) continue;
        
        std::ifstream f(chunk_path, std::ios::binary);
        if (!f.is_open()) continue;
        
        ChunkHeader header;
        f.read(reinterpret_cast<char*>(&header), sizeof(header));
        
        if (header.magic != 0x44415441) {
            std::cerr << "[DatasetStorage] Invalid chunk format: " << chunk_id << std::endl;
            continue;
        }
        
        for (uint32_t i = 0; i < header.num_samples; ++i) {
            SampleHeader sh;
            f.read(reinterpret_cast<char*>(&sh), sizeof(sh));
            
            TopologicalSample sample;
            sample.timestamp = sh.timestamp;
            
            // Read trits
            for (uint32_t j = 0; j < sh.data_size; ++j) {
                int8_t val;
                f.read(reinterpret_cast<char*>(&val), sizeof(val));
                sample.data.push_back(static_cast<ternary::Trit>(val));
            }
            
            // Read strings
            if (sh.api_len > 0) {
                sample.source_api.resize(sh.api_len);
                f.read(&sample.source_api[0], sh.api_len);
            }
            if (sh.domain_len > 0) {
                sample.domain.resize(sh.domain_len);
                f.read(&sample.domain[0], sh.domain_len);
            }
            
            // Read topological geometry (if v2 format)
            if (header.version >= 2) {
                // Graph topology snapshot
                sample.graph_density_at_storage = sh.graph_density;
                sample.avg_beta_1_at_storage = sh.avg_beta_1;
                sample.num_experts_at_storage = sh.num_experts_snapshot;
                sample.num_edges_at_storage = sh.num_edges_snapshot;
                
                // Activated experts (graph nodes)
                for (uint32_t j = 0; j < sh.num_activated_experts; ++j) {
                    uint64_t expert_id;
                    f.read(reinterpret_cast<char*>(&expert_id), sizeof(expert_id));
                    sample.activated_experts.insert(static_cast<size_t>(expert_id));
                }
                
                // Embedding vector (continuous coordinates)
                sample.embedding.reserve(sh.embedding_dim);
                for (uint32_t j = 0; j < sh.embedding_dim; ++j) {
                    double coord;
                    f.read(reinterpret_cast<char*>(&coord), sizeof(coord));
                    sample.embedding.push_back(coord);
                }
                
                // Homology barcode (persistent homology)
                for (uint32_t j = 0; j < sh.homology_pairs; ++j) {
                    PersistencePair pair;
                    f.read(reinterpret_cast<char*>(&pair.birth), sizeof(pair.birth));
                    f.read(reinterpret_cast<char*>(&pair.death), sizeof(pair.death));
                    f.read(reinterpret_cast<char*>(&pair.dimension), sizeof(pair.dimension));
                    sample.homology_barcode.push_back(pair);
                }
            }
            
            result.push_back(std::move(sample));
            
            if (max_count > 0 && result.size() >= max_count) {
                return result;
            }
        }
    }
    
    std::cout << "[DatasetStorage] Loaded " << result.size() << " samples from disk" << std::endl;
    return result;
}

// Save/retrieve graph topology snapshot
void DatasetStorage::set_topology(const StorageTopology& topo) {
    std::lock_guard<std::mutex> lock(mutex_);
    ensure_directory();
    
    std::ofstream f(topology_file_, std::ios::binary);
    if (!f.is_open()) return;
    
    f.write(reinterpret_cast<const char*>(&topo.num_experts), sizeof(topo.num_experts));
    f.write(reinterpret_cast<const char*>(&topo.num_edges), sizeof(topo.num_edges));
    f.write(reinterpret_cast<const char*>(&topo.graph_density), sizeof(topo.graph_density));
    f.write(reinterpret_cast<const char*>(&topo.avg_beta_1), sizeof(topo.avg_beta_1));
    f.write(reinterpret_cast<const char*>(&topo.max_beta_1), sizeof(topo.max_beta_1));
    f.write(reinterpret_cast<const char*>(&topo.generation), sizeof(topo.generation));
}

DatasetStorage::StorageTopology DatasetStorage::get_topology() const {
    std::lock_guard<std::mutex> lock(mutex_);
    StorageTopology topo;
    
    if (!std::filesystem::exists(topology_file_)) return topo;
    
    std::ifstream f(topology_file_, std::ios::binary);
    if (!f.is_open()) return topo;
    
    f.read(reinterpret_cast<char*>(&topo.num_experts), sizeof(topo.num_experts));
    f.read(reinterpret_cast<char*>(&topo.num_edges), sizeof(topo.num_edges));
    f.read(reinterpret_cast<char*>(&topo.graph_density), sizeof(topo.graph_density));
    f.read(reinterpret_cast<char*>(&topo.avg_beta_1), sizeof(topo.avg_beta_1));
    f.read(reinterpret_cast<char*>(&topo.max_beta_1), sizeof(topo.max_beta_1));
    f.read(reinterpret_cast<char*>(&topo.generation), sizeof(topo.generation));
    
    return topo;
}

// Query samples by topological features (requires scanning)
std::vector<TopologicalSample> DatasetStorage::find_by_expert_activation(size_t expert_id, size_t max_count) {
    std::vector<TopologicalSample> result;
    std::vector<TopologicalSample> all_samples = load_samples(0);
    
    for (const auto& sample : all_samples) {
        if (sample.activated_experts.count(expert_id) > 0) {
            result.push_back(sample);
            if (result.size() >= max_count) break;
        }
    }
    return result;
}

std::vector<TopologicalSample> DatasetStorage::find_by_beta_1_range(float min_beta, float max_beta, size_t max_count) {
    std::vector<TopologicalSample> result;
    std::vector<TopologicalSample> all_samples = load_samples(0);
    
    for (const auto& sample : all_samples) {
        if (sample.avg_beta_1_at_storage >= min_beta && sample.avg_beta_1_at_storage <= max_beta) {
            result.push_back(sample);
            if (result.size() >= max_count) break;
        }
    }
    return result;
}

} // namespace
