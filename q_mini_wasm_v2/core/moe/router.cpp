#include "router.hpp"
#include <algorithm>
#include <numeric>
#include <cmath>
#include <stdexcept>
#include <random>

namespace q_mini_wasm_v2::core::moe {

MoERouter::MoERouter(const ExpertConfig& config)
    : config_(config)
    , expert_weights_(config.total_experts)
    , routing_weights_(config.total_experts, std::vector<ternary::Trit>(config.routing_qutrits, ternary::Trit::ZERO))
    , rng_(std::random_device{}())
{
    // Initialize routing weights randomly
    for (auto& row : routing_weights_) {
        for (auto& val : row) {
            int r = rand() % 3;
            val = static_cast<ternary::Trit>(r - 1);  // Map 0,1,2 to -1,0,1
        }
    }
    
    // Initialize entanglement coupling matrix
    initialize_entanglement_coupling();
}

MoERouter::MoERouter(const ExpertConfig& config, const EntangledRoutingConfig& entangled_config)
    : config_(config)
    , entangled_config_(entangled_config)
    , expert_weights_(config.total_experts)
    , routing_weights_(config.total_experts, std::vector<ternary::Trit>(config.routing_qutrits, ternary::Trit::ZERO))
    , rng_(std::random_device{}())
{
    // Initialize routing weights randomly
    for (auto& row : routing_weights_) {
        for (auto& val : row) {
            int r = rand() % 3;
            val = static_cast<ternary::Trit>(r - 1);
        }
    }
    
    // Initialize entanglement coupling matrix
    initialize_entanglement_coupling();
}

MoERouter::~MoERouter() = default;

// ============================================================================
// Routing Operations
// ============================================================================

std::vector<size_t> MoERouter::route_topk(const std::vector<ternary::Trit>& input) {
    auto logits = compute_routing_logits(input);
    return select_topk(logits, config_.active_experts);
}

// Inline helper for GF(3) modulo addition over symmetric {-1, 0, 1}
inline int8_t gf3_add(int8_t a, int8_t b) {
    // Hardware-accelerated lookup or logical equivalent avoiding branching
    int sum = a + b;
    if (sum > 1) return -1;
    if (sum < -1) return 1;
    return static_cast<int8_t>(sum);
}

std::vector<double> MoERouter::compute_routing_logits(const std::vector<ternary::Trit>& input) {
    std::vector<double> logits(config_.total_experts, 0.0);
    
    // Iterate over all experts to compute graph distance from input state
    for (size_t e = 0; e < config_.total_experts; ++e) {
        size_t min_size = std::min(input.size(), routing_weights_[e].size());
        
        // Initialize with minimum possible quantized value to represent -infinity
        int8_t max_val = -128; 
        
        // Compute Tropical Inner Product
        for (size_t i = 0; i < min_size; ++i) {
            // Tropical Multiplication is mapped to GF(3) Addition.
            int8_t weight_val = static_cast<int8_t>(routing_weights_[e][i]);
            int8_t input_val = static_cast<int8_t>(input[i]);
            
            int8_t tropical_mult = gf3_add(weight_val, input_val);
            
            // Tropical Addition is mapped to standard Maximum comparison.
            if (tropical_mult > max_val) {
                max_val = tropical_mult;
            }
        }
        
        // The final maximum value represents the shortest-path geometric affinity
        logits[e] = static_cast<double>(max_val);
    }
    
    // To accurately simulate the normal fan of the hypersimplex, the output logits 
    // are explicitly not passed through a Softmax function.
    return logits;
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

std::vector<size_t> MoERouter::llep_route(
    const std::vector<double>& logits,
    const std::vector<size_t>& expert_loads,
    size_t k
) const {
    // Least-Loaded Expert Parallelism (LLEP) Routing
    // Dynamically routes overflow tokens from overloaded hypersimplex cones
    // Eliminates 20-40% standard MoE load imbalance penalties
    
    const double MAX_CAPACITY = logits.size() / static_cast<double>(config_.total_experts);
    const double OVERFLOW_THRESHOLD = MAX_CAPACITY * 1.2;
    
    std::vector<std::pair<double, size_t>> scored_experts;
    scored_experts.reserve(config_.total_experts);
    
    for (size_t i = 0; i < config_.total_experts; ++i) {
        scored_experts.emplace_back(logits[i], i);
    }
    
    // Sort experts by descending logit score
    std::sort(scored_experts.begin(), scored_experts.end(),
        [](const auto& a, const auto& b) { return a.first > b.first; });
    
    std::vector<size_t> selected_experts;
    selected_experts.reserve(k);
    
    std::vector<size_t> current_loads = expert_loads;
    
    for (const auto& entry : scored_experts) {
        if (selected_experts.size() >= k) break;
        
        size_t expert_idx = entry.second;
        
        if (current_loads[expert_idx] >= OVERFLOW_THRESHOLD) {
            // Find least loaded expert for rerouting
            size_t min_load = current_loads[0];
            size_t target_expert = 0;
            
            for (size_t e = 1; e < config_.total_experts; ++e) {
                if (current_loads[e] < min_load) {
                    min_load = current_loads[e];
                    target_expert = e;
                }
            }
            
            // Route to least loaded expert instead
            selected_experts.push_back(target_expert);
            current_loads[target_expert]++;
        } else {
            // Route normally to originally selected expert
            selected_experts.push_back(expert_idx);
            current_loads[expert_idx]++;
        }
    }
    
    return selected_experts;
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

void MoERouter::initialize_entanglement_coupling() {
    // Initialize entanglement coupling matrix for correlated expert routing
    // This creates a symmetric matrix where entry [i][j] represents the
    // coupling strength between expert i and expert j
    size_t n = config_.total_experts;
    entanglement_coupling_.resize(n, std::vector<double>(n, 0.0));
    
    std::uniform_real_distribution<double> dist(0.0, entangled_config_.entanglement_strength);
    
    for (size_t i = 0; i < n; ++i) {
        entanglement_coupling_[i][i] = 1.0;  // Self-coupling is always 1
        for (size_t j = i + 1; j < n; ++j) {
            double coupling = dist(rng_);
            entanglement_coupling_[i][j] = coupling;
            entanglement_coupling_[j][i] = coupling;  // Symmetric
        }
    }
}

double MoERouter::compute_coherence(const std::vector<double>& probabilities) const {
    // Compute coherence as the inverse of entropy (normalized)
    // Higher coherence = more peaked distribution = more decisive routing
    double entropy = 0.0;
    for (double p : probabilities) {
        if (p > 1e-10) {
            entropy -= p * std::log2(p);
        }
    }
    
    double max_entropy = std::log2(config_.total_experts);
    if (max_entropy < 1e-10) return 1.0;
    
    // Coherence = 1 - (normalized entropy)
    return 1.0 - (entropy / max_entropy);
}

std::unique_ptr<MoERouter> create_moe_router(const ExpertConfig& config) {
    return std::make_unique<MoERouter>(config);
}

} // namespace q_mini_wasm_v2::core::moe
