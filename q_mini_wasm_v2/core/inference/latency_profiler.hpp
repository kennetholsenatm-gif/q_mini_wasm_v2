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
     * @brief Latency statistics for a pipeline run (fixed-point version)
     * All times in fixed-point: scale 1000 = 1.0 ms
     */
    struct LatencyStats {
        int32_t total_ms_fixed;
        int32_t min_ms_fixed;
        int32_t max_ms_fixed;
        int32_t avg_ms_fixed;
        int32_t p50_ms_fixed;
        int32_t p95_ms_fixed;
        int32_t p99_ms_fixed;
        size_t sample_count;
        int8_t meets_sub_millisecond_target;  // 0/1 instead of bool
    };

    /**
     * @brief Construct profiler with target latency (fixed-point version)
     * @param target_ms_fixed Target latency in fixed-point (1000 = 1.0 ms, default: 1000 for sub-ms)
     */
    explicit LatencyProfiler(int32_t target_ms_fixed = 1000);
    
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
     * @brief Get target latency in milliseconds (fixed-point)
     * @return Target latency in fixed-point (1000 = 1.0 ms)
     */
    int32_t get_target_ms_fixed() const { return target_ms_fixed_; }

private:
    int32_t target_ms_fixed_;  // Fixed-point: 1000 = 1.0 ms (was double)
    std::chrono::high_resolution_clock::time_point run_start_;
    std::vector<Checkpoint> current_checkpoints_;
    std::vector<int32_t> run_durations_ms_fixed_;  // Fixed-point durations (was double)
    size_t total_runs_;
    
    /**
     * @brief Calculate percentile from sorted values (fixed-point version)
     * @param sorted_values Fixed-point sorted values (scale 1000)
     * @param percentile Percentile to calculate (0-100)
     * @return Percentile value in fixed-point (scale 1000)
     */
    static int32_t calculate_percentile_fixed(
        std::vector<int32_t> sorted_values_fixed,
        int32_t percentile
    );
};

} // namespace q_mini_wasm_v2::core::inference