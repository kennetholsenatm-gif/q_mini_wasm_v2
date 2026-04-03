#include "latency_profiler.hpp"
#include <algorithm>
#include <numeric>
#include <cmath>

namespace q_mini_wasm_v2::core::inference {

LatencyProfiler::LatencyProfiler(double target_ms)
    : target_ms_(target_ms), total_runs_(0) {}

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
    
    double total_ms = static_cast<double>(total_ns) / 1000000.0;
    run_durations_ms_.push_back(total_ms);
    total_runs_++;
}

LatencyProfiler::LatencyStats LatencyProfiler::get_stats() const {
    LatencyStats stats;
    
    if (run_durations_ms_.empty()) {
        stats.total_ms = 0;
        stats.min_ms = 0;
        stats.max_ms = 0;
        stats.avg_ms = 0;
        stats.p50_ms = 0;
        stats.p95_ms = 0;
        stats.p99_ms = 0;
        stats.sample_count = 0;
        stats.meets_sub_millisecond_target = true;
        return stats;
    }
    
    std::vector<double> sorted = run_durations_ms_;
    std::sort(sorted.begin(), sorted.end());
    
    stats.total_ms = std::accumulate(sorted.begin(), sorted.end(), 0.0);
    stats.min_ms = sorted.front();
    stats.max_ms = sorted.back();
    stats.avg_ms = stats.total_ms / sorted.size();
    stats.p50_ms = calculate_percentile(sorted, 50.0);
    stats.p95_ms = calculate_percentile(sorted, 95.0);
    stats.p99_ms = calculate_percentile(sorted, 99.0);
    stats.sample_count = sorted.size();
    stats.meets_sub_millisecond_target = (stats.p95_ms <= target_ms_);
    
    return stats;
}

const std::vector<LatencyProfiler::Checkpoint>& LatencyProfiler::get_last_run_checkpoints() const {
    return current_checkpoints_;
}

void LatencyProfiler::reset() {
    current_checkpoints_.clear();
    run_durations_ms_.clear();
    total_runs_ = 0;
}

bool LatencyProfiler::meets_target() const {
    if (run_durations_ms_.empty()) return true;
    return run_durations_ms_.back() <= target_ms_;
}

double LatencyProfiler::calculate_percentile(
    std::vector<double> sorted_values,
    double percentile
) {
    if (sorted_values.empty()) return 0.0;
    
    double index = (percentile / 100.0) * (sorted_values.size() - 1);
    size_t lower = static_cast<size_t>(std::floor(index));
    size_t upper = static_cast<size_t>(std::ceil(index));
    
    if (lower == upper) {
        return sorted_values[lower];
    }
    
    double fraction = index - lower;
    return sorted_values[lower] * (1.0 - fraction) + sorted_values[upper] * fraction;
}

} // namespace q_mini_wasm_v2::core::inference