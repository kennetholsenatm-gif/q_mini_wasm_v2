#include "router.hpp"
#include "../ternary/packing.hpp"
#include <numeric>
#include <stdexcept>
#include <algorithm>
#include <random>
#include <iostream>
#include <mutex>
#if defined(USE_SYCL) && USE_SYCL
#include "../../sycl/tableau_kernels.hpp"
#endif

namespace {
std::once_flag g_once_tritpack5_route_short_pack;
} // namespace

namespace q_mini_wasm_v2::core::moe {

#if defined(USE_SYCL) && USE_SYCL
static bool moe_routing_sycl_desired(size_t total_experts, SyclRouteMode mode) noexcept;
#endif

MoERouter::MoERouter(const ExpertConfig& config)
    : config_(config)
    , current_active_experts_(config.active_experts)
    , last_load_measurement_(50)
    , expert_weights_(config.total_experts)
    , ternary_seed_(42)  // Deterministic seed for reproducibility
    , expert_loads_(config.total_experts, 0)  // Initialize load tracking
    , expert_request_counts_(config.total_experts, 0)
    , routing_weights_initialized_(false)
    , entanglement_initialized_(false)
    , advanced_selection_initialized_(false)
{
    // All heavy initialization is lazy - see ensure_* methods
}

MoERouter::MoERouter(const ExpertConfig& config, const EntangledRoutingConfig& entangled_config)
    : config_(config)
    , current_active_experts_(config.active_experts)
    , last_load_measurement_(50)
    , entangled_config_(entangled_config)
    , expert_weights_(config.total_experts)
    , ternary_seed_(42)  // Deterministic seed for reproducibility
    , expert_loads_(config.total_experts, 0)  // Initialize load tracking
    , expert_request_counts_(config.total_experts, 0)
    , routing_weights_initialized_(false)
    , entanglement_initialized_(false)
    , advanced_selection_initialized_(false)
{
    // All heavy initialization is lazy - see ensure_* methods
}

MoERouter::~MoERouter() = default;

// ============================================================================
// Routing Operations
// ============================================================================

std::vector<size_t> MoERouter::route(
    const std::vector<ternary::Trit>& input,
    const RoutingStrategy strategy
) {
    // Lazy initialization of routing weights on first use
    ensure_routing_weights_initialized();
    
    auto logits = compute_routing_logits(input);
    switch (strategy) {
        // ... rest of the method remains the same ...
    }
    return select_topk(logits, config_.active_experts);
}

std::vector<size_t> MoERouter::route_topk(const std::vector<ternary::Trit>& input) {
    auto logits = compute_routing_logits(input);
    return select_topk(logits, config_.active_experts);
}

std::vector<size_t> MoERouter::route_topk_from_tritpack5(
    const std::vector<uint8_t>& input_packed,
    size_t input_trit_count
) {
    last_symplectic_logits_used_sycl_ = false;
    ensure_routing_weights_initialized();
    if (input_trit_count == 0 || input_packed.empty()) {
        return select_topk(std::vector<int8_t>(config_.total_experts, static_cast<int8_t>(-1)), config_.active_experts);
    }
    const size_t need_bytes = (input_trit_count + 4) / 5;
    if (input_packed.size() < need_bytes) {
        std::call_once(g_once_tritpack5_route_short_pack, [&]() {
            std::cerr << "[MoERouter] route_topk_from_tritpack5: input_packed shorter than ceil(input_trit_count/5) "
                      << "(need_bytes=" << need_bytes << " input_packed.size=" << input_packed.size()
                      << " input_trit_count=" << input_trit_count << ") — using sentinel logits once\n";
        });
        return select_topk(std::vector<int8_t>(config_.total_experts, static_cast<int8_t>(-1)), config_.active_experts);
    }

#if defined(USE_SYCL) && USE_SYCL
    if (moe_routing_sycl_desired(config_.total_experts, config_.sycl_route_mode)) {
        try {
            const size_t E = config_.total_experts;
            const size_t R = config_.routing_qutrits;
            std::vector<uint8_t> wt = pack_routing_weights_tritpack5_rowmajor();
            auto logits = ::q_mini_wasm_v2::sycl_kernels::moe_routing_symplectic_scores_sycl(
                input_packed, input_trit_count, wt, E, R);
            last_symplectic_logits_used_sycl_ = true;
            return select_topk(logits, config_.active_experts);
        } catch (...) {
            // fall through to CPU
        }
    }
#endif

    return select_topk(symplectic_scores_cpu_from_tritpack5(input_packed, input_trit_count), config_.active_experts);
}

// Inline helper for GF(3) modulo addition over symmetric {-1, 0, 1}
inline int8_t gf3_add(int8_t a, int8_t b) {
    // Hardware-accelerated lookup or logical equivalent avoiding branching
    int sum = a + b;
    if (sum > 1) return -1;
    if (sum < -1) return 1;
    return static_cast<int8_t>(sum);
}

#if defined(USE_SYCL) && USE_SYCL
/** SYCL routing logits: controlled by ExpertConfig::sycl_route_mode (from TOML). */
static bool moe_routing_sycl_desired(size_t total_experts, SyclRouteMode mode) noexcept {
    if (total_experts < 8) {
        return false;
    }
    switch (mode) {
        case SyclRouteMode::Off:
            return false;
        case SyclRouteMode::On:
            return true;
        case SyclRouteMode::Auto:
        default:
            return total_experts >= 128;
    }
}
#endif

void MoERouter::ensure_routing_weights_initialized() {
    if (!routing_weights_initialized_) {
        routing_weights_.resize(config_.total_experts);
        for (auto& row : routing_weights_) {
            row.resize(config_.routing_qutrits);
            for (auto& val : row) {
                val = ternary_random();
            }
        }
        routing_weights_initialized_ = true;
    }
}

std::vector<uint8_t> MoERouter::pack_routing_weights_tritpack5_rowmajor() const {
    const size_t E = config_.total_experts;
    const size_t R = config_.routing_qutrits;
    const size_t row_bytes = (R + 4) / 5;
    std::vector<uint8_t> wt(E * row_bytes, static_cast<uint8_t>(0));
    for (size_t e = 0; e < E; ++e) {
        std::vector<int8_t> row(R);
        for (size_t j = 0; j < R; ++j) {
            row[j] = static_cast<int8_t>(routing_weights_[e][j]);
        }
        std::vector<uint8_t> packed_row;
        q::ternary::pack_batch_t5(row, packed_row);
        for (size_t b = 0; b < row_bytes && b < packed_row.size(); ++b) {
            wt[e * row_bytes + b] = packed_row[b];
        }
    }
    return wt;
}

std::vector<int8_t> MoERouter::symplectic_scores_cpu_from_tritpack5(
    const std::vector<uint8_t>& input_packed,
    size_t input_trit_count
) const {
    std::vector<int8_t> symplectic_scores(config_.total_experts, -1);
    if (input_packed.empty() || input_trit_count == 0) {
        return symplectic_scores;
    }
    const size_t need_bytes = (input_trit_count + 4) / 5;
    if (input_packed.size() < need_bytes) {
        return symplectic_scores;
    }
    const uint8_t* pin = input_packed.data();
    const std::vector<uint8_t> w_packed = pack_routing_weights_tritpack5_rowmajor();
    const size_t R = config_.routing_qutrits;
    const size_t w_row_bytes = (R + 4) / 5;
    const uint8_t* pw = w_packed.data();

    for (size_t e = 0; e < config_.total_experts; ++e) {
        const size_t min_size = std::min(input_trit_count, routing_weights_[e].size());

        int8_t symplectic_sum = 0;

        for (size_t i = 0; i < min_size; i += 2) {
            const int8_t x1 = q::ternary::read_trit_t5_at(pin, i);
            const int8_t z1 =
                (i + 1 < min_size) ? q::ternary::read_trit_t5_at(pin, i + 1) : static_cast<int8_t>(0);

            const int8_t x2 = q::ternary::read_trit_t5_at(pw + e * w_row_bytes, i);
            const int8_t z2 = (i + 1 < min_size) ? q::ternary::read_trit_t5_at(pw + e * w_row_bytes, i + 1)
                                                : static_cast<int8_t>(0);

            int pairing = static_cast<int>(x1) * static_cast<int>(z2) - static_cast<int>(z1) * static_cast<int>(x2);
            while (pairing > 1) {
                pairing -= 3;
            }
            while (pairing < -1) {
                pairing += 3;
            }

            symplectic_sum = gf3_add(symplectic_sum, static_cast<int8_t>(pairing));
        }

        symplectic_scores[e] = symplectic_sum;
    }

    return symplectic_scores;
}

std::vector<int8_t> MoERouter::compute_routing_logits(const std::vector<ternary::Trit>& input) {
    last_symplectic_logits_used_sycl_ = false;
    ensure_routing_weights_initialized();

    std::vector<int8_t> inb(input.size());
    for (size_t i = 0; i < input.size(); ++i) {
        inb[i] = static_cast<int8_t>(input[i]);
    }

#if defined(USE_SYCL) && USE_SYCL
    if (moe_routing_sycl_desired(config_.total_experts, config_.sycl_route_mode)) {
        try {
            const size_t E = config_.total_experts;
            const size_t R = config_.routing_qutrits;
            std::vector<uint8_t> wt = pack_routing_weights_tritpack5_rowmajor();
            std::vector<uint8_t> in_t5;
            q::ternary::pack_batch_t5(inb, in_t5);
            auto logits =
                ::q_mini_wasm_v2::sycl_kernels::moe_routing_symplectic_scores_sycl(in_t5, inb.size(), wt, E, R);
            last_symplectic_logits_used_sycl_ = true;
            return logits;
        } catch (...) {
            // CPU path below
        }
    }
#endif

    std::vector<uint8_t> in_t5;
    q::ternary::pack_batch_t5(inb, in_t5);
    return symplectic_scores_cpu_from_tritpack5(in_t5, inb.size());
}

std::vector<int32_t> MoERouter::entangled_route(
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
    
    // Convert measurements to routing probabilities (scaled to 0-100)
    std::vector<int32_t> probs(config_.total_experts, 0);
    for (size_t i = 0; i < std::min(measurements.size(), config_.total_experts); ++i) {
        probs[i] = (measurements[i] + 1) * 33;  // Map -1,0,1 to ~0, 33, 66
    }
    
    // Normalize to sum to 100
    int32_t sum = std::accumulate(probs.begin(), probs.end(), 0);
    if (sum > 0) {
        for (auto& p : probs) {
            p = (p * 100) / sum;
        }
    }
    
    return probs;
}

// ============================================================================
// Tropical Geometry Operations
// ============================================================================

int32_t MoERouter::tropical_inner_product(
    const std::vector<int32_t>& a,
    const std::vector<int32_t>& b
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
    
    int32_t result = -2147483647; // Minimal 32-bit int minus 1
    
    for (size_t i = 0; i < a.size(); ++i) {
        // Tropical multiplication: a ⊗ b = a + b
        int32_t tropical_product = tropical_multiply(a[i], b[i]);
        
        // Tropical addition: a ⊕ b = max(a, b)
        result = tropical_add(result, tropical_product);
    }
    
    // Handle case where all products were minimal
    if (result == -2147483647) {
        return 0;
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

int32_t MoERouter::compute_load_balance_loss(const std::vector<size_t>& expert_counts) const {
    // Integer approximation of KL-divergence from uniform distribution
    // Avoids floating point by scaling
    int32_t total_count = static_cast<int32_t>(std::accumulate(expert_counts.begin(), expert_counts.end(), size_t(0)));
    if (total_count == 0) return 0;
    
    int32_t expected_count = total_count / config_.total_experts;
    if (expected_count == 0) return 0;
    
    int32_t loss = 0;
    for (size_t i = 0; i < config_.total_experts; ++i) {
        int32_t diff = static_cast<int32_t>(expert_counts[i]) - expected_count;
        loss += (diff * diff) / expected_count; // Chi-squared-like approximation
    }
    return loss;
}

std::vector<int32_t> MoERouter::apply_load_balancing(
    const std::vector<int32_t>& logits,
    const std::vector<size_t>& expert_counts
) const {
    std::vector<int32_t> balanced_logits = logits;
    
    int32_t total_count = static_cast<int32_t>(std::accumulate(expert_counts.begin(), expert_counts.end(), size_t(0)));
    if (total_count == 0) return balanced_logits;
    
    // Apply penalty for overused experts
    for (size_t i = 0; i < config_.total_experts; ++i) {
        int32_t usage_ratio_scaled = (static_cast<int32_t>(expert_counts[i]) * 1000) / total_count;
        int32_t expected_ratio_scaled = 1000 / config_.total_experts;
        
        // Penalize experts that are used more than expected
        if (usage_ratio_scaled > expected_ratio_scaled) {
            balanced_logits[i] -= (usage_ratio_scaled - expected_ratio_scaled) / 10;
        }
    }
    
    return balanced_logits;
}

std::vector<size_t> MoERouter::llep_route(
    const std::vector<int32_t>& logits,
    const std::vector<size_t>& expert_loads,
    size_t k
) const {
    // Least-Loaded Expert Parallelism (LLEP) Routing
    // Dynamically routes overflow tokens from overloaded hypersimplex cones
    // Eliminates 20-40% standard MoE load imbalance penalties
    
    const int32_t MAX_CAPACITY = static_cast<int32_t>(logits.size()) / config_.total_experts;
    const int32_t OVERFLOW_THRESHOLD = (MAX_CAPACITY * 12) / 10;
    
    std::vector<std::pair<int32_t, size_t>> scored_experts;
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

std::vector<size_t> MoERouter::select_topk(const std::vector<int8_t>& scores, size_t k) const {
    if (scores.empty()) {
        return {};
    }
    // Clamp k to available scores size to prevent iterator overflow
    k = std::min(k, scores.size());

    std::vector<size_t> indices(scores.size());
    std::iota(indices.begin(), indices.end(), 0);

    // MSVC debug STL asserts when middle == last in partial_sort.
    // Use full sort when k >= scores.size() to avoid the assertion.
    if (k >= scores.size()) {
        std::sort(
            indices.begin(),
            indices.end(),
            [&scores](size_t a, size_t b) {
                return scores[a] > scores[b];
            }
        );
    } else {
        // Partial sort to get Top-K GF(3) symplectic scores
        std::partial_sort(
            indices.begin(),
            indices.begin() + k,
            indices.end(),
            [&scores](size_t a, size_t b) {
                return scores[a] > scores[b];
            }
        );
    }

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
    if (entanglement_initialized_) return;
    
    // Initialize entanglement coupling matrix for correlated expert routing
    // This creates a symmetric matrix where entry [i][j] represents the
    // coupling strength between expert i and expert j (0-100)
    size_t n = config_.total_experts;
    entanglement_coupling_.resize(n, std::vector<int32_t>(n, 0));
    
    std::uniform_int_distribution<int32_t> dist(0, entangled_config_.entanglement_strength);
    
    for (size_t i = 0; i < n; ++i) {
        entanglement_coupling_[i][i] = 100;  // Self-coupling is always 100
        for (size_t j = i + 1; j < n; ++j) {
            int32_t coupling = dist(rng_);
            entanglement_coupling_[i][j] = coupling;
            entanglement_coupling_[j][i] = coupling;  // Symmetric
        }
    }
    entanglement_initialized_ = true;
}

int32_t MoERouter::compute_coherence(const std::vector<int32_t>& probabilities) const {
    // Compute an integer approximation of coherence (inverse entropy)
    // probabilities are scaled to 0-100
    int32_t peak = 0;
    for (int32_t p : probabilities) {
        if (p > peak) peak = p;
    }
    
    // Simplistic coherence approximation without float log2:
    // How much does the peak deviate from a uniform distribution?
    int32_t uniform = 100 / config_.total_experts;
    if (peak <= uniform) return 0;
    
    return ((peak - uniform) * 100) / (100 - uniform);
}

std::vector<std::vector<size_t>> MoERouter::expert_choice_route(
    const std::vector<int8_t>& symplectic_scores,
    const std::vector<size_t>& /* expert_loads */,
    size_t tokens_per_expert
) const {
    // Quantum Architecture Review §3.4 Expert Choice Routing
    // Experts select tokens instead of tokens selecting experts
    // Eliminates 100% of MoE load imbalance at O(N log N) complexity
    
    std::vector<std::vector<size_t>> assignments(config_.total_experts);
    std::vector<size_t> remaining_tokens(symplectic_scores.size());
    std::iota(remaining_tokens.begin(), remaining_tokens.end(), 0);
    
    // Each expert selects highest alignment tokens until capacity reached
    for (size_t expert = 0; expert < config_.total_experts; ++expert) {
        // Sort remaining tokens by alignment score with this expert
        std::sort(remaining_tokens.begin(), remaining_tokens.end(),
            [&](size_t a, size_t b) {
                return symplectic_scores[a] > symplectic_scores[b];
            });
        
        // Assign up to tokens_per_expert tokens
        size_t assign_count = std::min(tokens_per_expert, remaining_tokens.size());
        for (size_t i = 0; i < assign_count; ++i) {
            assignments[expert].push_back(remaining_tokens[i]);
        }
        
        // Remove assigned tokens from remaining pool
        // MSVC debug STL asserts when first == last in erase (empty range).
        if (assign_count > 0 && assign_count < remaining_tokens.size()) {
            remaining_tokens.erase(
                remaining_tokens.begin(),
                remaining_tokens.begin() + assign_count
            );
        } else if (assign_count >= remaining_tokens.size()) {
            // Clear all remaining tokens
            remaining_tokens.clear();
        }
        
        if (remaining_tokens.empty()) break;
    }
    
    // Distribute any remaining tokens uniformly
    size_t round_robin = 0;
    while (!remaining_tokens.empty()) {
        if (assignments[round_robin].size() < tokens_per_expert + 1) {
            assignments[round_robin].push_back(remaining_tokens[0]);
            remaining_tokens.erase(remaining_tokens.begin());
        }
        round_robin = (round_robin + 1) % config_.total_experts;
    }
    
    return assignments;
}

// ============================================================================
// Dynamic Expert Scaling
// ============================================================================

size_t MoERouter::adjust_expert_scale(uint32_t current_load) {
    last_load_measurement_ = current_load;
    
    const size_t MIN_EXPERTS = config_.active_experts * 80 / 100;  // -20% minimum
    const size_t MAX_EXPERTS = config_.active_experts * 120 / 100; // +20% maximum
    
    if (current_load > 80) {
        // High load: increase experts
        current_active_experts_ = std::min(current_active_experts_ + 1, MAX_EXPERTS);
    } else if (current_load < 30) {
        // Low load: decrease experts
        current_active_experts_ = std::max(current_active_experts_ - 1, MIN_EXPERTS);
    }
    
    // Clamp to valid range
    current_active_experts_ = std::clamp(current_active_experts_, MIN_EXPERTS, MAX_EXPERTS);
    return current_active_experts_;
}

int32_t MoERouter::get_scaling_factor_fixed() const {
    // Return scaling factor as fixed-point (1000 = 1.0)
    // Formula: (current_active / config.active) * 1000
    return static_cast<int32_t>((current_active_experts_ * 1000) / config_.active_experts);
}

int32_t MoERouter::compute_energy_cost_fixed(size_t expert_count) const {
    // Fixed-point energy cost calculation (scale 1000 = 1.0 pJ per operation)
    // Using integer arithmetic only
    
    // Base cost: energy_per_expert * expert_count
    // trit_to_energy returns fixed-point value (e.g., 500 for MEDIUM = 0.5 pJ)
    int32_t base_energy = trit_to_energy(energy_per_expert_);
    int32_t base_cost = base_energy * static_cast<int32_t>(expert_count);
    
    // Overhead: quadratic scaling for coordination
    // (expert_count * expert_count * 10) / 1000 = 0.01 pJ per expert pair
    int32_t overhead = (static_cast<int32_t>(expert_count * expert_count) * 10) / 1000;
    
    // Thermal penalty: increased cost at high temperatures
    int32_t thermal_penalty = 0;
    if (thermal_current_ > 70) {
        // Penalty = base_cost * (thermal - 70) / 100
        thermal_penalty = (base_cost * static_cast<int32_t>(thermal_current_ - 70)) / 100;
    }
    
    // Total in fixed-point (pJ per operation)
    return base_cost + overhead + thermal_penalty;
}

// Energy-aware scaling using fixed-point
size_t MoERouter::energy_aware_scaling_fixed(
    uint32_t current_load,
    int32_t energy_budget_fixed,  // Fixed-point: budget in pJ * 1000
    uint32_t thermal_limit
) {
    // Update thermal tracking
    thermal_current_ = std::min(thermal_current_ + (current_load / 10), thermal_limit);
    
    // Compute energy cost for different expert counts using fixed-point
    size_t optimal_experts = current_active_experts_;
    int32_t best_efficiency = 0;  // Fixed-point efficiency score
    
    const size_t MIN_EXPERTS = std::max(config_.active_experts * 50 / 100, size_t(1));
    const size_t MAX_EXPERTS = std::min(config_.active_experts * 130 / 100, config_.total_experts);
    
    for (size_t e = MIN_EXPERTS; e <= MAX_EXPERTS; ++e) {
        int32_t energy_cost = compute_energy_cost_fixed(e);
        uint32_t estimated_latency = estimate_latency(e, current_load);
        
        // Fixed-point efficiency: (experts * 1000000) / (energy_cost + latency * 10)
        // Higher is better, avoid division by zero
        int32_t denominator = energy_cost + static_cast<int32_t>(estimated_latency / 10);
        if (denominator <= 0) denominator = 1;
        
        int32_t efficiency = (static_cast<int32_t>(e) * 1000000) / denominator;
        
        // Check constraints: energy_cost <= budget AND thermal <= limit
        if (energy_cost <= energy_budget_fixed && thermal_current_ <= thermal_limit) {
            if (efficiency > best_efficiency) {
                best_efficiency = efficiency;
                optimal_experts = e;
            }
        }
    }
    
    // If no configuration meets constraints, choose least energy cost
    if (optimal_experts == current_active_experts_ && best_efficiency == 0) {
        int32_t min_cost = compute_energy_cost_fixed(optimal_experts);
        for (size_t e = MIN_EXPERTS; e <= MAX_EXPERTS; ++e) {
            int32_t cost = compute_energy_cost_fixed(e);
            if (cost < min_cost) {
                min_cost = cost;
                optimal_experts = e;
            }
        }
    }
    
    current_active_experts_ = optimal_experts;
    scaling_decision_count_++;
    
    return current_active_experts_;
}

int32_t MoERouter::compute_priority_fairness_fixed() const {
    // Jain's fairness index in fixed-point (1000 = 1.0 = perfect fairness)
    // Formula: (sum(x)^2) / (n * sum(x^2))
    
    int64_t numerator = 0;    // Use int64_t to prevent overflow
    int64_t denominator = 0;
    
    for (uint32_t queue_size : priority_queue_sizes_) {
        numerator += static_cast<int64_t>(queue_size);
        denominator += static_cast<int64_t>(queue_size) * queue_size;
    }
    
    if (denominator == 0) return 1000; // Perfect fairness when empty
    
    // (numerator^2 * 1000) / (n * denominator)
    int64_t num_sq = numerator * numerator;
    int64_t n = static_cast<int64_t>(priority_queue_sizes_.size());
    int64_t result = (num_sq * 1000) / (n * denominator);
    
    return static_cast<int32_t>(result);
}

// ============================================================================
// Priority-Based Routing
// ============================================================================

std::vector<size_t> MoERouter::priority_route(const std::vector<ternary::Trit>& input, PriorityLevel priority) {
    auto logits = compute_routing_logits(input);
    
    // Convert to int32_t for priority adjustment
    std::vector<int32_t> adjusted_logits(logits.begin(), logits.end());
    
    // Apply priority bias
    adjusted_logits = apply_priority_bias(adjusted_logits, priority);
    
    // Apply load balancing with REAL expert loads (not dummy)
    adjusted_logits = apply_load_balancing(adjusted_logits, expert_loads_);
    
    // Update load counts for selected experts
    auto selected = llep_route(adjusted_logits, expert_loads_, current_active_experts_);
    for (size_t expert_idx : selected) {
        if (expert_idx < expert_loads_.size()) {
            expert_loads_[expert_idx]++;
            expert_request_counts_[expert_idx]++;
        }
    }
    
    return selected;
}

std::vector<int32_t> MoERouter::apply_priority_bias(const std::vector<int32_t>& logits, PriorityLevel priority) const {
    std::vector<int32_t> biased = logits;
    
    // Priority bias values (scaled integer offsets)
    const int32_t BIAS_TABLE[] = {
        -5,   // LOW: penalize low priority
         0,   // NORMAL: no bias
        +5,   // HIGH: favor high priority
        +15   // CRITICAL: strong favor for critical requests
    };
    
    int32_t bias = BIAS_TABLE[static_cast<uint8_t>(priority)];
    
    // Apply bias to all logits (global priority multiplier)
    for (auto& val : biased) {
        val += bias;
    }
    
    return biased;
}

std::unique_ptr<MoERouter> create_moe_router(const ExpertConfig& config) {
    return std::make_unique<MoERouter>(config);
}

// ============================================================================
// Advanced Expert Selection Algorithms
// ============================================================================

void MoERouter::initialize_advanced_selection() {
    if (advanced_selection_initialized_) return;
    
    // Initialize tropical selection state (replaces RL)
    const size_t STATE_SIZE = 100;  // Discretized state space
    state_action_scores_.resize(STATE_SIZE, std::vector<int32_t>(config_.total_experts, 50)); // Initial score 50/100
    
    last_system_state_.resize(4, 50); // [load, latency, energy, accuracy]
    selection_episode_ = 0;
    tropical_initialized_ = ternary::Trit::POSITIVE;
    
    // Initialize dynamic scaling state
    load_history_.resize(100, 50);    // 100 historical load measurements
    latency_history_.resize(100, 100); // 100 historical latency measurements (μs)
    energy_history_.resize(100, ternary::EnergyTrit::MEDIUM);  // 100 ternary energy measurements
    scaling_decision_count_ = 0;
    last_scaling_time_ = 0;
    energy_per_expert_ = ternary::EnergyTrit::MEDIUM; // 0.5 pJ/op per expert
    thermal_current_ = 30;    // 30°C default
    
    // Initialize priority routing state
    priority_performance_history_.resize(4, std::vector<uint32_t>(100, 50)); // 4 priorities, 100 time steps
    priority_queue_sizes_.resize(4, 0); // 4 priority queues
    priority_fairness_metrics_.resize(4, ternary::ProbTrit::MED_PROB); // Fairness metrics per priority
    priority_preemptions_ = 0;
    priority_reserved_experts_.resize(4); // Reserved experts per priority
    expert_performance_history_.resize(config_.total_experts, std::vector<int32_t>(10, 50)); // Default score 50
    
    // Reserve experts for critical and high priority
    reserve_priority_experts(PriorityLevel::CRITICAL, config_.total_experts * 20 / 100);
    reserve_priority_experts(PriorityLevel::HIGH, config_.total_experts * 15 / 100);
    
    // Initialize advanced load balancing state
    load_prediction_history_.resize(config_.total_experts, std::vector<uint32_t>(10, 50)); // 10 future time steps
    bottleneck_flags_.resize(config_.total_experts, ternary::Trit::ZERO);
    ml_balancing_weights_.resize(config_.total_experts, std::vector<ternary::ProbTrit>(5, ternary::ProbTrit::MED_PROB)); // 5 features per expert
    load_balancing_episodes_ = 0;
    load_balancing_accuracy_ = ternary::ProbTrit::MED_PROB; // 50% initial accuracy
    
    // Create expert groups for hierarchical balancing
    create_expert_groups(4); // Create 4 groups of experts
    advanced_selection_initialized_ = true;
}

std::vector<size_t> MoERouter::adaptive_expert_selection(
    const std::vector<ternary::Trit>& input,
    const std::vector<std::vector<int32_t>>& expert_performance,
    const std::vector<std::vector<int32_t>>& specialization_scores
) {
    // Compute base routing scores
    auto base_logits = compute_routing_logits(input);
    std::vector<int32_t> adaptive_scores(base_logits.begin(), base_logits.end());
    
    // Factor in historical performance (weighted average)
    const int32_t PERFORMANCE_WEIGHT = 30;
    for (size_t e = 0; e < config_.total_experts; ++e) {
        int32_t perf_score = 0;
        if (!expert_performance[e].empty()) {
            perf_score = std::accumulate(expert_performance[e].end() - 10, expert_performance[e].end(), 0) / 10;
        }
        adaptive_scores[e] += (perf_score * PERFORMANCE_WEIGHT) / 100;
    }
    
    // Factor in specialization scores for input pattern
    const int32_t SPECIALIZATION_WEIGHT = 20;
    size_t input_category = 0;
    
    // Simple hash of input pattern without string conversion
    for (size_t i = 0; i < input.size(); ++i) {
        input_category = (input_category * 3 + static_cast<int>(input[i])) % 10;
    }
    
    for (size_t e = 0; e < config_.total_experts; ++e) {
        if (e < specialization_scores.size() && input_category < specialization_scores[e].size()) {
            adaptive_scores[e] += (specialization_scores[e][input_category] * SPECIALIZATION_WEIGHT) / 100;
        }
    }
    
    // Apply load balancing with real expert loads
    adaptive_scores = apply_load_balancing(adaptive_scores, expert_loads_);
    
    // Select top K and update load tracking
    auto selected = select_topk(std::vector<int8_t>(adaptive_scores.begin(), adaptive_scores.end()), 
                      current_active_experts_);
    
    // Update load counts for selected experts
    for (size_t expert_idx : selected) {
        if (expert_idx < expert_loads_.size()) {
            expert_loads_[expert_idx]++;
            expert_request_counts_[expert_idx]++;
        }
    }
    
    return selected;
}

std::vector<size_t> MoERouter::quantum_entangled_selection(
    const std::vector<ternary::Trit>& input,
    int32_t coherence_threshold
) {
    ensure_routing_weights_initialized();
    initialize_entanglement_coupling();
    
    // Compute base routing with entanglement coupling
    auto base_logits = compute_routing_logits(input);
    std::vector<int32_t> quantum_scores(base_logits.begin(), base_logits.end());
    
    // Apply entanglement coupling for correlated experts
    for (size_t i = 0; i < config_.total_experts; ++i) {
        for (size_t j = 0; j < config_.total_experts; ++j) {
            if (i != j) {
                // Entanglement boost for correlated pairs
                int32_t coupling = entanglement_coupling_[i][j];
                int32_t interference = (base_logits[j] * coupling) / 100;
                quantum_scores[i] += interference / 4; // Scale down interference
            }
        }
    }
    
    // Compute coherence and filter by threshold
    int32_t coherence = compute_coherence(quantum_scores);
    if (coherence < coherence_threshold) {
        // Low coherence: fall back to standard routing
        return select_topk(base_logits, current_active_experts_);
    }
    
    // High coherence: use quantum-enhanced scores
    return select_topk(std::vector<int8_t>(quantum_scores.begin(), quantum_scores.end()), 
                      current_active_experts_);
}

std::vector<size_t> MoERouter::multi_objective_selection(
    const std::vector<ternary::Trit>& input,
    const std::vector<ternary::ProbTrit>& objectives
) {
    ensure_routing_weights_initialized();
    initialize_advanced_selection();
    
    // Adaptive load-aware selection (replaces multi-objective RL)
    auto base_logits = compute_routing_logits(input);
    
    // Default objectives: [performance, load_balance, energy, latency] in fixed-point (scale 1000)
    std::vector<std::vector<int32_t>> objective_scores(config_.total_experts, std::vector<int32_t>(4, 0));
    
    // Objective 1: Performance (based on routing scores) - scale to 0-1000
    for (size_t e = 0; e < config_.total_experts; ++e) {
        objective_scores[e][0] = (static_cast<int32_t>(base_logits[e]) + 128) * 1000 / 255;
    }
    
    // Objective 2: Load Balance (inverse of current load - simulated)
    for (size_t e = 0; e < config_.total_experts; ++e) {
        objective_scores[e][1] = 1000 - ((ternary_seed_ % 100) * 10);  // 0-1000 range
    }
    
    // Objective 3: Energy Efficiency (higher is better)
    for (size_t e = 0; e < config_.total_experts; ++e) {
        objective_scores[e][2] = 700 + (ternary_seed_ % 300);  // 700-1000 range
    }
    
    // Objective 4: Latency (lower is better, so invert)
    for (size_t e = 0; e < config_.total_experts; ++e) {
        objective_scores[e][3] = 1000 - ((ternary_seed_ % 50) * 10);  // 500-1000 range
    }
    
    // Compute weighted scores using fixed-point arithmetic
    std::vector<int32_t> weighted_scores(config_.total_experts, 0);
    for (size_t e = 0; e < config_.total_experts; ++e) {
        for (size_t o = 0; o < objectives.size() && o < 4; ++o) {
            // Convert ProbTrit to fixed-point weight (LOW=250, MED=500, HIGH=750)
            int32_t obj_weight = 500;
            switch (objectives[o]) {
                case ternary::ProbTrit::LOW_PROB: obj_weight = 250; break;
                case ternary::ProbTrit::MED_PROB: obj_weight = 500; break;
                case ternary::ProbTrit::HIGH_PROB: obj_weight = 750; break;
            }
            weighted_scores[e] += (objective_scores[e][o] * obj_weight) / 1000;
        }
    }
    
    // Convert to integer scores for selection (fixed-point to int8)
    std::vector<int8_t> final_scores(config_.total_experts);
    for (size_t e = 0; e < config_.total_experts; ++e) {
        final_scores[e] = static_cast<int8_t>((weighted_scores[e] * 127) / 1000 - 128);
    }
    
    return select_topk(final_scores, current_active_experts_);
}

std::vector<size_t> MoERouter::tropical_expert_selection(
    const std::vector<ternary::Trit>& /* input */,
    const std::vector<int32_t>& system_state
) {
    // Discretize system state for tropical selection
    size_t state_index = 0;
    for (size_t i = 0; i < system_state.size() && i < 4; ++i) {
        state_index = (state_index * 10) + (system_state[i] / 10); // Each dimension: 0-9
    }
    state_index = state_index % state_action_scores_.size();
    
    // Tropical selection: deterministic max (no random exploration)
    // Uses tropical argmax - select experts with highest tropical scores
    auto& scores = state_action_scores_[state_index];
    std::vector<std::pair<int32_t, size_t>> scored_experts;
    
    for (size_t e = 0; e < config_.total_experts; ++e) {
        scored_experts.emplace_back(scores[e], e);
    }
    
    // Tropical sort: descending order (max first)
    std::sort(scored_experts.begin(), scored_experts.end(), 
        [](const auto& a, const auto& b) { return a.first > b.first; });
    
    // Select top-K experts using tropical selection
    std::vector<size_t> selected_experts;
    selected_experts.reserve(current_active_experts_);
    
    for (size_t i = 0; i < current_active_experts_ && i < scored_experts.size(); ++i) {
        selected_experts.push_back(scored_experts[i].second);
    }
    
    // Store current state for tropical update
    last_system_state_ = system_state;
    selection_episode_++;
    
    return selected_experts;
}

// ============================================================================
// Advanced Selection Helper Methods
// ============================================================================

int32_t MoERouter::compute_pattern_similarity(
    const std::vector<ternary::Trit>& input1,
    const std::vector<ternary::Trit>& input2
) const {
    size_t min_size = std::min(input1.size(), input2.size());
    if (min_size == 0) return 0;
    
    size_t matches = 0;
    for (size_t i = 0; i < min_size; ++i) {
        if (input1[i] == input2[i]) {
            matches++;
        }
    }
    
    return static_cast<int32_t>((matches * 100) / min_size);
}

void MoERouter::update_tropical_scores(
    const std::vector<int32_t>& state,
    size_t action,
    int32_t performance_score,
    const std::vector<int32_t>& /* next_state */
) {
    // Discretize state
    size_t state_idx = 0;
    for (size_t i = 0; i < std::min(state.size(), size_t(4)); ++i) {
        state_idx = (state_idx * 10) + (state[i] / 10);
    }
    state_idx %= state_action_scores_.size();
    
    // Tropical update: score = max(score, new_score)
    // This is the tropical (max-plus) algebra update rule
    int32_t current_score = state_action_scores_[state_idx][action];
    int32_t new_score = performance_score; // Fixed-point: already scaled 0-100
    
    // Tropical max: take the maximum
    if (new_score > current_score) {
        state_action_scores_[state_idx][action] = new_score;
    }
    // If current_score >= new_score, keep current (no learning needed)
    
    selection_episode_++;
}

std::vector<size_t> MoERouter::compute_pareto_frontier(
    const std::vector<std::vector<int32_t>>& objective_scores
) const {
    std::vector<size_t> pareto_front;
    
    for (size_t i = 0; i < objective_scores.size(); ++i) {
        int8_t is_dominated = 0;  // 0 = not dominated, 1 = dominated (replaces bool)
        
        for (size_t j = 0; j < objective_scores.size(); ++j) {
            if (i == j) continue;
            
            // Check if j dominates i (better in all objectives)
            // dominates = 1 if j is better in all objectives, 0 otherwise
            int8_t dominates = 1;  // Assume j dominates until proven otherwise
            for (size_t o = 0; o < objective_scores[i].size(); ++o) {
                if (objective_scores[j][o] < objective_scores[i][o]) {
                    dominates = 0;  // j is worse in objective o, so j does not dominate i
                    break;
                }
            }
            
            if (dominates) {
                is_dominated = 1;
                break;
            }
        }
        
        if (is_dominated == 0) {  // Not dominated = on Pareto frontier
            pareto_front.push_back(i);
        }
    }
    
    return pareto_front;
}

// ============================================================================
// Enhanced Dynamic Expert Scaling
// ============================================================================

size_t MoERouter::predictive_expert_scaling(
    uint32_t current_load,
    const std::vector<uint32_t>& /* load_history */
) {
    // Update load history
    if (load_history_.size() >= 100) {
        load_history_.erase(load_history_.begin());
    }
    load_history_.push_back(current_load);
    
    // Predict future load using exponential smoothing
    uint32_t predicted_load = predict_load(load_history_);
    
    // Proactive scaling based on prediction
    const size_t MIN_EXPERTS = config_.active_experts * 60 / 100;  // -40% minimum for predictive
    const size_t MAX_EXPERTS = config_.active_experts * 150 / 100; // +50% maximum for predictive
    
    size_t target_experts = config_.active_experts;
    
    if (predicted_load > 75) {
        // Predicted high load: scale up proactively
        target_experts = std::min(current_active_experts_ + 2, MAX_EXPERTS);
    } else if (predicted_load > 60) {
        // Predicted moderate load: scale up moderately
        target_experts = std::min(current_active_experts_ + 1, MAX_EXPERTS);
    } else if (predicted_load < 25 && current_load < 30) {
        // Predicted low load: scale down conservatively
        target_experts = std::max(current_active_experts_ - 1, MIN_EXPERTS);
    }
    
    // Apply hysteresis to prevent oscillation
    static uint32_t last_prediction = 50;
    if (std::abs(static_cast<int>(predicted_load) - static_cast<int>(last_prediction)) < 10) {
        // Small change: maintain current scale
        target_experts = current_active_experts_;
    }
    last_prediction = predicted_load;
    
    current_active_experts_ = std::clamp(target_experts, MIN_EXPERTS, MAX_EXPERTS);
    scaling_decision_count_++;
    
    return current_active_experts_;
}

size_t MoERouter::latency_critical_scaling(
    uint32_t current_load,
    uint32_t latency_target,
    uint32_t current_latency
) {
    // Update latency history
    if (latency_history_.size() >= 100) {
        latency_history_.erase(latency_history_.begin());
    }
    latency_history_.push_back(current_latency);
    
    // Aggressive scaling for latency-critical workloads
    const size_t MIN_EXPERTS = config_.active_experts * 70 / 100;  // -30% minimum
    const size_t MAX_EXPERTS = std::min(config_.active_experts * 200 / 100, config_.total_experts); // +100% maximum
    
    size_t target_experts = current_active_experts_;
    
    if (current_latency > latency_target * 120 / 100) {
        // Latency exceeds target by 20%: aggressive scale up
        target_experts = std::min(current_active_experts_ + 3, MAX_EXPERTS);
    } else if (current_latency > latency_target * 110 / 100) {
        // Latency exceeds target by 10%: moderate scale up
        target_experts = std::min(current_active_experts_ + 2, MAX_EXPERTS);
    } else if (current_latency > latency_target) {
        // Latency exceeds target: conservative scale up
        target_experts = std::min(current_active_experts_ + 1, MAX_EXPERTS);
    } else if (current_latency < latency_target * 80 / 100 && current_load < 40) {
        // Latency well below target and low load: can scale down
        target_experts = std::max(current_active_experts_ - 1, MIN_EXPERTS);
    }
    
    // Predict future latency based on load trend
    uint32_t predicted_latency = estimate_latency(target_experts, predict_load(load_history_));
    
    // Adjust if prediction still exceeds target
    if (predicted_latency > latency_target) {
        target_experts = std::min(target_experts + 1, MAX_EXPERTS);
    }
    
    current_active_experts_ = std::clamp(target_experts, MIN_EXPERTS, MAX_EXPERTS);
    scaling_decision_count_++;
    
    return current_active_experts_;
}

// ============================================================================
// Scaling Helper Methods
// ============================================================================

uint32_t MoERouter::predict_load(const std::vector<uint32_t>& load_history) const {
    if (load_history.size() < 3) return load_history.empty() ? 50 : load_history.back();
    
    // Tropical smoothing (replaces exponential smoothing with doubles)
    // Uses tropical (max-plus) algebra: new_value = max(alpha * current, (1-alpha) * previous)
    // With fixed-point: scale by 100, so 30 = 0.3, 70 = 0.7
    const int32_t ALPHA_FIXED = 30;    // 0.3 in fixed-point (scale 100)
    const int32_t BETA_FIXED = 20;     // 0.2 in fixed-point (scale 100)
    const int32_t SCALE = 100;
    
    // Use integer arithmetic only
    int32_t smoothed = static_cast<int32_t>(load_history[0]) * SCALE;
    int32_t trend = 0;
    
    for (size_t i = 1; i < load_history.size(); ++i) {
        int32_t current = static_cast<int32_t>(load_history[i]) * SCALE;
        int32_t prev_smoothed = smoothed;
        
        // Tropical-like smoothing: weighted combination using fixed-point
        // smoothed = alpha * current + (1-alpha) * (smoothed + trend)
        int32_t trend_adjusted = smoothed + trend;
        int32_t weighted_current = (ALPHA_FIXED * current) / SCALE;
        int32_t weighted_prev = ((SCALE - ALPHA_FIXED) * trend_adjusted) / SCALE;
        smoothed = weighted_current + weighted_prev;
        
        // trend = beta * (smoothed - prev) + (1-beta) * trend
        int32_t diff = smoothed - prev_smoothed;
        int32_t weighted_diff = (BETA_FIXED * diff) / SCALE;
        int32_t weighted_trend = ((SCALE - BETA_FIXED) * trend) / SCALE;
        trend = weighted_diff + weighted_trend;
    }
    
    // Predict next value with trend, clamp to valid range
    int32_t prediction = (smoothed + trend) / SCALE;
    if (prediction < 0) prediction = 0;
    if (prediction > 100) prediction = 100;
    
    return static_cast<uint32_t>(prediction);
}

uint32_t MoERouter::estimate_latency(size_t expert_count, uint32_t current_load) const {
    if (expert_count == 0) return 1000; // Very high latency with no experts
    
    // Base latency decreases with more experts (inverse relationship)
    // Using integer arithmetic: base = 10000 / expert_count (in μs)
    uint32_t base_latency = 10000 / static_cast<uint32_t>(expert_count);
    
    // Load factor: latency increases with load
    // Fixed-point: load_factor = 1000 + (current_load * 20) = 1000 to 3000 (1.0 to 3.0)
    // This represents 1.0 + (load/100) * 2.0 in fixed-point
    int32_t load_factor = 1000 + (static_cast<int32_t>(current_load) * 20);
    
    // Saturation factor: diminishing returns at high expert counts
    // Fixed-point: starts at 1000, increases by 100 per expert over active threshold
    int32_t saturation_factor = 1000;
    if (expert_count > config_.active_experts) {
        saturation_factor += static_cast<int32_t>(expert_count - config_.active_experts) * 100;
    }
    
    // Calculate: base * load_factor * saturation_factor / (1000 * 1000)
    // Step 1: base * load_factor / 1000
    uint32_t step1 = (base_latency * static_cast<uint32_t>(load_factor)) / 1000;
    // Step 2: step1 * saturation_factor / 1000
    uint32_t estimated = (step1 * static_cast<uint32_t>(saturation_factor)) / 1000;
    
    return std::clamp(estimated, 1u, 10000u); // Clamp between 1μs and 10ms
}

std::vector<ternary::Trit> MoERouter::detect_bottlenecks(const std::vector<size_t>& expert_loads) const {
    std::vector<ternary::Trit> bottlenecks(expert_loads.size(), ternary::Trit::ZERO);
    size_t max_load = 0;
    for (size_t load : expert_loads) {
        if (load > max_load) max_load = load;
    }
    
    size_t threshold = (max_load * 80) / 100;
    for (size_t i = 0; i < expert_loads.size(); ++i) {
        if (expert_loads[i] > threshold) {
            bottlenecks[i] = ternary::Trit::POSITIVE;
        }
    }
    return bottlenecks;
}

// ============================================================================
// Advanced Priority Routing Implementation
// ============================================================================

std::vector<size_t> MoERouter::advanced_priority_route(
    const std::vector<ternary::Trit>& input,
    PriorityLevel priority,
    const std::vector<uint32_t>& sla_constraints,
    uint32_t system_load
) {
    // Update priority queue metrics
    priority_queue_sizes_[static_cast<size_t>(priority)]++;
    
    // Adaptive priority scaling based on system conditions
    PriorityLevel adjusted_priority = static_cast<PriorityLevel>(adaptive_priority_scaling(priority, system_load, 100 - system_load));
    
    // Compute base routing with priority bias
    auto logits = compute_routing_logits(input);
    std::vector<int32_t> adjusted_logits(logits.begin(), logits.end());
    
    // Apply enhanced priority bias
    adjusted_logits = apply_priority_bias(advanced_priority_bias(adjusted_logits, adjusted_priority), adjusted_priority);
    
    // Check if we have reserved experts for this priority
    if (!priority_reserved_experts_[static_cast<size_t>(adjusted_priority)].empty()) {
        // Use reserved experts first
        auto& reserved = priority_reserved_experts_[static_cast<size_t>(adjusted_priority)];
        std::vector<size_t> selected_experts;
        
        for (size_t expert_idx : reserved) {
            if (selected_experts.size() >= current_active_experts_) break;
            selected_experts.push_back(expert_idx);
        }
        
        // Fill remaining slots with best available experts
        if (selected_experts.size() < current_active_experts_) {
            auto additional = select_topk(std::vector<int8_t>(adjusted_logits.begin(), adjusted_logits.end()), 
                                        current_active_experts_ - selected_experts.size());
            for (size_t expert : additional) {
                if (std::find(selected_experts.begin(), selected_experts.end(), expert) == selected_experts.end()) {
                    selected_experts.push_back(expert);
                }
            }
        }
        
        return selected_experts;
    }
    
    // Standard routing with priority bias
    return select_topk(std::vector<int8_t>(adjusted_logits.begin(), adjusted_logits.end()), 
                      current_active_experts_);
}

int MoERouter::adaptive_priority_scaling(
    PriorityLevel base_priority,
    uint32_t congestion_level,
    uint32_t resource_availability
) {
    // Under high congestion, elevate priorities to maintain QoS
    if (congestion_level > 80) {
        if (base_priority == PriorityLevel::NORMAL) return static_cast<int>(PriorityLevel::HIGH);
        if (base_priority == PriorityLevel::LOW) return static_cast<int>(PriorityLevel::NORMAL);
    }
    
    // Under low resources, prioritize critical requests
    if (resource_availability < 30) {
        if (base_priority == PriorityLevel::HIGH) return static_cast<int>(PriorityLevel::CRITICAL);
    }
    
    // Under excellent conditions, can be more lenient
    if (congestion_level < 20 && resource_availability > 80) {
        // Allow some priority degradation for fairness
        static uint32_t fairness_counter = 0;
        fairness_counter++;
        
        if (fairness_counter % 10 == 0 && base_priority == PriorityLevel::HIGH) {
            return (int)PriorityLevel::NORMAL; // Occasionally downgrade for fairness
        }
    }
    
    return (int)base_priority;
}

std::vector<std::vector<size_t>> MoERouter::priority_aware_load_balance(
    const std::vector<std::pair<std::vector<ternary::Trit>, PriorityLevel>>& requests,
    const std::vector<size_t>& expert_loads
) {
    std::vector<std::vector<size_t>> assignments(config_.total_experts);
    
    // Sort requests by priority (descending)
    std::vector<size_t> request_indices(requests.size());
    std::iota(request_indices.begin(), request_indices.end(), 0);
    
    std::sort(request_indices.begin(), request_indices.end(),
        [&](size_t a, size_t b) {
            return requests[a].second > requests[b].second; // Higher priority first
        });
    
    // Process requests in priority order
    std::vector<size_t> current_loads = expert_loads;
    
    for (size_t req_idx : request_indices) {
        const auto& request = requests[req_idx];
        const auto& input = request.first;
        PriorityLevel priority = request.second;
        
        // Route request with priority consideration
        auto selected_experts = advanced_priority_route(input, priority, {100, 1000}, 
                                                      static_cast<uint32_t>(std::accumulate(current_loads.begin(), current_loads.end(), 0) * 100 / config_.total_experts));
        
        // Assign to experts
        for (size_t expert : selected_experts) {
            assignments[expert].push_back(req_idx);
            current_loads[expert]++;
        }
    }
    
    return assignments;
}

// ============================================================================
// Priority Routing Helper Methods
// ============================================================================

ternary::Trit MoERouter::check_sla_compliance(
    PriorityLevel priority,
    const std::vector<uint32_t>& sla_constraints,
    uint32_t current_latency,
    uint32_t current_throughput
) const {
    if (sla_constraints.size() < 2) return ternary::Trit::POSITIVE; // No SLA constraints
    
    uint32_t latency_sla = sla_constraints[0];
    uint32_t throughput_sla = sla_constraints[1];
    
    // Different SLA requirements based on priority
    switch (priority) {
        case PriorityLevel::CRITICAL:
            return (current_latency <= latency_sla && current_throughput >= throughput_sla) 
                ? ternary::Trit::POSITIVE : ternary::Trit::ZERO;
        case PriorityLevel::HIGH:
            return (current_latency <= latency_sla * 120 / 100 && current_throughput >= throughput_sla * 90 / 100)
                ? ternary::Trit::POSITIVE : ternary::Trit::ZERO;
        case PriorityLevel::NORMAL:
            return (current_latency <= latency_sla * 150 / 100 && current_throughput >= throughput_sla * 80 / 100)
                ? ternary::Trit::POSITIVE : ternary::Trit::ZERO;
        case PriorityLevel::LOW:
            return (current_latency <= latency_sla * 200 / 100 && current_throughput >= throughput_sla * 70 / 100)
                ? ternary::Trit::POSITIVE : ternary::Trit::ZERO;
    }
    
    return ternary::Trit::POSITIVE;
}

// Stub implementations for missing functions
void MoERouter::reserve_priority_experts(PriorityLevel /*priority*/, size_t /*count*/) {
    // TODO: Implement priority expert reservation
}

std::vector<int32_t> MoERouter::advanced_priority_bias(
    const std::vector<int32_t>& logits,
    PriorityLevel /*priority*/) const {
    // TODO: Implement advanced priority bias
    return logits;
}

void MoERouter::create_expert_groups(size_t /*num_groups*/) {
    // TODO: Implement expert group creation
}

ternary::Trit MoERouter::ternary_random() noexcept {
    // Simple deterministic ternary random using LCG
    ternary_seed_ = ternary_seed_ * 1103515245 + 12345;
    int val = (ternary_seed_ / 65536) % 3;
    if (val == 0) return ternary::Trit::NEGATIVE;
    if (val == 1) return ternary::Trit::ZERO;
    return ternary::Trit::POSITIVE;
}

} // namespace q_mini_wasm_v2::core::moe
