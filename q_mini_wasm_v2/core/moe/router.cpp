#include "router.hpp"
#include <algorithm>
#include <numeric>
#include <cmath>
#include <stdexcept>

namespace q_mini_wasm_v2::core::moe {

MoERouter::MoERouter(const ExpertConfig& config)
    : config_(config)
    , expert_weights_(config.total_experts)
    , routing_weights_(config.total_experts, std::vector<ternary::Trit>(config.routing_qutrits, ternary::Trit::ZERO))
{
    // Initialize routing weights randomly
    for (auto& row : routing_weights_) {
        for (auto& val : row) {
            int r = rand() % 3;
            val = static_cast<ternary::Trit>(r - 1);  // Map 0,1,2 to -1,0,1
        }
    }
}

MoERouter::~MoERouter() = default;

// ============================================================================
// Routing Operations
// ============================================================================

std::vector<size_t> MoERouter::route_topk(const std::vector<ternary::Trit>& input) {
    auto logits = compute_routing_logits(input);
    return select_topk(logits, config_.active_experts);
}

std::vector<double> MoERouter::compute_routing_logits(const std::vector<ternary::Trit>& input) {
    std::vector<double> logits(config_.total_experts, 0.0);
    
    for (size_t e = 0; e < config_.total_experts; ++e) {
        // Compute tropical inner product between input and routing weights
        std::vector<double> input_double(input.size());
        std::vector<double> weights_double(routing_weights_[e].size());
        
        for (size_t i = 0; i < input.size(); ++i) {
            input_double[i] = static_cast<double>(input[i]);
        }
        for (size_t i = 0; i < routing_weights_[e].size(); ++i) {
            weights_double[i] = static_cast<double>(routing_weights_[e][i]);
        }
        
        logits[e] = tropical_inner_product(input_double, weights_double);
    }
    
    return tropical_softmax(logits);
}

std::vector<double> MoERouter::entangled_route(
    stabilizer::StabilizerTableau& tableau,
    const std::vector<ternary::Trit>& input
) {
    // Apply entanglement gates based on input
    for (size_t i = 0; i < std::min(input.size(), tableau.num_qutrits()); ++i) {
        if (input[i] == ternary::Trit::POSITIVE) {
            tableau.apply_hadamard(i);
        } else if (input[i] == ternary::Trit::NEGATIVE) {
            tableau.apply_phase(i);
        }
    }
    
    // Apply CSUM gates for entanglement between routing qutrits
    for (size_t i = 0; i + 1 < config_.routing_qutrits; ++i) {
        tableau.apply_csum(i, i + 1);
    }
    
    // Measure qutrits to get routing decisions
    auto measurements = tableau.measure_all();
    
    // Convert measurements to routing probabilities
    std::vector<double> probs(config_.total_experts, 0.0);
    for (size_t i = 0; i < std::min(measurements.size(), config_.total_experts); ++i) {
        probs[i] = (measurements[i] + 1.0) / 3.0;  // Map -1,0,1 to 0, 1/3, 2/3
    }
    
    // Normalize
    double sum = std::accumulate(probs.begin(), probs.end(), 0.0);
    if (sum > 0) {
        for (auto& p : probs) {
            p /= sum;
        }
    }
    
    return probs;
}

// ============================================================================
// Tropical Geometry Operations
// ============================================================================

double MoERouter::tropical_inner_product(
    const std::vector<double>& a,
    const std::vector<double>& b
) {
    if (a.size() != b.size()) {
        throw std::invalid_argument("Vector sizes must match for tropical inner product");
    }
    
    // Tropical inner product: max_i(a_i + b_i)
    // Based on research: "Sparsity is Combinatorial Depth: Quantifying MoE 
    // Expressivity via Tropical Geometry"
    //
    // The tropical inner product is defined as:
    // ⟨a, b⟩_trop = max_i(a_i ⊗ b_i) = max_i(a_i + b_i)
    //
    // This operation is the max-plus algebra equivalent of the standard dot product.
    // It satisfies the tropical triangle inequality and induces a tropical metric.
    
    double result = -std::numeric_limits<double>::infinity();
    
    for (size_t i = 0; i < a.size(); ++i) {
        // Tropical multiplication: a ⊗ b = a + b
        double tropical_product = tropical_multiply(a[i], b[i]);
        
        // Tropical addition: a ⊕ b = max(a, b)
        result = tropical_add(result, tropical_product);
    }
    
    // Handle case where all products were -infinity
    if (result == -std::numeric_limits<double>::infinity()) {
        return 0.0;
    }
    
    return result;
}

size_t MoERouter::compute_hypersimplex_capacity() const {
    // Hypersimplex capacity = C(total_experts, active_experts)
    return binomial_coefficient(config_.total_experts, config_.active_experts);
}

// ============================================================================
// Load Balancing
// ============================================================================

double MoERouter::compute_load_balance_loss(const std::vector<size_t>& expert_counts) const {
    // KL-divergence from uniform distribution
    double uniform_prob = 1.0 / config_.total_experts;
    double total_count = std::accumulate(expert_counts.begin(), expert_counts.end(), 0.0);
    
    if (total_count == 0) {
        return 0.0;
    }
    
    double kl_div = 0.0;
    for (size_t i = 0; i < config_.total_experts; ++i) {
        double p = expert_counts[i] / total_count;
        if (p > 0) {
            kl_div += p * std::log(p / uniform_prob);
        }
    }
    
    return kl_div;
}

std::vector<double> MoERouter::apply_load_balancing(
    const std::vector<double>& logits,
    const std::vector<size_t>& expert_counts
) const {
    std::vector<double> balanced_logits = logits;
    
    double total_count = std::accumulate(expert_counts.begin(), expert_counts.end(), 0.0);
    if (total_count == 0) {
        return balanced_logits;
    }
    
    // Apply penalty for overused experts
    for (size_t i = 0; i < config_.total_experts; ++i) {
        double usage_ratio = expert_counts[i] / total_count;
        double expected_ratio = 1.0 / config_.total_experts;
        
        // Penalize experts that are used more than expected
        if (usage_ratio > expected_ratio) {
            balanced_logits[i] -= (usage_ratio - expected_ratio) * 0.1;
        }
    }
    
    return balanced_logits;
}

void MoERouter::update_expert_weights(size_t expert_idx, const std::vector<std::vector<ternary::Trit>>& weights) {
    if (expert_idx >= config_.total_experts) {
        throw std::out_of_range("Expert index out of range");
    }
    expert_weights_[expert_idx] = weights;
}

// ============================================================================
// Internal Helpers
// ============================================================================

std::vector<size_t> MoERouter::select_topk(const std::vector<double>& logits, size_t k) const {
    std::vector<size_t> indices(logits.size());
    std::iota(indices.begin(), indices.end(), 0);
    
    // Partial sort to get Top-K
    std::partial_sort(
        indices.begin(),
        indices.begin() + k,
        indices.end(),
        [&logits](size_t a, size_t b) {
            return logits[a] > logits[b];
        }
    );
    
    indices.resize(k);
    return indices;
}

std::vector<double> MoERouter::tropical_softmax(const std::vector<double>& logits) const {
    // Tropical softmax: normalize using tropical operations
    double max_logit = *std::max_element(logits.begin(), logits.end());
    
    std::vector<double> result(logits.size());
    for (size_t i = 0; i < logits.size(); ++i) {
        // Tropical division is subtraction
        result[i] = std::exp(logits[i] - max_logit);
    }
    
    // Normalize
    double sum = std::accumulate(result.begin(), result.end(), 0.0);
    if (sum > 0) {
        for (auto& r : result) {
            r /= sum;
        }
    }
    
    return result;
}

size_t MoERouter::factorial(size_t n) {
    if (n <= 1) return 1;
    size_t result = 1;
    for (size_t i = 2; i <= n; ++i) {
        result *= i;
    }
    return result;
}

size_t MoERouter::binomial_coefficient(size_t n, size_t k) {
    if (k > n) return 0;
    if (k == 0 || k == n) return 1;
    
    // Use multiplicative formula to avoid overflow
    k = std::min(k, n - k);
    size_t result = 1;
    for (size_t i = 0; i < k; ++i) {
        result = result * (n - i) / (i + 1);
    }
    return result;
}

std::unique_ptr<MoERouter> create_moe_router(const ExpertConfig& config) {
    return std::make_unique<MoERouter>(config);
}

} // namespace q_mini_wasm_v2::core::moe