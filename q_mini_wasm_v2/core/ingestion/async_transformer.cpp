#include "async_transformer.hpp"
#include "../ternary/packing.hpp"
#include <chrono>
#include <cstring>
#include <iostream>

namespace q {
namespace ingestion {

// ============================================================================
// AsyncTransformer Implementation
// ============================================================================

AsyncTransformer::AsyncTransformer(StagingBuffer& buffer, size_t num_workers)
    : buffer_(buffer), num_workers_(num_workers) {
    
    for (size_t i = 0; i < num_workers; ++i) {
        worker_data_.push_back(std::make_unique<WorkerData>());
    }
}

AsyncTransformer::~AsyncTransformer() {
    stop();
}

void AsyncTransformer::start() {
    if (running_.exchange(true)) {
        return; // Already running
    }
    
    workers_.clear();
    for (size_t i = 0; i < num_workers_; ++i) {
        workers_.emplace_back(&AsyncTransformer::worker_loop, this, i);
    }
}

void AsyncTransformer::stop() {
    running_.store(false);
    
    for (auto& t : workers_) {
        if (t.joinable()) {
            t.join();
        }
    }
    workers_.clear();
}

void AsyncTransformer::set_callback(TransformCallback cb) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    callback_ = cb;
}

int8_t AsyncTransformer::char_to_trit(char c, size_t position) {
    // Position-sensitive hash: combine char value with position
    // Same algorithm as Python converter for compatibility
    uint64_t hash_val = (static_cast<uint64_t>(static_cast<unsigned char>(c)) * 2654435761ULL 
                         + position * 2246822519ULL) & 0xFFFFFFFFULL;
    
    uint32_t val = static_cast<uint32_t>(hash_val % 3);
    if (val == 0) return -1;
    if (val == 1) return 0;
    return 1;
}

std::vector<int8_t> AsyncTransformer::transform_text_to_trits(const uint8_t* text, size_t length,
                                                            uint32_t trit_dim) {
    std::vector<int8_t> trits;
    trits.reserve(trit_dim);
    
    // Convert each char to trit using position-sensitive hashing
    for (size_t i = 0; i < length && trits.size() < trit_dim; ++i) {
        trits.push_back(char_to_trit(static_cast<char>(text[i]), i));
    }
    
    // Pad to dimension with zeros
    while (trits.size() < trit_dim) {
        trits.push_back(0);
    }
    
    return trits;
}

void AsyncTransformer::worker_loop(size_t worker_id) {
    WorkerData& stats = *worker_data_[worker_id];
    
    while (running_.load(std::memory_order_relaxed)) {
        // Claim work from staging buffer
        int64_t slot = buffer_.claim_for_transformation();
        
        if (slot < 0) {
            // No work available, yield
            std::this_thread::sleep_for(std::chrono::microseconds(10));
            continue;
        }
        
        auto start_time = std::chrono::high_resolution_clock::now();
        
        // Get entry data
        // Note: In real implementation, we'd need accessor methods on StagingBuffer
        // For now, this is a simplified version
        
        // Transform (simplified - real would read from entry)
        // For now, just mark as complete to show flow
        
        auto end_time = std::chrono::high_resolution_clock::now();
        double elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
            end_time - start_time).count();
        
        // Update stats
        stats.processed.fetch_add(1, std::memory_order_relaxed);
        stats.total_time_us.fetch_add(elapsed_us, std::memory_order_relaxed);
        
        // Trigger callback if set
        {
            std::lock_guard<std::mutex> lock(callback_mutex_);
            if (callback_) {
                // callback_(correlation_id, trits);
            }
        }
    }
}

std::vector<AsyncTransformer::WorkerStats> AsyncTransformer::get_worker_stats() const {
    std::vector<WorkerStats> stats;
    stats.reserve(worker_data_.size());
    
    for (size_t i = 0; i < worker_data_.size(); ++i) {
        const WorkerData& wd = *worker_data_[i];
        uint64_t processed = wd.processed.load(std::memory_order_relaxed);
        
        WorkerStats ws;
        ws.worker_id = i;
        ws.samples_processed = processed;
        ws.samples_failed = wd.failed.load(std::memory_order_relaxed);
        ws.avg_time_us = (processed > 0) ? 
            wd.total_time_us.load(std::memory_order_relaxed) / processed : 0.0;
        
        stats.push_back(ws);
    }
    
    return stats;
}

// ============================================================================
// AsyncDataLoader Implementation
// ============================================================================

AsyncDataLoader::AsyncDataLoader(const Config& config) : config_(config) {
    staging_buffer_ = std::make_unique<StagingBuffer>(config.staging_capacity);
    transformer_ = std::make_unique<AsyncTransformer>(*staging_buffer_, 
                                                       config.num_transform_workers);
}

AsyncDataLoader::~AsyncDataLoader() {
    stop();
}

bool AsyncDataLoader::initialize() {
    // Set up callback for completed transformations
    transformer_->set_callback(
        [this](uint64_t corr_id, const std::vector<int8_t>& trits) {
            this->on_transform_complete(corr_id, trits);
        }
    );
    
    // Start workers
    transformer_->start();
    
    start_time_ns_.store(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()
        ).count()
    );
    
    return true;
}

void AsyncDataLoader::stop() {
    transformer_->stop();
    
    if (t3b_loader_thread_.joinable()) {
        t3b_loading_.store(false);
        t3b_loader_thread_.join();
    }
}

uint64_t AsyncDataLoader::ingest(const std::string& text) {
    return ingest(reinterpret_cast<const uint8_t*>(text.data()), text.size());
}

