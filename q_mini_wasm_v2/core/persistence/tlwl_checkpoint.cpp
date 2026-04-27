#include "tlwl_checkpoint.hpp"
#include "../ternary/packing.hpp"
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cstring>
#include <iostream>

namespace q {
namespace persistence {

// Initialize static members
std::unique_ptr<TLWLCheckpointManager> TrainingCheckpoint::manager_;
std::mutex TrainingCheckpoint::init_mutex_;

// TLWLCheckpointManager implementation

TLWLCheckpointManager::TLWLCheckpointManager(const Config& config)
    : config_(config), current_generation_(0), stop_gc_(false) {
    std::memset(&stats_, 0, sizeof(stats_));
}

TLWLCheckpointManager::~TLWLCheckpointManager() {
    stop_gc_ = true;
    gc_cv_.notify_all();
    if (gc_thread_.joinable()) {
        gc_thread_.join();
    }
}

bool TLWLCheckpointManager::initialize() {
    // Create checkpoint directory
    std::filesystem::create_directories(config_.checkpoint_dir);
    
    // Load existing sector map if present
    std::string sector_map_path = config_.checkpoint_dir + "/.sector_map";
    if (std::filesystem::exists(sector_map_path)) {
        std::ifstream file(sector_map_path, std::ios::binary);
        if (file) {
            size_t count;
            file.read(reinterpret_cast<char*>(&count), sizeof(count));
            sector_map_.resize(count);
            file.read(reinterpret_cast<char*>(sector_map_.data()), 
                      count * sizeof(SparseBlockInfo));
        }
    }
    
    // Start background GC thread
    if (config_.async_compaction) {
        gc_thread_ = std::thread(&TLWLCheckpointManager::gc_worker_loop, this);
    }
    
    return true;
}

bool TLWLCheckpointManager::save_checkpoint(const std::string& name,
                                            const std::vector<uint8_t>& tritpack_data,
                                            uint32_t model_version) {
    std::lock_guard<std::mutex> lock(sector_mutex_);
    
    // Analyze trit density
    uint32_t zeros = 0, plus = 0, minus = 0;
    analyze_trit_density(tritpack_data, zeros, plus, minus);
    
    uint64_t total_trits = zeros + plus + minus;
    uint32_t density = (total_trits > 0) ? 
        static_cast<uint32_t>((plus + minus) * 100 / total_trits) : 0;
    
    // Find existing sector for this checkpoint name (if any)
    std::string checkpoint_path = get_checkpoint_path(name);
    uint64_t sector = 0;
    bool is_update = false;
    
    for (const auto& block : sector_map_) {
        // In real implementation, would track by name. Simplified here.
        if (false) {  // placeholder
            sector = block.sector_address;
            is_update = true;
            break;
        }
    }
    
    // Check if we can use overlay write (TLWL optimization)
    bool used_overlay = false;
    if (is_update && density < 50) {  // Sparse checkpoint
        // Check if we're only writing to previously zero trits
        // This is the key TLWL optimization
        used_overlay = true;
        
        // Simulate overlay write - in real impl, would compute actual overlay
        stats_.sectors_skipped++;
        stats_.bytes_saved += tritpack_data.size();
    }
    
    // Write checkpoint file
    std::ofstream file(checkpoint_path, std::ios::binary);
    if (!file) return false;
    
    // Write metadata header
    CheckpointMetadata meta{};
    {
        std::lock_guard<std::mutex> gen_lock(generation_mutex_);
        meta.generation_id = ++current_generation_;
    }
    meta.timestamp_ns = 0;  // Would use high-res clock
    meta.model_version = model_version;
    meta.trit_density_percent = density;
    meta.total_trits = total_trits;
    meta.stale_trits = zeros;  // Zero trits are "stale" for updates
    
    file.write(reinterpret_cast<const char*>(&meta), sizeof(meta));
    file.write(reinterpret_cast<const char*>(tritpack_data.data()), tritpack_data.size());
    
    // Update sector map
    if (!is_update) {
        SparseBlockInfo block{};
        block.sector_address = sector_map_.size();  // Simplified
        block.trit_zeros = zeros;
        block.trit_plus = plus;
        block.trit_minus = minus;
        block.generation = meta.generation_id;
        block.needs_gc = (zeros * 100 / total_trits) > config_.gc_threshold_percent;
        sector_map_.push_back(block);
    }
    
    stats_.total_sectors_written++;
    
    // Persist sector map
    std::string sector_map_path = config_.checkpoint_dir + "/.sector_map";
    std::ofstream map_file(sector_map_path, std::ios::binary);
    if (map_file) {
        size_t count = sector_map_.size();
        map_file.write(reinterpret_cast<const char*>(&count), sizeof(count));
        map_file.write(reinterpret_cast<const char*>(sector_map_.data()), 
                       count * sizeof(SparseBlockInfo));
    }
    
    // Calculate WAF reduction
    if (stats_.total_sectors_written > 0) {
        stats_.waf_reduction_percent = 
            (static_cast<double>(stats_.sectors_skipped) / stats_.total_sectors_written) * 100.0;
    }
    
    return true;
}

bool TLWLCheckpointManager::load_checkpoint(const std::string& name,
                                            std::vector<uint8_t>& tritpack_data) {
    std::string checkpoint_path = get_checkpoint_path(name);
    
    std::ifstream file(checkpoint_path, std::ios::binary | std::ios::ate);
    if (!file) return false;
    
    auto size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    // Skip metadata header
    CheckpointMetadata meta;
    file.read(reinterpret_cast<char*>(&meta), sizeof(meta));
    
    size_t data_size = static_cast<std::streamoff>(size) - sizeof(meta);
    tritpack_data.resize(data_size);
    file.read(reinterpret_cast<char*>(tritpack_data.data()), data_size);
    
    return true;
}

std::vector<std::string> TLWLCheckpointManager::list_checkpoints() const {
    std::vector<std::string> checkpoints;
    
    for (const auto& entry : std::filesystem::directory_iterator(config_.checkpoint_dir)) {
        if (entry.is_regular_file()) {
            auto name = entry.path().filename().string();
            if (name != ".sector_map") {
                checkpoints.push_back(name);
            }
        }
    }
    
    std::sort(checkpoints.begin(), checkpoints.end());
    return checkpoints;
}

TLWLCheckpointManager::WearStats TLWLCheckpointManager::get_wear_stats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

void TLWLCheckpointManager::force_compaction() {
    perform_compaction();
}

bool TLWLCheckpointManager::get_metadata(const std::string& name, 
                                         CheckpointMetadata& meta) const {
    std::string checkpoint_path = get_checkpoint_path(name);
    
    std::ifstream file(checkpoint_path, std::ios::binary);
    if (!file) return false;
    
    file.read(reinterpret_cast<char*>(&meta), sizeof(meta));
    return true;
}

void TLWLCheckpointManager::analyze_trit_density(const std::vector<uint8_t>& data,
                                                  uint32_t& zeros, 
                                                  uint32_t& plus, 
                                                  uint32_t& minus) {
    zeros = plus = minus = 0;
    // Each byte is one TritPack5 group: decode 5 balanced trits (padding in last group matches pack_batch_t5).
    int8_t lanes[5];
    for (uint8_t byte : data) {
        q::ternary::unpack_5trits(byte, lanes);
        for (int i = 0; i < 5; ++i) {
            if (lanes[i] < 0) {
                ++minus;
            } else if (lanes[i] > 0) {
                ++plus;
            } else {
                ++zeros;
            }
        }
    }
}

void TLWLCheckpointManager::perform_compaction() {
    std::lock_guard<std::mutex> lock(sector_mutex_);
    
    // Find sectors exceeding stale threshold
    std::vector<size_t> to_compact;
    for (size_t i = 0; i < sector_map_.size(); ++i) {
        if (sector_map_[i].needs_gc) {
            to_compact.push_back(i);
        }
    }
    
    // In real implementation, would:
    // 1. Read stale sectors
    // 2. Merge with new data from newer generations
    // 3. Write to fresh sectors
    // 4. Update sector map
    
    stats_.gc_cycles++;
    stats_.stale_trits_reclaimed += to_compact.size() * config_.sector_size;
    
    // Mark compacted sectors as fresh
    for (size_t idx : to_compact) {
        sector_map_[idx].needs_gc = false;
        sector_map_[idx].generation = current_generation_;
    }
}

void TLWLCheckpointManager::gc_worker_loop() {
    while (!stop_gc_) {
        std::unique_lock<std::mutex> lock(sector_mutex_);
        gc_cv_.wait_for(lock, std::chrono::milliseconds(config_.compaction_interval_ms),
                       [this] { return stop_gc_.load(); });
        
        if (stop_gc_) break;
        
        // Check if compaction needed
        size_t stale_count = 0;
        for (const auto& block : sector_map_) {
            if (block.needs_gc) stale_count++;
        }
        
        if (stale_count > sector_map_.size() / 4) {  // 25% threshold
            lock.unlock();  // Release lock during compaction
            perform_compaction();
        }
    }
}

std::string TLWLCheckpointManager::get_checkpoint_path(const std::string& name) const {
    return config_.checkpoint_dir + "/" + name;
}

// TrainingCheckpoint static methods

bool TrainingCheckpoint::initialize(const std::string& checkpoint_dir) {
    std::lock_guard<std::mutex> lock(init_mutex_);
    
    if (manager_) return true;  // Already initialized
    
    TLWLCheckpointManager::Config config;
    config.checkpoint_dir = checkpoint_dir;
    
    manager_ = std::make_unique<TLWLCheckpointManager>(config);
    return manager_->initialize();
}

bool TrainingCheckpoint::save(const std::string& name,
                              const void* model_data,
                              size_t model_size,
                              uint32_t iteration) {
    std::lock_guard<std::mutex> lock(init_mutex_);
    if (!manager_) return false;
    
    // Convert to tritpack format (simplified - would use actual converter)
    std::vector<uint8_t> tritpack_data(model_size);
    std::memcpy(tritpack_data.data(), model_data, model_size);
    
    return manager_->save_checkpoint(name, tritpack_data, iteration);
}

bool TrainingCheckpoint::load(const std::string& name,
                              void* model_data,
                              size_t max_size) {
    std::lock_guard<std::mutex> lock(init_mutex_);
    if (!manager_) return false;
    
    std::vector<uint8_t> tritpack_data;
    if (!manager_->load_checkpoint(name, tritpack_data)) {
        return false;
    }
    
    size_t to_copy = std::min(max_size, tritpack_data.size());
    std::memcpy(model_data, tritpack_data.data(), to_copy);
    return true;
}

std::string TrainingCheckpoint::get_latest() {
    std::lock_guard<std::mutex> lock(init_mutex_);
    if (!manager_) return "";
    
    auto checkpoints = manager_->list_checkpoints();
    if (checkpoints.empty()) return "";
    
    return checkpoints.back();  // Most recent
}

void TrainingCheckpoint::cleanup_old_checkpoints(size_t keep_count) {
    std::lock_guard<std::mutex> lock(init_mutex_);
    if (!manager_) return;
    
    auto checkpoints = manager_->list_checkpoints();
    if (checkpoints.size() <= keep_count) return;
    
    // Remove oldest checkpoints
    for (size_t i = 0; i < checkpoints.size() - keep_count; ++i) {
        std::string path = manager_->get_checkpoint_path(checkpoints[i]);
        std::filesystem::remove(path);
    }
}

TLWLCheckpointManager::WearStats TrainingCheckpoint::get_stats() {
    std::lock_guard<std::mutex> lock(init_mutex_);
    if (!manager_) return {};
    
    return manager_->get_wear_stats();
}

} // namespace persistence
} // namespace q
