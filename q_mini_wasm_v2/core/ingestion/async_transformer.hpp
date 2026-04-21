#pragma once
#include "async_staging_buffer.hpp"
#include "trit_binary_loader.hpp"
#include "../ternary/trit.hpp"
#include <thread>
#include <vector>
#include <atomic>
#include <functional>
#include <queue>
#include <mutex>
#include <condition_variable>

namespace q {
namespace ingestion {

// Worker thread pool for parallel transformation
// Converts raw text/bytes to ternary trits using position-sensitive hashing
class AsyncTransformer {
public:
    using TransformCallback = std::function<void(uint64_t correlation_id, 
                                                  const std::vector<int8_t>& trits)>;
    
    explicit AsyncTransformer(StagingBuffer& buffer, size_t num_workers = 4);
    ~AsyncTransformer();
    
    // Start worker threads
    void start();
    
    // Stop all workers (graceful shutdown)
    void stop();
    
    // Set callback for completed transformations
    void set_callback(TransformCallback cb);
    
    // Manual transform (for single-threaded fallback)
    static std::vector<int8_t> transform_text_to_trits(const uint8_t* text, size_t length, 
                                                        uint32_t trit_dim);
    
    // Stats
    struct WorkerStats {
        size_t worker_id;
        uint64_t samples_processed;
        uint64_t samples_failed;
        double avg_time_us;
    };
    std::vector<WorkerStats> get_worker_stats() const;
    
private:
    StagingBuffer& buffer_;
    std::vector<std::thread> workers_;
    std::atomic<bool> running_{false};
    size_t num_workers_;
    
    TransformCallback callback_;
    std::mutex callback_mutex_;
    
    // Per-worker stats
    struct alignas(64) WorkerData {
        std::atomic<uint64_t> processed{0};
        std::atomic<uint64_t> failed{0};
        std::atomic<double> total_time_us{0.0};
    };
    std::vector<std::unique_ptr<WorkerData>> worker_data_;
    
    void worker_loop(size_t worker_id);
    
    // Text-to-trit conversion (position-sensitive hashing)
    static int8_t char_to_trit(char c, size_t position);
};

// High-level async data loader combining staging + transformation
class AsyncDataLoader {
public:
    struct Config {
        size_t staging_capacity = 1024;      // Number of staging slots
        size_t num_transform_workers = 4;   // Parallel transformation threads
        uint32_t trit_dimension = 256;      // Output trits per sample
        size_t batch_size = 32;             // WASM injection batch size
        bool enable_sbfr = true;            // Enable single-bit flip recovery
    };
    
    explicit AsyncDataLoader(const Config& config);
    ~AsyncDataLoader();
    
    // Initialize and start workers
    bool initialize();
    
    // Stop all workers (graceful shutdown)
    void stop();
    
    // Ingest a raw sample (non-blocking, returns correlation_id)
    uint64_t ingest(const std::string& text);
    uint64_t ingest(const uint8_t* data, size_t length);
    
    // Poll for ready samples (call from WASM injection thread)
    // Returns number of samples retrieved
    size_t poll_ready(std::vector<std::vector<int8_t>>& output, size_t max_samples);
    
    // Wait for all pending samples to complete (blocking)
    void wait_for_completion();
    
    // Get current pipeline stats
    struct PipelineStats {
        StagingBuffer::Stats staging_stats;
        std::vector<AsyncTransformer::WorkerStats> worker_stats;
        size_t ready_queue_size;
        uint64_t total_ingested;
        uint64_t total_ready;
        double throughput_samples_sec;
    };
    PipelineStats get_stats() const;
    
    // Load from T3B file asynchronously
    bool load_t3b_file(const std::string& filepath, size_t max_samples = 0);
    
private:
    Config config_;
    std::unique_ptr<StagingBuffer> staging_buffer_;
    std::unique_ptr<AsyncTransformer> transformer_;
    
    // Ready queue for WASM injection
    std::queue<std::pair<uint64_t, std::vector<int8_t>>> ready_queue_;
    mutable std::mutex ready_mutex_;
    std::condition_variable ready_cv_;
    
    std::atomic<uint64_t> total_ingested_{0};
    std::atomic<uint64_t> total_ready_{0};
    std::atomic<uint64_t> start_time_ns_{0};
    
    // Background T3B loading thread
    std::thread t3b_loader_thread_;
    std::atomic<bool> t3b_loading_{false};
    
    void on_transform_complete(uint64_t correlation_id, const std::vector<int8_t>& trits);
    void load_t3b_async(const std::string& filepath, size_t max_samples);
};

// Async pipeline metrics exporter (for monitoring)
class PipelineMetrics {
public:
    struct Snapshot {
        uint64_t timestamp_ns;
        uint64_t ingress_rate;        // samples/sec
        uint64_t transform_rate;      // samples/sec
        uint64_t validation_rate;       // samples/sec
        uint64_t ejection_rate;         // samples/sec
        size_t staging_utilization;     // % full
        size_t dlq_size;
        double avg_latency_us;
    };
    
    static Snapshot capture(const AsyncDataLoader& loader);
    static std::string to_json(const Snapshot& snap);
};

} // namespace ingestion
} // namespace q
