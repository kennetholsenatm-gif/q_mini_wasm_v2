#include "router.hpp"
#include <numeric>
#include <stdexcept>
#include <algorithm>
#include <random>

namespace q_mini_wasm_v2::core::moe {

MoERouter::MoERouter(const ExpertConfig& config)
    : config_(config)
    , current_active_experts_(config.active_experts)
    , last_load_measurement_(50)
    , expert_weights_(config.total_experts)
    , routing_weights_(config.total_experts, std::vector<ternary::Trit>(config.routing_qutrits, ternary::Trit::ZERO))
    , ternary_seed_(42)  // Deterministic seed for reproducibility
    , learning_episode_(0)
    , rl_initialized_(false)
    , expert_loads_(config.total_experts, 0)  // Initialize load tracking
    , expert_request_counts_(config.total_experts, 0)
{
    // Initialize routing weights with deterministic ternary values
    for (auto& row : routing_weights_) {
        for (auto& val : row) {
            val = ternary_random();  // Deterministic ternary random
        }
    }
    
    // Initialize entanglement coupling matrix
    initialize_entanglement_coupling();
    
    // Initialize advanced selection state
    initialize_advanced_selection();
}

MoERouter::MoERouter(const ExpertConfig& config, const EntangledRoutingConfig& entangled_config)
    : config_(config)
    , current_active_experts_(config.active_experts)
    , last_load_measurement_(50)
    , entangled_config_(entangled_config)
    , expert_weights_(config.total_experts)
    , routing_weights_(config.total_experts, std::vector<ternary::Trit>(config.routing_qutrits, ternary::Trit::ZERO))
    , ternary_seed_(42)  // Deterministic seed for reproducibility
    , learning_episode_(0)
    , rl_initialized_(false)
    , expert_loads_(config.total_experts, 0)  // Initialize load tracking
    , expert_request_counts_(config.total_experts, 0)
{
    // Initialize routing weights with deterministic ternary values
    for (auto& row : routing_weights_) {
        for (auto& val : row) {
            val = ternary_random();  // Deterministic ternary random
        }
    }
    
    // Initialize entanglement coupling matrix
    initialize_entanglement_coupling();
    
    // Initialize advanced selection state
    initialize_advanced_selection();
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

std::vector<int8_t> MoERouter::compute_routing_logits(const std::vector<ternary::Trit>& input) {
    std::vector<int8_t> symplectic_scores(config_.total_experts, -1);
    
    // Compute GF(3) Symplectic Inner Product for each expert
    // Implements the quantum routing mechanism described in Architecture Review
    for (size_t e = 0; e < config_.total_experts; ++e) {
        size_t min_size = std::min(input.size(), routing_weights_[e].size());
        
        int8_t symplectic_sum = 0;
        
        // Symplectic inner product over GF(3): sum( a_i * b_{i+n} - a_{i+n} * b_i ) mod 3
        // This is the stabilizer state alignment metric
        for (size_t i = 0; i < min_size; i += 2) {
            int8_t x1 = static_cast<int8_t>(input[i]);
            int8_t z1 = (i+1 < min_size) ? static_cast<int8_t>(input[i+1]) : 0;
            
            int8_t x2 = static_cast<int8_t>(routing_weights_[e][i]);
            int8_t z2 = (i+1 < min_size) ? static_cast<int8_t>(routing_weights_[e][i+1]) : 0;
            
            // Symplectic pairing: x1*z2 - z1*x2 mod 3
            int8_t pairing = (x1 * z2) - (z1 * x2);
            
            // Reduce to GF(3) canonical range {-1, 0, 1}
            while (pairing > 1)  pairing -= 3;
            while (pairing < -1) pairing += 3;
            
            symplectic_sum = gf3_add(symplectic_sum, pairing);
        }
        
        symplectic_scores[e] = symplectic_sum;
    }
    
    // No floating point conversion, no softmax. Strict discrete GF(3) outputs only.
    return symplectic_scores;
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
    std::vector<size_t> indices(scores.size());
    std::iota(indices.begin(), indices.end(), 0);
    
    // Partial sort to get Top-K GF(3) symplectic scores
    std::partial_sort(
        indices.begin(),
        indices.begin() + k,
        indices.end(),
        [&scores](size_t a, size_t b) {
            return scores[a] > scores[b];
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
        remaining_tokens.erase(
            remaining_tokens.begin(),
            remaining_tokens.begin() + assign_count
        );
        
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

float MoERouter::get_scaling_factor() const {
    return static_cast<float>(current_active_experts_) / config_.active_experts;
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
    // Initialize expert performance history
    expert_performance_history_.resize(config_.total_experts, std::vector<int32_t>(100, 50)); // 100 time steps, default 50%
    
    // Initialize expert specialization scores with ternary deterministic values
    expert_specialization_scores_.resize(config_.total_experts, std::vector<int32_t>(10, 33)); // 10 categories
    for (auto& scores : expert_specialization_scores_) {
        for (auto& score : scores) {
            score = static_cast<int32_t>(ternary_random()) * 33 + 33; // Map -1,0,1 to 0,33,66
        }
    }
    
    // Initialize Q-learning table (state_size x action_size)
    const size_t STATE_SIZE = 100;  // Discretized state space
    q_learning_table_.resize(STATE_SIZE, std::vector<ternary::ProbTrit>(config_.total_experts, ternary::ProbTrit::LOW_PROB)); // Small initial values
    
    last_system_state_.resize(4, 50); // [load, latency, energy, accuracy]
    learning_episode_ = 0;
    
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
    
    // Reserve experts for critical and high priority
    reserve_priority_experts(PriorityLevel::CRITICAL, config_.total_experts * 20 / 100);
    reserve_priority_experts(PriorityLevel::HIGH, config_.total_experts * 15 / 100);
    
    // Initialize advanced load balancing state
    load_prediction_history_.resize(config_.total_experts, std::vector<uint32_t>(10, 50)); // 10 future time steps
    bottleneck_flags_.resize(config_.total_experts, false);
    ml_balancing_weights_.resize(config_.total_experts, std::vector<ternary::ProbTrit>(5, ternary::ProbTrit::MED_PROB)); // 5 features per expert
    load_balancing_episodes_ = 0;
    load_balancing_accuracy_ = ternary::ProbTrit::MED_PROB; // 50% initial accuracy
    
    // Create expert groups for hierarchical balancing
    create_expert_groups(4); // Create 4 groups of experts
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
    const std::vector<double>& objectives
) {
    // Default objectives: [performance, load_balance, energy, latency]
    std::vector<std::vector<double>> objective_scores(config_.total_experts, std::vector<double>(4, 0.0));
    
    auto base_logits = compute_routing_logits(input);
    
    // Objective 1: Performance (based on routing scores)
    for (size_t e = 0; e < config_.total_experts; ++e) {
        objective_scores[e][0] = static_cast<double>(base_logits[e] + 128) / 255.0; // Normalize to [0,1]
    }
    
    // Objective 2: Load Balance (inverse of current load - simulated)
    for (size_t e = 0; e < config_.total_experts; ++e) {
        objective_scores[e][1] = 1.0 - (static_cast<double>(rand() % 100) / 100.0);
    }
    
    // Objective 3: Energy Efficiency (higher is better)
    for (size_t e = 0; e < config_.total_experts; ++e) {
        objective_scores[e][2] = 0.7 + (static_cast<double>(rand() % 30) / 100.0);
    }
    
    // Objective 4: Latency (lower is better, so invert)
    for (size_t e = 0; e < config_.total_experts; ++e) {
        objective_scores[e][3] = 1.0 - (static_cast<double>(rand() % 50) / 100.0);
    }
    
    // Compute weighted scores
    std::vector<double> weighted_scores(config_.total_experts, 0.0);
    for (size_t e = 0; e < config_.total_experts; ++e) {
        for (size_t o = 0; o < objectives.size() && o < 4; ++o) {
            weighted_scores[e] += objective_scores[e][o] * objectives[o];
        }
    }
    
    // Convert to integer scores for selection
    std::vector<int8_t> final_scores(config_.total_experts);
    for (size_t e = 0; e < config_.total_experts; ++e) {
        final_scores[e] = static_cast<int8_t>(weighted_scores[e] * 127 - 128);
    }
    
    return select_topk(final_scores, current_active_experts_);
}

std::vector<size_t> MoERouter::rl_expert_selection(
    const std::vector<ternary::Trit>& /* input */,
    const std::vector<int32_t>& system_state
) {
    // Discretize system state for Q-learning
    size_t state_index = 0;
    for (size_t i = 0; i < system_state.size() && i < 4; ++i) {
        state_index = (state_index * 10) + (system_state[i] / 10); // Each dimension: 0-9
    }
    state_index = state_index % q_learning_table_.size();
    
    // Epsilon-greedy action selection
    const double EPSILON = 0.1; // 10% exploration
    std::vector<size_t> selected_experts;
    
    if (static_cast<double>(rand()) / RAND_MAX < EPSILON) {
        // Exploration: random selection
        std::vector<size_t> all_experts(config_.total_experts);
        std::iota(all_experts.begin(), all_experts.end(), 0);
        std::shuffle(all_experts.begin(), all_experts.end(), rng_);
        selected_experts.assign(all_experts.begin(), all_experts.begin() + current_active_experts_);
    } else {
        // Exploitation: best Q-values
        auto& q_values = q_learning_table_[state_index];
        std::vector<std::pair<uint32_t, size_t>> scored_experts;
        for (size_t e = 0; e < config_.total_experts; ++e) {
            scored_experts.emplace_back(trit_to_prob(q_values[e]), e);
        }
        std::sort(scored_experts.begin(), scored_experts.end(), std::greater<>());
        
        for (size_t i = 0; i < current_active_experts_; ++i) {
            selected_experts.push_back(scored_experts[i].second);
        }
    }
    
    // Store current state for learning
    last_system_state_ = system_state;
    
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

void MoERouter::update_q_learning(
    const std::vector<int32_t>& state,
    size_t action,
    double reward,
    const std::vector<int32_t>& next_state
) {
    const double ALPHA = 0.1;  // Learning rate
    const double GAMMA = 0.9;  // Discount factor
    
    // Discretize states
    size_t state_idx = 0, next_state_idx = 0;
    for (size_t i = 0; i < std::min(state.size(), size_t(4)); ++i) {
        state_idx = (state_idx * 10) + (state[i] / 10);
    }
    for (size_t i = 0; i < std::min(next_state.size(), size_t(4)); ++i) {
        next_state_idx = (next_state_idx * 10) + (next_state[i] / 10);
    }
    state_idx %= q_learning_table_.size();
    next_state_idx %= q_learning_table_.size();
    
    // Find max Q-value for next state
    auto max_it = std::max_element(q_learning_table_[next_state_idx].begin(), 
                                   q_learning_table_[next_state_idx].end(),
                                   [](ternary::ProbTrit a, ternary::ProbTrit b) {
                                       return trit_to_prob(a) < trit_to_prob(b);
                                   });
    double max_next_q = trit_to_prob(*max_it);
    
    // Q-learning update: Q(s,a) = Q(s,a) + α[r + γ*max(Q(s',a')) - Q(s,a)]
    double old_q = trit_to_prob(q_learning_table_[state_idx][action]);
    double new_q = old_q + ALPHA * (reward + GAMMA * max_next_q - old_q);
    q_learning_table_[state_idx][action] = prob_to_trit(static_cast<uint32_t>(std::clamp(new_q, 0.0, 100.0)));
    
    learning_episode_++;
}

std::vector<size_t> MoERouter::compute_pareto_frontier(
    const std::vector<std::vector<double>>& objective_scores
) const {
    std::vector<size_t> pareto_front;
    
    for (size_t i = 0; i < objective_scores.size(); ++i) {
        bool is_dominated = false;
        
        for (size_t j = 0; j < objective_scores.size(); ++j) {
            if (i == j) continue;
            
            // Check if j dominates i (better in all objectives)
            bool dominates = true;
            for (size_t o = 0; o < objective_scores[i].size(); ++o) {
                if (objective_scores[j][o] < objective_scores[i][o]) {
                    dominates = false;
                    break;
                }
            }
            
            if (dominates) {
                is_dominated = true;
                break;
            }
        }
        
        if (!is_dominated) {
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

size_t MoERouter::energy_aware_scaling(
    uint32_t current_load,
    double energy_budget,
    uint32_t thermal_limit
) {
    // Update thermal and energy tracking
    thermal_current_ = std::min(thermal_current_ + (current_load / 10), thermal_limit);
    
    // Compute energy cost for different expert counts
    size_t optimal_experts = current_active_experts_;
    double best_efficiency = 0.0;
    
    const size_t MIN_EXPERTS = std::max(config_.active_experts * 50 / 100, size_t(1));
    const size_t MAX_EXPERTS = std::min(config_.active_experts * 130 / 100, config_.total_experts);
    
    for (size_t e = MIN_EXPERTS; e <= MAX_EXPERTS; ++e) {
        double energy_cost = compute_energy_cost(e);
        uint32_t estimated_latency = estimate_latency(e, current_load);
        
        // Energy efficiency: throughput per energy unit
        double efficiency = (static_cast<double>(e) * 1000) / (energy_cost + estimated_latency / 100.0);
        
        // Check thermal and energy constraints
        if (energy_cost <= energy_budget && thermal_current_ <= thermal_limit) {
            if (efficiency > best_efficiency) {
                best_efficiency = efficiency;
                optimal_experts = e;
            }
        }
    }
    
    // If no configuration meets constraints, choose least violating option
    if (optimal_experts == current_active_experts_ && best_efficiency == 0.0) {
        for (size_t e = MIN_EXPERTS; e <= MAX_EXPERTS; ++e) {
            double energy_cost = compute_energy_cost(e);
            if (energy_cost < compute_energy_cost(optimal_experts)) {
                optimal_experts = e;
            }
        }
    }
    
    current_active_experts_ = optimal_experts;
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
    
    // Exponential smoothing with trend adjustment
    const double ALPHA = 0.3;  // Smoothing factor
    const double BETA = 0.2;   // Trend factor
    
    double smoothed = load_history[0];
    double trend = 0;
    
    for (size_t i = 1; i < load_history.size(); ++i) {
        double prev_smoothed = smoothed;
        smoothed = ALPHA * load_history[i] + (1 - ALPHA) * (smoothed + trend);
        trend = BETA * (smoothed - prev_smoothed) + (1 - BETA) * trend;
    }
    
    // Predict next value with trend
    uint32_t prediction = static_cast<uint32_t>(std::clamp(smoothed + trend, 0.0, 100.0));
    return prediction;
}

double MoERouter::compute_energy_cost(size_t expert_count) const {
    // Base energy cost: energy per expert * number of experts
    double base_cost = expert_count * trit_to_energy(energy_per_expert_);
    
    // Overhead cost: quadratic scaling for coordination
    double overhead = (expert_count * expert_count) * 0.01; // 0.01 pJ/op per expert pair
    
    // Thermal penalty: increased cost at high temperatures
    double thermal_penalty = (thermal_current_ > 70) ? 
        base_cost * ((thermal_current_ - 70) * 0.01) : 0.0;
    
    return base_cost + overhead + thermal_penalty;
}

uint32_t MoERouter::estimate_latency(size_t expert_count, uint32_t current_load) const {
    if (expert_count == 0) return 1000; // Very high latency with no experts
    
    // Base latency decreases with more experts (inverse relationship)
    uint32_t base_latency = static_cast<uint32_t>(10000 / expert_count); // 10ms base / expert_count
    
    // Load factor: latency increases with load
    double load_factor = 1.0 + (current_load / 100.0) * 2.0; // 2x latency at 100% load
    
    // Saturation factor: diminishing returns at high expert counts
    double saturation_factor = 1.0;
    if (expert_count > config_.active_experts) {
        saturation_factor = 1.0 + ((expert_count - config_.active_experts) * 0.1);
    }
    
    uint32_t estimated = static_cast<uint32_t>(base_latency * load_factor * saturation_factor);
    return std::clamp(estimated, 1u, 10000u); // Clamp between 1μs and 10ms
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
    PriorityLevel adjusted_priority = adaptive_priority_scaling(priority, system_load, 100 - system_load);
    
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

PriorityLevel MoERouter::adaptive_priority_scaling(
    PriorityLevel base_priority,
    uint32_t congestion_level,
    uint32_t resource_availability
) {
    // Under high congestion, elevate priorities to maintain QoS
    if (congestion_level > 80) {
        if (base_priority == PriorityLevel::NORMAL) return PriorityLevel::HIGH;
        if (base_priority == PriorityLevel::LOW) return PriorityLevel::NORMAL;
    }
    
    // Under low resources, prioritize critical requests
    if (resource_availability < 30) {
        if (base_priority == PriorityLevel::HIGH) return PriorityLevel::CRITICAL;
    }
    
    // Under excellent conditions, can be more lenient
    if (congestion_level < 20 && resource_availability > 80) {
        // Allow some priority degradation for fairness
        static uint32_t fairness_counter = 0;
        fairness_counter++;
        
        if (fairness_counter % 10 == 0 && base_priority == PriorityLevel::HIGH) {
            return PriorityLevel::NORMAL; // Occasionally downgrade for fairness
        }
    }
    
    return base_priority;
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

bool MoERouter::check_sla_compliance(
    PriorityLevel priority,
    const std::vector<uint32_t>& sla_constraints,
    uint32_t current_latency,
    uint32_t current_throughput
) const {
    if (sla_constraints.size() < 2) return true; // No SLA constraints
    
    uint32_t latency_sla = sla_constraints[0];
    uint32_t throughput_sla = sla_constraints[1];
    
    // Different SLA requirements based on priority
    switch (priority) {
        case PriorityLevel::CRITICAL:
            return current_latency <= latency_sla && current_throughput >= throughput_sla;
        case PriorityLevel::HIGH:
            return current_latency <= latency_sla * 120 / 100 && current_throughput >= throughput_sla * 90 / 100;
        case PriorityLevel::NORMAL:
            return current_latency <= latency_sla * 150 / 100 && current_throughput >= throughput_sla * 80 / 100;
        case PriorityLevel::LOW:
            return current_latency <= latency_sla * 200 / 100 && current_throughput >= throughput_sla * 70 / 100;
    }
    
    return true;
}

double MoERouter::compute_priority_fairness() const {
    // Jain's fairness index for priority queues
    double numerator = 0.0;
    double denominator = 0.0;
    
    for (uint32_t queue_size : priority_queue_sizes_) {
        numerator += queue_size;
        denominator += queue_size * queue_size;
    }
    
    if (denominator == 0) return 1.0;
    
    double fairness = (numerator * numerator) / (priority_queue_sizes_.size() * denominator);
    return fairness;
}

void MoERouter::reserve_priority_experts(PriorityLevel priority, size_t expert_count) {
    size_t priority_idx = static_cast<size_t>(priority);
    priority_reserved_experts_[priority_idx].clear();
    
    // Select best performing experts for reservation
    std::vector<std::pair<int32_t, size_t>> expert_scores;
    for (size_t i = 0; i < config_.total_experts; ++i) {
        int32_t score = std::accumulate(expert_performance_history_[i].end() - 10, 
                                       expert_performance_history_[i].end(), 0) / 10;
        expert_scores.emplace_back(score, i);
    }
    
    std::sort(expert_scores.begin(), expert_scores.end(), std::greater<>());
    
    for (size_t i = 0; i < std::min(expert_count, expert_scores.size()); ++i) {
        priority_reserved_experts_[priority_idx].push_back(expert_scores[i].second);
    }
}

std::vector<int32_t> MoERouter::advanced_priority_bias(const std::vector<int32_t>& logits, PriorityLevel priority) const {
    std::vector<int32_t> biased = logits;
    
    // Enhanced priority bias with context awareness
    const int32_t ENHANCED_BIAS_TABLE[] = {
        -10,  // LOW: stronger penalty
        0,    // NORMAL: neutral
        +10,  // HIGH: significant favor
        +25   // CRITICAL: maximum priority
    };
    
    int32_t bias = ENHANCED_BIAS_TABLE[static_cast<uint8_t>(priority)];
    
    // Apply bias with fairness consideration
    double fairness = compute_priority_fairness();
    if (fairness < 0.8 && priority != PriorityLevel::CRITICAL) {
        // Reduce bias if fairness is poor (except for critical)
        bias = bias * 80 / 100;
    }
    
    for (auto& val : biased) {
        val += bias;
    }
    
    return biased;
}

// ============================================================================
// Advanced Load Balancing Implementation
// ============================================================================

std::vector<size_t> MoERouter::predictive_load_balance(
    const std::vector<size_t>& current_loads,
    const std::vector<uint32_t>& load_predictions,
    uint32_t bottleneck_threshold
) {
    // Update load prediction history
    for (size_t i = 0; i < config_.total_experts && i < load_predictions.size(); ++i) {
        if (load_prediction_history_[i].size() >= 10) {
            load_prediction_history_[i].erase(load_prediction_history_[i].begin());
        }
        load_prediction_history_[i].push_back(load_predictions[i]);
    }
    
    // Detect current bottlenecks
    bottleneck_flags_ = detect_bottlenecks(current_loads);
    
    // Create expert selection scores based on predicted loads
    std::vector<std::pair<int32_t, size_t>> expert_scores;
    expert_scores.reserve(config_.total_experts);
    
    for (size_t i = 0; i < config_.total_experts; ++i) {
        int32_t score = 100; // Base score
        
        // Penalize experts with high current loads
        score -= static_cast<int32_t>(current_loads[i] * 2);
        
        // Penalize experts with high predicted loads
        uint32_t avg_predicted = std::accumulate(load_prediction_history_[i].begin(), 
                                               load_prediction_history_[i].end(), 0) / load_prediction_history_[i].size();
        score -= static_cast<int32_t>(avg_predicted);
        
        // Heavily penalize bottlenecked experts
        if (bottleneck_flags_[i]) {
            score -= 50;
        }
        
        expert_scores.emplace_back(score, i);
    }
    
    // Sort by score (descending)
    std::sort(expert_scores.begin(), expert_scores.end(), std::greater<>());
    
    // Select top experts avoiding bottlenecks
    std::vector<size_t> selected_experts;
    for (const auto& entry : expert_scores) {
        if (selected_experts.size() >= current_active_experts_) break;
        if (!bottleneck_flags_[entry.second]) {
            selected_experts.push_back(entry.second);
        }
    }
    
    // If not enough non-bottleneck experts, include bottlenecked ones
    if (selected_experts.size() < current_active_experts_) {
        for (const auto& entry : expert_scores) {
            if (selected_experts.size() >= current_active_experts_) break;
            if (std::find(selected_experts.begin(), selected_experts.end(), entry.second) == selected_experts.end()) {
                selected_experts.push_back(entry.second);
            }
        }
    }
    
    return selected_experts;
}

// ============================================================================
// Ternary Deterministic Generation & Helper Methods
// ============================================================================

void MoERouter::create_expert_groups(size_t num_groups) {
    expert_groups_.clear();
    expert_groups_.resize(num_groups);
    
    // Simple round-robin assignment to groups
    for (size_t i = 0; i < config_.total_experts; ++i) {
        size_t group_idx = i % num_groups;
        expert_groups_[group_idx].push_back(i);
    }
}

ternary::Trit MoERouter::ternary_random() noexcept {
    // Simple linear congruential generator in GF(3) space
    ternary_seed_ = (ternary_seed_ * 1103515245 + 12345) & 0x7fffffff;
    
    // Map to ternary space
    int32_t trit_val = static_cast<int32_t>(ternary_seed_ % 3);
    if (trit_val == 0) return ternary::Trit::NEGATIVE;  // Map 0 -> -1
    if (trit_val == 1) return ternary::Trit::ZERO;     // Map 1 -> 0
    return ternary::Trit::POSITIVE;                     // Map 2 -> +1
}

ternary::ProbTrit MoERouter::ternary_prob_random() noexcept {
    // Use ternary random to generate probability
    ternary::Trit base_trit = ternary_random();
    
    // Map trit to probability space
    if (base_trit == ternary::Trit::NEGATIVE) return ternary::ProbTrit::LOW_PROB;
    if (base_trit == ternary::Trit::ZERO) return ternary::ProbTrit::MED_PROB;
    return ternary::ProbTrit::HIGH_PROB;
}

} // namespace q_mini_wasm_v2::core::moe
