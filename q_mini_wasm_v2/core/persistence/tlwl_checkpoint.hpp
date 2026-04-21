#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>

namespace q {
namespace persistence {

// Trit-Level Wear Leveling (TLWL) for Checkpoint Persistence
// Based on Q-MINI IncusOS research paper
//
// Key benefits:
// - 70% reduction in Write Amplification Factor (WAF)
// - Extends NVMe lifespan during continuous ML training
// - Bypasses full block erase for sparse updates
// - Async background compaction without stalling training

// Checkpoint metadata with trit-level tracking
struct CheckpointMetadata {
    uint64_t generation_id;          // Monotonic epoch counter
    uint64_t timestamp_ns;          // Nanosecond precision
    uint32_t model_version;        // Training iteration
    uint32_t trit_density_percent; // 0-100% (sparsity indicator)
    uint64_t total_trits;           // Total trits in checkpoint
    uint64_t stale_trits;           // Trits marked for GC
    char reserved[32];              // Future expansion
};

// Sparse block tracking for wear leveling
struct SparseBlockInfo {
    uint64_t sector_address;        // NVMe physical sector
    uint32_t trit_zeros;           // Count of State 0 trits
    uint32_t trit_plus;            // Count of State +1 trits  
    uint32_t trit_minus;           // Count of State -1 trits
    uint32_t generation;           // Birth generation
    bool needs_gc;                 // >75% stale threshold
};

// Main TLWL checkpoint manager
class TLWLCheckpointManager {
public:
    // Configuration
    struct Config {
        std::string checkpoint_dir;
        uint64_t sector_size = 4096;        // NVMe page size
        uint32_t gc_threshold_percent = 75; // Stale trit threshold
        uint32_t max_generations = 128;     // Sliding window size
        bool async_compaction = true;       // Background GC
        uint32_t compaction_interval_ms = 5000; // 5 second GC cycle
    };

    TLWLCheckpointManager(const Config& config);
    ~TLWLCheckpointManager();

    // Initialize manager
    bool initialize();

    // Save checkpoint with wear leveling
    // Returns true if successfully written (may skip actual write if sparse)
    bool save_checkpoint(const std::string& name,
                         const std::vector<uint8_t>& tritpack_data,
                         uint32_t model_version);

    // Load checkpoint
    bool load_checkpoint(const std::string& name,
                         std::vector<uint8_t>& tritpack_data);

    // List available checkpoints
    std::vector<std::string> list_checkpoints() const;

    // Get wear leveling statistics
    struct WearStats {
        uint64_t total_sectors_written;
        uint64_t sectors_skipped;        // Due to sparse optimization
        uint64_t bytes_saved;          // Through wear leveling
        uint64_t gc_cycles;            // Compactions performed
        uint64_t stale_trits_reclaimed;
        double waf_reduction_percent;  // vs naive approach
    };
    WearStats get_wear_stats() const;

    // Force immediate compaction
    void force_compaction();

    // Get checkpoint metadata
    bool get_metadata(const std::string& name, CheckpointMetadata& meta) const;

private:
    Config config_;
    
    // Sector tracking for wear leveling
    std::vector<SparseBlockInfo> sector_map_;
    mutable std::mutex sector_mutex_;
    
    // Background compaction thread
    std::atomic<bool> stop_gc_;
    std::thread gc_thread_;
    std::condition_variable gc_cv_;
    
    // Generation tracking
    uint64_t current_generation_;
    std::mutex generation_mutex_;
    
    // Statistics
    mutable std::mutex stats_mutex_;
    WearStats stats_;
    
    // Allow TrainingCheckpoint to access private methods
    friend class TrainingCheckpoint;
    
    // Internal methods
    uint64_t allocate_sector();
    void release_sector(uint64_t sector);
    bool can_overlay_write(const SparseBlockInfo& block, 
                           const std::vector<uint8_t>& new_data);
    void perform_compaction();
    void gc_worker_loop();
    
    // Compute trit density of data
    void analyze_trit_density(const std::vector<uint8_t>& data,
                              uint32_t& zeros, uint32_t& plus, uint32_t& minus);
    
    // Calculate logical XOR in Base-3 (trit-level merge)
    // Returns true if overlay write is possible (only writing to 0 states)
    bool compute_trit_overlay(const std::vector<uint8_t>& old_data,
                              const std::vector<uint8_t>& new_data,
                              std::vector<uint8_t>& overlay);
    
    // Get checkpoint path
    std::string get_checkpoint_path(const std::string& name) const;
};

// High-level checkpoint API for training
class TrainingCheckpoint {
public:
    // Initialize with default TLWL configuration
    static bool initialize(const std::string& checkpoint_dir);
    
    // Save model checkpoint during training
    // Automatically applies wear leveling for sparse updates
    static bool save(const std::string& name,
                     const void* model_data,
                     size_t model_size,
                     uint32_t iteration);
    
    // Load model checkpoint
    static bool load(const std::string& name,
                     void* model_data,
                     size_t max_size);
    
    // Get latest checkpoint name
    static std::string get_latest();
    
    // Cleanup old checkpoints (keep only N most recent)
    static void cleanup_old_checkpoints(size_t keep_count);
    
    // Get wear statistics
    static TLWLCheckpointManager::WearStats get_stats();

private:
    static std::unique_ptr<TLWLCheckpointManager> manager_;
    static std::mutex init_mutex_;
};

} // namespace persistence
} // namespace q
