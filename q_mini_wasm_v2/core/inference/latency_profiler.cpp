#include "latency_profiler.hpp"
#include <algorithm>
#include <numeric>
#include <cmath>

namespace q_mini_wasm_v2::core::inference {

LatencyProfiler::LatencyProfiler(int32_t target_ms_fixed)
    : target_ms_fixed_(target_ms_fixed), total_runs_(0) {}

void LatencyProfiler::start_run() {
    current_checkpoints_.clear();
    run_start_ = std::chrono::high_resolution_clock::now();
}

void LatencyProfiler::checkpoint(const std::string& name) {
    auto now = std::chrono::high_resolution_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
        now - run_start_
    ).count();
    
    Checkpoint cp;
    cp.name = name;
    cp.timestamp = now;
    cp.nanoseconds = elapsed;
    current_checkpoints_.push_back(cp);
}

void LatencyProfiler::end_run() {
    auto now = std::chrono::high_resolution_clock::now();
    auto total_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        now - run_start_
    ).count();
    
    // Convert to fixed-point: 1 ms = 1000 units
    // total_ns / 1,000,000.0 = ms, then * 1000 for fixed-point
    int32_t total_ms_fixed = static_cast<int32_t>(total_ns / 1000);
    run_durations_ms_fixed_.push_back(total_ms_fixed);
    total_runs_++;
}

LatencyProfiler::LatencyStats LatencyProfiler::get_stats() const {
    LatencyStats stats{};
    
    if (run_durations_ms_fixed_.empty()) {
        stats.total_ms_fixed = 0;
        stats.min_ms_fixed = 0;
        stats.max_ms_fixed = 0;
        stats.avg_ms_fixed = 0;
        stats.p50_ms_fixed = 0;
        stats.p95_ms_fixed = 0;
        stats.p99_ms_fixed = 0;
        stats.sample_count = 0;
        stats.meets_sub_millisecond_target = 1;
        return stats;
    }
    
    std::vector<int32_t> sorted = run_durations_ms_fixed_;
    std::sort(sorted.begin(), sorted.end());
    
    // Calculate total using int64_t to prevent overflow
    int64_t total = 0;
    for (auto val : sorted) {
        total += val;
    }
    stats.total_ms_fixed = static_cast<int32_t>(total);
    stats.min_ms_fixed = sorted.front();
    stats.max_ms_fixed = sorted.back();
    stats.avg_ms_fixed = static_cast<int32_t>(total / static_cast<int64_t>(sorted.size()));
    stats.p50_ms_fixed = calculate_percentile_fixed(sorted, 50);
    stats.p95_ms_fixed = calculate_percentile_fixed(sorted, 95);
    stats.p99_ms_fixed = calculate_percentile_fixed(sorted, 99);
    stats.sample_count = sorted.size();
    stats.meets_sub_millisecond_target = (stats.p95_ms_fixed <= target_ms_fixed_) ? 1 : 0;
    
    return stats;
}

const std::vector<LatencyProfiler::Checkpoint>& LatencyProfiler::get_last_run_checkpoints() const {
    return current_checkpoints_;
}

void LatencyProfiler::reset() {
    current_checkpoints_.clear();
    run_durations_ms_fixed_.clear();
    total_runs_ = 0;
}

bool LatencyProfiler::meets_target() const {
    if (run_durations_ms_fixed_.empty()) return true;
    return run_durations_ms_fixed_.back() <= target_ms_fixed_;
}

int32_t LatencyProfiler::calculate_percentile_fixed(
    std::vector<int32_t> sorted_values_fixed,
    int32_t percentile
) {
    if (sorted_values_fixed.empty()) return 0;
    
    // Calculate index: (percentile / 100) * (size - 1)
    // Using fixed-point arithmetic
    int64_t index_num = static_cast<int64_t>(percentile) * (sorted_values_fixed.size() - 1);
    size_t lower = static_cast<size_t>((index_num / 100));
    size_t upper = static_cast<size_t>((index_num + 99) / 100);  // ceil equivalent
    
    if (lower == upper || upper >= sorted_values_fixed.size()) {
        return sorted_values_fixed[lower];
    }
    
    // Linear interpolation in fixed-point
    int64_t fraction_num = index_num - static_cast<int64_t>(lower) * 100;
    // value = lower_val * (1 - fraction) + upper_val * fraction
    // = lower_val + (upper_val - lower_val) * fraction
    int64_t diff = static_cast<int64_t>(sorted_values_fixed[upper]) - sorted_values_fixed[lower];
    int64_t interpolated = static_cast<int64_t>(sorted_values_fixed[lower]) + (diff * fraction_num) / 100;
    return static_cast<int32_t>(interpolated);
}

} // namespace q_mini_wasm_v2::core::inference