#pragma once

#include <vector>
#include <cstdint>
#include <memory>
#include <chrono>
#include <string>
#include <functional>
#include <atomic>

namespace q_mini_wasm_v2::core::inference {

/**
 * @brief High-resolution latency profiler for sub-millisecond inference tracking
 * 
 * Provides nanosecond-precision timing for measuring inference pipeline stages.
 * Optimized for edge AI scenarios where sub-millisecond latency is critical.
 */
class LatencyProfiler {
public:
    /**
     * @brief Timing checkpoint for a pipeline stage
     */
    struct Checkpoint {
        std::string name;
        std::chrono::high_resolution_clock::time_point timestamp;
        size_t nanoseconds;
    };

    /**
     * @brief Latency statistics for a pipeline run
     */
    struct LatencyStats {
        double total_ms;
        double min_ms;
        double max_ms;
        double avg_ms;
        double p50_ms;
        double p95_ms;
        double p99_ms;
        size_t sample_count;
        bool meets_sub_millisecond_target;
    };

    /**
     * @brief Construct profiler with target latency
     * @param target_ms Target latency in milliseconds (default: 1.0 for sub-ms)
     */
    explicit LatencyProfiler(double target_ms = 1.0);
    
    /**
     * @brief Start timing a pipeline run
     */
    void start_run();
    
    /**
     * @brief Record a checkpoint in the current run
     * @param name Name of the checkpoint/stage
     */
    void checkpoint(const std::string& name);
    
    /**
     * @brief End the current pipeline run
     */
    void end_run();
    
    /**
     * @brief Get statistics for all recorded runs
     */
    LatencyStats get_stats() const;
    
    /**
     * @brief Get checkpoints from the last run
     */
    const std::vector<Checkpoint>& get_last_run_checkpoints() const;
    
    /**
     * @brief Reset all recorded data
     */
    void reset();
    
    /**
     * @brief Check if last run met sub-millisecond target
     */
    bool meets_target() const;
    
    /**
     * @brief Get target latency in milliseconds
     */
    double get_target_ms() const { return target_ms_; }

private:
    double target_ms_;
    std::chrono::high_resolution_clock::time_point run_start_;
    std::vector<Checkpoint> current_checkpoints_;
    std::vector<double> run_durations_ms_;
    size_t total_runs_;
    
    /**
     * @brief Calculate percentile from sorted values
     */
    static double calculate_percentile(std::vector<double> sorted_values, double percentile);
};

} // namespace q_mini_wasm_v2::core::inference