uint64_t AsyncDataLoader::ingest(const uint8_t* data, size_t length) {
    // Clamp length to max staging size
    if (length > StagingEntry::MAX_RAW_SIZE) {
        length = StagingEntry::MAX_RAW_SIZE;
    }
    
    uint64_t corr_id = staging_buffer_->stage_data(data, static_cast<uint32_t>(length),
                                                    config_.trit_dimension);
    
    if (corr_id != 0) {
        total_ingested_.fetch_add(1, std::memory_order_relaxed);
    }
    
    return corr_id;
}

void AsyncDataLoader::on_transform_complete(uint64_t correlation_id, 
                                          const std::vector<int8_t>& trits) {
    std::lock_guard<std::mutex> lock(ready_mutex_);
    ready_queue_.push({correlation_id, trits});
    total_ready_.fetch_add(1, std::memory_order_relaxed);
    ready_cv_.notify_one();
}

size_t AsyncDataLoader::poll_ready(std::vector<std::vector<int8_t>>& output, 
                                   size_t max_samples) {
    std::lock_guard<std::mutex> lock(ready_mutex_);
    
    size_t count = 0;
    while (!ready_queue_.empty() && count < max_samples) {
        output.push_back(std::move(ready_queue_.front().second));
        ready_queue_.pop();
        ++count;
    }
    
    return count;
}

void AsyncDataLoader::wait_for_completion() {
    // Wait until all ingested samples are ready
    while (total_ready_.load() < total_ingested_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

AsyncDataLoader::PipelineStats AsyncDataLoader::get_stats() const {
    PipelineStats stats;
    stats.staging_stats = staging_buffer_->get_stats();
    stats.worker_stats = transformer_->get_worker_stats();
    
    {
        std::lock_guard<std::mutex> lock(ready_mutex_);
        stats.ready_queue_size = ready_queue_.size();
    }
    
    stats.total_ingested = total_ingested_.load(std::memory_order_relaxed);
    stats.total_ready = total_ready_.load(std::memory_order_relaxed);
    
    // Calculate throughput
    uint64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()
    ).count();
    uint64_t elapsed_ns = now_ns - start_time_ns_.load(std::memory_order_relaxed);
    
    if (elapsed_ns > 0) {
        stats.throughput_samples_sec = 
            static_cast<double>(stats.total_ready) * 1e9 / elapsed_ns;
    } else {
        stats.throughput_samples_sec = 0.0;
    }
    
    return stats;
}

bool AsyncDataLoader::load_t3b_file(const std::string& filepath, size_t max_samples) {
    if (t3b_loading_.exchange(true)) {
        return false; // Already loading
    }
    
    t3b_loader_thread_ = std::thread(&AsyncDataLoader::load_t3b_async, this, 
                                      filepath, max_samples);
    
    return true;
}

void AsyncDataLoader::load_t3b_async(const std::string& filepath, size_t max_samples) {
    T3BLoader loader;
    if (!loader.open(filepath)) {
        std::cerr << "AsyncDataLoader: Failed to open T3B file: " << filepath << std::endl;
        t3b_loading_.store(false);
        return;
    }
    
    size_t count = 0;
    loader.stream_samples([&](uint64_t index, const std::vector<int8_t>& trits) -> bool {
        // Ingest pre-converted trits directly
        // Note: This bypasses transformation stage since trits are already packed
        
        on_transform_complete(index, trits);
        
        ++count;
        if (max_samples > 0 && count >= max_samples) {
            return false; // Stop streaming
        }
        
        return t3b_loading_.load(); // Continue while not cancelled
    });
    
    t3b_loading_.store(false);
}

// ============================================================================
// PipelineMetrics Implementation
// ============================================================================

PipelineMetrics::Snapshot PipelineMetrics::capture(const AsyncDataLoader& loader) {
    Snapshot snap;
    auto stats = loader.get_stats();
    
    snap.timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()
    ).count();
    
    snap.ingress_rate = static_cast<uint64_t>(stats.throughput_samples_sec);
    snap.transform_rate = 0;
    snap.validation_rate = 0;
    snap.ejection_rate = 0;
    
    // Sum up worker throughput
    for (const auto& ws : stats.worker_stats) {
        snap.transform_rate += static_cast<uint64_t>(
            ws.avg_time_us > 0 ? 1e6 / ws.avg_time_us : 0
        );
    }
    
    snap.staging_utilization = static_cast<size_t>(
        100.0 * stats.staging_stats.total_staged / 
        (stats.staging_stats.total_staged + stats.ready_queue_size + 1)
    );
    snap.dlq_size = stats.staging_stats.total_dlq;
    snap.avg_latency_us = stats.staging_stats.avg_latency_us;
    
    return snap;
}

std::string PipelineMetrics::to_json(const Snapshot& snap) {
    return "{"
        "\"timestamp\":" + std::to_string(snap.timestamp_ns) + "," +
        "\"ingress_rate\":" + std::to_string(snap.ingress_rate) + "," +
        "\"transform_rate\":" + std::to_string(snap.transform_rate) + "," +
        "\"validation_rate\":" + std::to_string(snap.validation_rate) + "," +
        "\"ejection_rate\":" + std::to_string(snap.ejection_rate) + "," +
        "\"staging_utilization\":" + std::to_string(snap.staging_utilization) + "," +
        "\"dlq_size\":" + std::to_string(snap.dlq_size) + "," +
        "\"avg_latency_us\":" + std::to_string(snap.avg_latency_us) +
        "}";
}

} // namespace ingestion
} // namespace q
