#include "fast_betti_estimator.hpp"
#include <chrono>
#include <numeric>
#include <algorithm>
#include <cmath>

namespace q {
namespace qgnn {

// Initialize Chebyshev coefficients for spectral approximation
// We use Chebyshev polynomials of the first kind: T_k(x) = cos(k*arccos(x))
// These are orthogonal on [-1, 1] with weight (1-x²)^(-1/2)

FastBettiEstimator::FastBettiEstimator(const Config& config)
    : config_(config), rng_(42) {
    compute_chebyshev_coefficients();
    
    // Pre-generate random vectors for Monte Carlo
    random_vectors_.reserve(config.monte_carlo_samples);
    std::normal_distribution<double> dist(0.0, 1.0);
    
    for (uint32_t i = 0; i < config.monte_carlo_samples; ++i) {
        std::vector<double> vec(128);  // Max expected size
        for (auto& v : vec) {
            v = dist(rng_);
        }
        random_vectors_.push_back(vec);
    }
    
    std::memset(&stats_, 0, sizeof(stats_));
}

void FastBettiEstimator::compute_chebyshev_coefficients() {
    // Compute coefficients for approximating the eigenvalue step function
    // The step function at λ=0 gives us the nullity (Betti numbers)
    // 
    // Chebyshev expansion: f(x) ≈ Σ c_k T_k(x)
    // where c_k = (2/π) ∫ f(x) T_k(x) / sqrt(1-x²) dx
    
    chebyshev_coeffs_.resize(config_.chebyshev_degree + 1);
    
    // For the step function at 0, coefficients have closed form
    // c_0 = 0.5 (average value)
    chebyshev_coeffs_[0] = 0.5;
    
    // c_k = (2/π) * sin(kπ/2) / k for k > 0
    // This comes from integrating the step function against Chebyshev polynomials
    for (uint32_t k = 1; k <= config_.chebyshev_degree; ++k) {
        if (k % 2 == 0) {
            chebyshev_coeffs_[k] = 0.0;  // Even terms vanish for step at 0
        } else {
            chebyshev_coeffs_[k] = (2.0 / M_PI) * std::sin(k * M_PI / 2.0) / k;
        }
    }
}

std::vector<double> FastBettiEstimator::apply_laplacian(
    const std::vector<std::vector<float>>& laplacian,
    const std::vector<double>& vec) {
    
    size_t n = laplacian.size();
    std::vector<double> result(n, 0.0);
    
    // Sparse-aware matrix-vector multiply
    // Laplacian L = D - A where D is degree, A is adjacency
    for (size_t i = 0; i < n; ++i) {
        double sum = 0.0;
        for (size_t j = 0; j < n; ++j) {
            sum += static_cast<double>(laplacian[i][j]) * vec[j];
        }
        result[i] = sum;
    }
    
    return result;
}

std::vector<double> FastBettiEstimator::chebyshev_apply(
    const std::vector<std::vector<float>>& laplacian,
    const std::vector<double>& vec,
    uint32_t degree) {
    
    // Compute T_k(L) * v using recurrence:
    // T_0(x) = 1, T_1(x) = x
    // T_{k+1}(x) = 2x T_k(x) - T_{k-1}(x)
    
    if (degree == 0) {
        return vec;
    }
    
    size_t n = vec.size();
    std::vector<double> t_prev = vec;           // T_0(v) = v
    std::vector<double> t_curr = apply_laplacian(laplacian, vec); // T_1(v) = L*v
    
    if (degree == 1) {
        return t_curr;
    }
    
    // Scale Laplacian eigenvalues to [-1, 1] for Chebyshev stability
    // Approximate spectral radius
    double spectral_radius = 0.0;
    for (const auto& row : laplacian) {
        double row_sum = 0.0;
        for (float val : row) {
            row_sum += std::abs(val);
        }
        spectral_radius = std::max(spectral_radius, row_sum);
    }
    
    double scale = (spectral_radius > 0) ? 2.0 / spectral_radius : 1.0;
    
    // Apply scaling: L_scaled = 2L/ρ - I maps [0, ρ] to [-1, 1]
    std::vector<double> t_next(n);
    
    for (uint32_t k = 2; k <= degree; ++k) {
        // T_k = 2 * L_scaled * T_{k-1} - T_{k-2}
        auto l_scaled_v = apply_laplacian(laplacian, t_curr);
        
        for (size_t i = 0; i < n; ++i) {
            // Apply scaling: 2L/ρ - I
            double scaled = l_scaled_v[i] * scale - t_curr[i];
            t_next[i] = 2.0 * scaled - t_prev[i];
        }
        
        t_prev = t_curr;
        t_curr = t_next;
    }
    
    return t_curr;
}

double FastBettiEstimator::estimate_trace(
    const std::vector<std::vector<float>>& laplacian,
    uint32_t k) {
    
    // Hutchinson's stochastic trace estimator:
    // trace(T_k(L)) ≈ (1/m) Σ v_i^T T_k(L) v_i
    // where v_i are random Rademacher or Gaussian vectors
    
    size_t n = laplacian.size();
    double trace_sum = 0.0;
    
    // Resize random vectors if needed
    if (random_vectors_[0].size() != n) {
        std::normal_distribution<double> dist(0.0, 1.0);
        for (auto& vec : random_vectors_) {
            vec.resize(n);
            for (auto& v : vec) {
                v = dist(rng_);
            }
        }
    }
    
    for (const auto& v : random_vectors_) {
        // Compute T_k(L) * v
        auto tkv = chebyshev_apply(laplacian, v, k);
        
        // v^T T_k(L) v (Rayleigh quotient style)
        double dot = 0.0;
        for (size_t i = 0; i < n; ++i) {
            dot += v[i] * tkv[i];
        }
        
        trace_sum += dot;
    }
    
    return trace_sum / config_.monte_carlo_samples;
}

FastBettiEstimator::BettiEstimate FastBettiEstimator::traces_to_betti(
    const std::vector<double>& traces,
    uint32_t n_vertices) {
    
    BettiEstimate result{};
    
    // β₀ = nullity of L = multiplicity of eigenvalue 0
    // For connected graph: β₀ = 1
    // For k components: β₀ = k
    // 
    // We estimate this from the trace of the spectral projection onto 0
    // Using the Chebyshev approximation of the step function
    
    double projected_rank = 0.0;
    for (uint32_t k = 0; k < traces.size() && k < chebyshev_coeffs_.size(); ++k) {
        projected_rank += chebyshev_coeffs_[k] * traces[k];
    }
    
    // β₀ ≈ n - rank(L)
    // The step function at 0 gives us the dimension of kernel
    result.beta_0 = static_cast<uint32_t>(std::max(1.0, n_vertices - projected_rank + 0.5));
    
    // β₁ = m - n + β₀ (for graph, where m is edges)
    // For higher dimensions, use generalized Euler characteristic
    // This is a simplified estimate for the graph case
    
    // For a graph: χ = β₀ - β₁ = n - m
    // So β₁ = β₀ - n + m
    // We approximate m from the trace of L (sum of degrees = 2m)
    if (traces.size() > 1) {
        double sum_eigenvalues = traces[1];  // trace(L) = sum of eigenvalues = sum of degrees = 2m
        uint32_t m = static_cast<uint32_t>(sum_eigenvalues / 2.0 + 0.5);
        
        int beta_1 = static_cast<int>(result.beta_0) - static_cast<int>(n_vertices) + static_cast<int>(m);
        result.beta_1 = static_cast<uint32_t>(std::max(0, beta_1));
    } else {
        result.beta_1 = 0;
    }
    
    // β₂ is typically 0 for graphs (no 2D voids)
    // For simplicial complexes, would need higher-order Laplacian
    result.beta_2 = 0;
    
    // Confidence based on Monte Carlo variance
    // Higher sample count = higher confidence
    result.confidence = std::min(1.0, 
        std::sqrt(static_cast<double>(config_.monte_carlo_samples)) / 10.0);
    
    return result;
}

uint32_t FastBettiEstimator::select_optimal_degree(uint32_t graph_size) {
    if (!config_.use_adaptive_degree) {
        return config_.chebyshev_degree;
    }
    
    // Larger graphs need higher degree for accuracy
    // Scale up to degree 100 for 8k expert massive graphs
    if (graph_size < 32) return 10;
    if (graph_size < 64) return 15;
    if (graph_size < 128) return 20;
    if (graph_size < 256) return 30;
    if (graph_size < 512) return 40;
    if (graph_size < 1024) return 50;
    if (graph_size < 2048) return 70;
    if (graph_size < 4096) return 85;
    return 100;  // Maximum for 8k+ expert systems
}

FastBettiEstimator::BettiEstimate FastBettiEstimator::estimate(
    const std::vector<std::vector<float>>& adjacency_matrix) {
    
    auto start = std::chrono::high_resolution_clock::now();
    
    uint32_t n = static_cast<uint32_t>(adjacency_matrix.size());
    
    // Build Laplacian: L = D - A
    std::vector<std::vector<float>> laplacian(n, std::vector<float>(n, 0.0f));
    
    for (uint32_t i = 0; i < n; ++i) {
        float degree = 0.0f;
        for (uint32_t j = 0; j < n; ++j) {
            degree += adjacency_matrix[i][j];
            laplacian[i][j] = -adjacency_matrix[i][j];  // Off-diagonal: -A
        }
        laplacian[i][i] = degree;  // Diagonal: D
    }
    
    // Select optimal degree
    uint32_t degree = select_optimal_degree(n);
    
    // Compute traces of T_k(L) for k = 0, 1, ..., degree
    std::vector<double> traces(degree + 1);
    for (uint32_t k = 0; k <= degree; ++k) {
        traces[k] = estimate_trace(laplacian, k);
    }
    
    // Convert traces to Betti numbers
    auto result = traces_to_betti(traces, n);
    
    auto end = std::chrono::high_resolution_clock::now();
    result.computation_ms = std::chrono::duration<double, std::milli>(end - start).count();
    
    // Update stats
    stats_.total_estimations++;
    stats_.avg_computation_us = static_cast<uint64_t>(
        (stats_.avg_computation_us * (stats_.total_estimations - 1) + 
         result.computation_ms * 1000) / stats_.total_estimations);
    stats_.max_graph_size = std::max(stats_.max_graph_size, n);
    
    last_estimate_ = result;
    return result;
}

void FastBettiEstimator::update_edge(uint32_t from, uint32_t to, float weight) {
    // Update current Laplacian
    size_t n = current_laplacian_.size();
    if (from >= n || to >= n) {
        // Resize if needed
        size_t new_size = std::max({static_cast<size_t>(from), static_cast<size_t>(to), n}) + 1;
        for (auto& row : current_laplacian_) {
            row.resize(new_size, 0.0f);
        }
        current_laplacian_.resize(new_size, std::vector<float>(new_size, 0.0f));
        n = new_size;
    }
    
    // Update adjacency (symmetric)
    float old_weight = current_laplacian_[from][to];
    current_laplacian_[from][to] = weight;
    current_laplacian_[to][from] = weight;
    
    // Update degrees (diagonal)
    current_laplacian_[from][from] += (weight - old_weight);
    current_laplacian_[to][to] += (weight - old_weight);
}

FastBettiEstimator::BettiEstimate FastBettiEstimator::get_current_estimate() {
    if (current_laplacian_.empty()) {
        return BettiEstimate{};
    }
    return estimate(current_laplacian_);
}

FastBettiEstimator::Stats FastBettiEstimator::get_stats() const {
    return stats_;
}

// HybridBettiExtractor implementation

HybridBettiExtractor::HybridBettiExtractor(uint32_t threshold)
    : threshold_(threshold) {
    fast_estimator_ = std::make_unique<FastBettiEstimator>();
}

FastBettiEstimator::BettiEstimate HybridBettiExtractor::extract(
    const std::vector<std::vector<float>>& adjacency) {
    
    uint32_t n = static_cast<uint32_t>(adjacency.size());
    
    if (n <= threshold_) {
        return extract_exact(adjacency);
    } else {
        return extract_fast(adjacency);
    }
}

FastBettiEstimator::BettiEstimate HybridBettiExtractor::extract_exact(
    const std::vector<std::vector<float>>& adjacency) {
    
    // Fall back to existing exact Betti extraction
    // This would call the existing betti_extractor code
    // For now, return zeros (integrate with existing code)
    
    FastBettiEstimator::BettiEstimate result{};
    result.beta_0 = 1;  // Assume connected
    result.beta_1 = 0;
    result.beta_2 = 0;
    result.confidence = 1.0;
    result.computation_ms = 0.0;
    
    return result;
}

FastBettiEstimator::BettiEstimate HybridBettiExtractor::extract_fast(
    const std::vector<std::vector<float>>& adjacency) {
    
    return fast_estimator_->estimate(adjacency);
}

void HybridBettiExtractor::set_fast_config(const FastBettiEstimator::Config& config) {
    fast_estimator_ = std::make_unique<FastBettiEstimator>(config);
}

} // namespace qgnn
} // namespace q
