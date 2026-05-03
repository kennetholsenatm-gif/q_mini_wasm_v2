#pragma once

#include "../ternary/trit.hpp"
#include "../stabilizer/tableau.hpp"
#include <vector>
#include <memory>
#include <cstdint>
#include <random>
#include <algorithm>
#include <iosfwd>
#include <string>
#include <optional>
#if defined(USE_SYCL) && USE_SYCL
#include "../../sycl/tableau_kernels.hpp"
#endif

namespace q_mini_wasm_v2::core::moe {

/**
 * Options for @ref MoERouter::compute_routing_logits_result.
 *
 * When @c copy_scores_to_host is false and SYCL routing is active, symplectic scores remain in device USM
 * (@ref SymplecticLogitsResult::scores_device) for follow-on kernels without an immediate full vector readback.
 * Legacy @ref MoERouter::compute_routing_logits always behaves as copy_scores_to_host=true.
 */
struct SymplecticLogitsOptions {
    bool copy_scores_to_host = true;
};

/** Symplectic routing logits: GPU SYCL only (host vector is readback / staging, not a CPU math path). */
struct SymplecticLogitsResult {
    /** Filled when SYCL returns host scores, or after readback from @c scores_device. */
    std::vector<int8_t> scores_host;
#if defined(USE_SYCL) && USE_SYCL
    /** Present only when SYCL succeeded with copy_scores_to_host false; owns device USM until cleared/moved. */
    std::optional<::q_mini_wasm_v2::sycl_kernels::SymplecticRoutingScoresDevice> scores_device;
#endif
    bool symplectic_computed_on_sycl = false;

    /** Blocking read: returns host scores, copying from @c scores_device when @c scores_host is empty. */
    std::vector<int8_t> scores_host_blocking() const;
};

/** Symplectic routing logits policy: Auto and On both use SYCL when E>=8 (kernel minimum). No CPU scorer. */
enum class SyclRouteMode : uint8_t {
    Auto = 0,  // Default: SYCL when USE_SYCL and E >= 8 (same effective routing path as On)
    On = 1,    // Explicit: SYCL when USE_SYCL and E >= 8
};
// Do not add Off / CPU-only mode again — enforced by scripts/check_no_sycl_route_off.py (CI + ctest).

/** SYCL routing logits selection from TOML (sycl_route_mode + expert count). */
inline bool moe_routing_sycl_desired(size_t total_experts, SyclRouteMode mode) noexcept {
    (void)mode;
    if (total_experts < 8) {
        return false;
    }
    return true;
}

/**
 * When true, a SYCL submission failure for symplectic routing must abort (GPU mandatory; no substitute path).
 * Matches @ref moe_routing_sycl_desired: if the policy would have used SYCL, a SYCL error is fatal.
 */
inline bool routing_sycl_failure_must_abort(SyclRouteMode mode, size_t total_experts) noexcept {
    return moe_routing_sycl_desired(total_experts, mode);
}

/**
 * @brief Expert routing configuration
 */
struct ExpertConfig {
    size_t total_experts;       // Total number of available experts
    size_t active_experts;      // Number of experts to activate (Top-K)
    size_t routing_qutrits;     // Number of qutrits for routing decisions
    SyclRouteMode sycl_route_mode = SyclRouteMode::Auto;
};

/**
 * @brief Entangled routing configuration
 * 
 * Configuration for stabilizer tableau state-based routing with
 * entangled probability distributions for 20-30% efficiency improvement.
 */
struct EntangledRoutingConfig {
    uint32_t entanglement_strength;    // Strength of entanglement coupling (0-100)
    uint32_t coherence_threshold;      // Minimum coherence for entangled routing (0-100)
    size_t measurement_shots;          // Number of measurement shots for probability estimation
    ternary::Trit use_adaptive_entanglement;    // Adapt entanglement based on input statistics
    uint32_t efficiency_target;        // Target efficiency improvement (20-30 for 20-30%)
    
    // Default constructor with sensible defaults
    EntangledRoutingConfig()
        : entanglement_strength(70)
        , coherence_threshold(30)
        , measurement_shots(100)
        , use_adaptive_entanglement(ternary::Trit::POSITIVE)
        , efficiency_target(25)  // 25% improvement target
    {}
};

/**
 * @brief Routing strategy selection
 */
enum class RoutingStrategy {
    TOP_K,           // Standard Top-K routing
    ENTANGLED,       // Quantum-inspired entangled routing
    ADAPTIVE_LOAD,   // Adaptive load-balanced routing
    MULTI_OBJECTIVE  // Multi-objective optimization routing
};

/**
 * @brief Mixture-of-Experts Router with Tropical Geometry
 * 
 * Based on research: "Sparsity is Combinatorial Depth: Quantifying MoE 
 * Expressivity via Tropical Geometry"
 * 
 * The router uses the max-plus semiring (tropical algebra) where:
 * - Tropical addition: a ⊕ b = max(a, b)
 * - Tropical multiplication: a ⊗ b = a + b
 * 
 * Top-K routing is isomorphic to the k-th elementary symmetric tropical 
 * polynomial, providing combinatorial depth scaling with C(n,k).
 */
class MoERouter {
public:
    /**
     * @brief Construct MoE router
     * @param config Expert configuration
     */
    explicit MoERouter(const ExpertConfig& config);
    
    /**
     * @brief Construct MoE router with entangled routing support
     * @param config Expert configuration
     * @param entangled_config Entangled routing configuration
     */
    MoERouter(const ExpertConfig& config, const EntangledRoutingConfig& entangled_config);
    
    /**
     * @brief Destructor
     */
    ~MoERouter();

    // ========================================================================
    // Routing Operations
    // ========================================================================
    
    /**
     * @brief Route input to experts using specified strategy
     * @param input Input features as ternary values
     * @param strategy Routing strategy to use
     * @return Indices of selected experts
     */
    std::vector<size_t> route(
        const std::vector<ternary::Trit>& input,
        const RoutingStrategy strategy = RoutingStrategy::ADAPTIVE_LOAD
    );
    
    /**
     * @brief Route input to Top-K experts using tropical geometry
     * @param input Input features as ternary values
     * @return Indices of selected experts
     */
    std::vector<size_t> route_topk(const std::vector<ternary::Trit>& input);

    /**
     * @brief Top-K routing from TritPack5-packed features (polynomial 5 trits/byte; @p input_trit_count logical trits).
     */
    std::vector<size_t> route_topk_from_tritpack5(const std::vector<uint8_t>& input_packed, size_t input_trit_count);

    /**
     * Top-K for many packed route inputs. `USE_SYCL=1`: batched SYCL when @ref moe_routing_sycl_desired; on failure throws.
     * Per-row retry uses @ref route_topk_from_tritpack5 (GPU when routing policy selects SYCL).
     */
    std::vector<std::vector<size_t>> route_topk_many_from_tritpack5(
        const std::vector<std::vector<uint8_t>>& inputs,
        size_t input_trit_count);

    /**
     * Whether the most recent dense or packed Top-K symplectic logits evaluation ran on SYCL.
     * Always false when built without USE_SYCL. Reset at each routing call.
     */
    bool symplectic_logits_last_used_sycl() const noexcept { return last_symplectic_logits_used_sycl_; }
    const std::string& symplectic_logits_last_sycl_note() const noexcept { return last_symplectic_sycl_note_; }

    /**
     * @brief Compute routing logits using tropical inner product
     * @param input Input features
     * @return Routing logits for each expert
     */
    std::vector<int8_t> compute_routing_logits(const std::vector<ternary::Trit>& input);

    /**
     * @brief Symplectic routing logits with explicit host vs device residency policy (SYCL).
     * @see SymplecticLogitsOptions
     */
    SymplecticLogitsResult compute_routing_logits_result(
        const std::vector<ternary::Trit>& input,
        const SymplecticLogitsOptions& opts = {});
    
    /**
     * @brief Apply entanglement-based routing via stabilizer tableau
     * @param tableau Stabilizer tableau for routing state
     * @param input Input features
     * @return Entangled routing probabilities
     */
    std::vector<int32_t> entangled_route(
        stabilizer::StabilizerTableau& tableau,
        const std::vector<ternary::Trit>& input
    );

    // ========================================================================
    // Tropical Geometry Operations
    // ========================================================================
    
    /**
     * @brief Tropical inner product for metric computation
     * @param a First vector
     * @param b Second vector
     * @return Tropical inner product: max_i(a_i + b_i)
     */
    static int32_t tropical_inner_product(
        const std::vector<int32_t>& a,
        const std::vector<int32_t>& b
    );
    
    /**
     * @brief Tropical addition (max operation)
     */
    static int32_t tropical_add(int32_t a, int32_t b) {
        return (a > b) ? a : b;
    }
    
    /**
     * @brief Tropical multiplication (addition)
     */
    static int32_t tropical_multiply(int32_t a, int32_t b) {
        return a + b;
    }
    
    /**
     * @brief Compute hypersimplex capacity
     * Number of distinct linear regions = C(total_experts, active_experts)
     */
    size_t compute_hypersimplex_capacity() const;

    // ========================================================================
    // Load Balancing
    // ========================================================================
    
    /**
     * @brief Compute KL-divergence for load balancing
     * @param expert_counts Usage count for each expert
     * @return KL-divergence from uniform distribution (scaled integer)
     */
    int32_t compute_load_balance_loss(const std::vector<size_t>& expert_counts) const;
    
    /**
     * @brief Apply load balancing penalty to routing logits
     * @param logits Original routing logits
     * @param expert_counts Historical usage counts
     * @return Balanced logits
     */
    std::vector<int32_t> apply_load_balancing(
        const std::vector<int32_t>& logits,
        const std::vector<size_t>& expert_counts
    ) const;

    /**
     * @brief Least-Loaded Expert Parallelism (LLEP) Routing
     */
    std::vector<size_t> llep_route(
        const std::vector<int32_t>& logits,
        const std::vector<size_t>& expert_loads,
        size_t k
    ) const;

    /**
     * @brief Expert Choice Routing
     * 
     * Implements the GF(3) symplectic Expert Choice algorithm from
     * Quantum Architecture Review §3.4. Experts select tokens rather than
     * tokens selecting experts, eliminating load imbalance.
     * 
     * @param symplectic_scores GF(3) alignment scores for each token
     * @param expert_loads Current expert load counters
     * @param tokens_per_expert Maximum tokens per expert
     * @return Token assignment matrix [expert][token_idx]
     */
    std::vector<std::vector<size_t>> expert_choice_route(
        const std::vector<int8_t>& symplectic_scores,
        const std::vector<size_t>& expert_loads,
        size_t tokens_per_expert
    ) const;
    
    /**
     * @brief Advanced load balancing with predictive analytics
     * 
     * Implements sophisticated load balancing with:
     * - Predictive load forecasting
     * - Dynamic capacity reallocation
     * - Load-aware expert selection
     * - Bottleneck detection and mitigation
     * 
     * @param current_loads Current expert loads
     * @param load_predictions Predicted future loads
     * @param bottleneck_threshold Threshold for bottleneck detection
     * @return Optimized load-balanced expert selection
     */
    std::vector<size_t> predictive_load_balance(
        const std::vector<size_t>& current_loads,
        const std::vector<uint32_t>& load_predictions,
        uint32_t bottleneck_threshold
    );
    
    /**
     * @brief Adaptive load balancing with machine learning
     * 
     * Uses ML-based approaches for load balancing:
     * - Reinforcement learning for load distribution
     * - Anomaly detection for load spikes
     * - Self-adaptive balancing parameters
     * - Performance-driven optimization
     * 
     * @param expert_performance Historical expert performance data
     * @param system_metrics Current system performance metrics
     * @param learning_rate ML learning rate parameter
     * @return ML-optimized load-balanced routing
     */
    std::vector<size_t> ml_adaptive_balance(
        const std::vector<std::vector<int32_t>>& expert_performance,
        const std::vector<int32_t>& system_metrics,
        ternary::ProbTrit learning_rate = ternary::ProbTrit::LOW_PROB
    );
    
    /**
     * @brief Hierarchical load balancing for large-scale systems
     * 
     * Implements multi-level load balancing:
     * - Local expert group balancing
     * - Cross-group load distribution
     * - Hierarchical bottleneck resolution
     * - Scalable coordination mechanisms
     * 
     * @param expert_groups Group assignments for experts
     * @param group_loads Load per expert group
     * @param global_load Global system load
     * @return Hierarchically balanced expert selection
     */
    std::vector<size_t> hierarchical_balance(
        const std::vector<std::vector<size_t>>& expert_groups,
        const std::vector<uint32_t>& group_loads,
        uint32_t global_load
    );
    
    /**
     * @brief Get expert configuration
     */
    const ExpertConfig& config() const { return config_; }
    
    /**
     * @brief Update expert weights
     * @param expert_idx Expert index
     * @param weights New weight matrix
     */
    void update_expert_weights(size_t expert_idx, const std::vector<std::vector<ternary::Trit>>& weights);

    /**
     * Persist full MoE router state (RUF2 blob; QMINI checkpoint fmt 5+).
     * Lazy-initializes routing weights so export is deterministic.
     */
    void SerializeRouterState(std::ostream& os);

    /**
     * Restore router tail: fmt 3 = no blob; fmt 4 = legacy RTW1 routing matrix only;
     * fmt 5 = full RUF2 state.
     */
    bool DeserializeRouterState(std::istream& is, uint32_t checkpoint_fmt);

    // ========================================================================
    // Dynamic Expert Scaling
    // ========================================================================
    
    /**
     * @brief Dynamically adjust active expert count based on load
     * @param current_load Current system load (0-100)
     * @return New active expert count
     */
    size_t adjust_expert_scale(uint32_t current_load);
    
    /**
     * @brief Get current active expert count
     */
    size_t get_active_experts() const { return current_active_experts_; }
    
    /**
     * @brief Update load balancing state
     * @param loaded_expert Expert index to add load to
     */
    void update_load_balancing(size_t loaded_expert) {
        if (loaded_expert < expert_loads_.size()) {
            expert_loads_[loaded_expert]++;
            expert_request_counts_[loaded_expert]++;
        }
    }
    
    /**
     * @brief Update load balancing state for multiple experts
     * @param selected_experts Expert indices to add load to
     */
    void update_load_balancing(const std::vector<size_t>& selected_experts) {
        for (size_t expert_idx : selected_experts) {
            update_load_balancing(expert_idx);
        }
    }
    
    /**
     * @brief Get scaling factor as fixed-point (1000 = 1.0)
     * @return Scaling factor in fixed-point format
     */
    int32_t get_scaling_factor_fixed() const;
    
    /**
     * @brief Predictive expert scaling based on load patterns
     * 
     * Uses historical load data to predict optimal expert count:
     * - Time series analysis of load patterns
     * - Proactive scaling before load spikes
     * - Energy-aware scaling decisions
     * 
     * @param current_load Current system load (0-100)
     * @param load_history Historical load data
     * @return Predicted optimal expert count
     */
    size_t predictive_expert_scaling(
        uint32_t current_load,
        const std::vector<uint32_t>& load_history
    );
    
    /**
     * @brief Energy-aware expert scaling
     * 
     * Optimizes expert count for energy efficiency:
     * - Energy per expert computation
     * - Load vs energy trade-off analysis
     * - Thermal constraints consideration
     * 
     * @param current_load Current system load (0-100)
     * @param energy_budget Available energy budget (relative)
     * @param thermal_limit Thermal constraint limit
     * @return Energy-optimized expert count
     */
    /**
     * @brief Energy-aware expert scaling with fixed-point budget
     * 
     * @param current_load Current system load (0-100)
     * @param energy_budget_fixed Energy budget in fixed-point (pJ * 1000)
     * @param thermal_limit Thermal constraint limit
     * @return Energy-optimized expert count
     */
    size_t energy_aware_scaling_fixed(
        uint32_t current_load,
        int32_t energy_budget_fixed,
        uint32_t thermal_limit
    );
    
    /**
     * @brief Latency-critical expert scaling
     * 
     * Prioritizes low latency for time-sensitive workloads:
     * - Latency prediction models
     * - Aggressive scaling for latency spikes
     * - Quality-of-service guarantees
     * 
     * @param current_load Current system load (0-100)
     * @param latency_target Target latency in microseconds
     * @param current_latency Current measured latency
     * @return Latency-optimized expert count
     */
    size_t latency_critical_scaling(
        uint32_t current_load,
        uint32_t latency_target,
        uint32_t current_latency
    );
    
    // ========================================================================
    // Priority-Based Routing
    // ========================================================================
    
    enum class PriorityLevel : uint8_t {
        LOW = 0,
        NORMAL = 1,
        HIGH = 2,
        CRITICAL = 3
    };
    
    /**
     * @brief Route with priority level
     * @param input Input features
     * @param priority Request priority level
     * @return Selected expert indices
     */
    std::vector<size_t> priority_route(const std::vector<ternary::Trit>& input, PriorityLevel priority);
    
    /**
     * @brief Apply priority-based scheduling bias
     * @param logits Base routing logits
     * @param priority Request priority
     * @return Priority-adjusted logits
     */
    std::vector<int32_t> apply_priority_bias(const std::vector<int32_t>& logits, PriorityLevel priority) const;
    
    /**
     * @brief Advanced priority routing with QoS guarantees
     * 
     * Implements sophisticated priority-based routing with:
     * - Service level agreement (SLA) enforcement
     * - Dynamic priority adjustment based on system state
     * - Priority inheritance and preemption
     * - Fairness guarantees across priority levels
     * 
     * @param input Input features
     * @param priority Request priority level
     * @param sla_constraints SLA requirements (latency, throughput)
     * @param system_load Current system load (0-100)
     * @return Priority-routed expert indices with QoS guarantees
     */
    std::vector<size_t> advanced_priority_route(
        const std::vector<ternary::Trit>& input,
        PriorityLevel priority,
        const std::vector<uint32_t>& sla_constraints,
        uint32_t system_load
    );
    
    /**
     * @brief Adaptive priority scaling
     * 
     * Dynamically adjusts priority weights based on:
     * - System congestion levels
     * - Historical priority performance
     * - Resource availability
     * - Fairness metrics
     * 
     * @param base_priority Original priority level
     * @param congestion_level Current congestion (0-100)
     * @param resource_availability Available resources (0-100)
     * @return Adaptively adjusted priority level
     */
    int adaptive_priority_scaling(
        PriorityLevel base_priority,
        uint32_t congestion_level,
        uint32_t resource_availability
    );
    
    /**
     * @brief Priority-aware load balancing
     * 
     * Balances load while respecting priority constraints:
     * - High-priority requests get preferred routing
     * - Low-priority requests use less optimal experts
     * - Priority-based expert reservation
     * - Dynamic priority queue management
     * 
     * @param requests Vector of requests with priorities
     * @param expert_loads Current expert loads
     * @return Priority-aware assignment matrix
     */
    std::vector<std::vector<size_t>> priority_aware_load_balance(
        const std::vector<std::pair<std::vector<ternary::Trit>, PriorityLevel>>& requests,
        const std::vector<size_t>& expert_loads
    );

    // ========================================================================
    // Advanced Expert Selection Algorithms
    // ========================================================================
    
    /**
     * @brief Adaptive Expert Selection with Context-Aware Routing
     * 
     * Implements advanced expert selection that considers:
     * - Historical expert performance
     * - Input pattern similarity
     * - Expert specialization scores
     * - Dynamic capacity constraints
     * 
     * @param input Input features
     * @param expert_performance Historical performance metrics
     * @param specialization_scores Expert specialization scores
     * @return Selected expert indices with adaptive routing
     */
    std::vector<size_t> adaptive_expert_selection(
        const std::vector<ternary::Trit>& input,
        const std::vector<std::vector<int32_t>>& expert_performance,
        const std::vector<std::vector<int32_t>>& specialization_scores
    );
    
    /**
     * 
     * @param input Input features
     * @param coherence_threshold Minimum coherence threshold (0-100)
     * @return Quantum-selected expert indices
     */
    std::vector<size_t> quantum_entangled_selection(
        const std::vector<ternary::Trit>& input,
        int32_t coherence_threshold = 30
    );
    
    /**
     * @brief Multi-Objective Expert Selection
     * 
     * Balances multiple objectives simultaneously:
     * - Performance optimization
     * - Load balancing
     * - Energy efficiency
     * - Latency minimization
     * 
     * @param input Input features
     * @param objectives Weight vector for different objectives
     * @return Pareto-optimal expert selection
     */
    std::vector<size_t> multi_objective_selection(
        const std::vector<ternary::Trit>& input,
        const std::vector<ternary::ProbTrit>& objectives = {
            ternary::ProbTrit::HIGH_PROB, ternary::ProbTrit::MED_PROB, 
            ternary::ProbTrit::MED_PROB, ternary::ProbTrit::LOW_PROB
        }
    );
    
    /**
     * @brief Tropical geometry-based expert selection (replaces RL)
     * 
     * Uses tropical (max-plus) selection without reinforcement learning:
     * - Tropical inner product for state-action scoring
     * - Deterministic selection via tropical argmax
     * - No exploration/exploitation tradeoff (pure tropical)
     * 
     * @param input Input features
     * @param system_state Current system state [load, latency, energy, accuracy]
     * @return Tropical-optimized expert selection
     */
    std::vector<size_t> tropical_expert_selection(
        const std::vector<ternary::Trit>& input,
        const std::vector<int32_t>& system_state
    );

    friend void moe_router_checkpoint_write(MoERouter&, std::ostream&);
    friend bool moe_router_checkpoint_read(MoERouter&, std::istream&, uint32_t);

private:
    size_t current_active_experts_;
    uint32_t last_load_measurement_;
    ExpertConfig config_;
    EntangledRoutingConfig entangled_config_;
    
    /**
     * @brief Lazy initialization of routing weights (67M elements!)
     */
    void ensure_routing_weights_initialized();

    /** MoE routing matrix: E rows × R trits, each row TritPack5-packed (ceil(R/5) bytes per row). */
    const std::vector<uint8_t>& pack_routing_weights_tritpack5_rowmajor() const;

    // Expert weight matrices (ternary)
    std::vector<std::vector<std::vector<ternary::Trit>>> expert_weights_;
    
    // Routing weight matrix (ternary) - lazy initialized (67M elements!)
    std::vector<std::vector<ternary::Trit>> routing_weights_;
    bool routing_weights_initialized_;
    mutable std::vector<uint8_t> routing_weights_t5_cache_;
    mutable bool routing_weights_t5_cache_valid_ = false;
    bool entanglement_initialized_;
    bool advanced_selection_initialized_;
    
    // Ternary deterministic state generator
    uint32_t ternary_seed_;
    
    // Entanglement coupling matrix for correlated routing (scaled integer)
    std::vector<std::vector<int32_t>> entanglement_coupling_;
    std::vector<int32_t> entanglement_coupling_flat_cache_;
    bool entanglement_coupling_flat_cache_valid_ = false;
    
    // Tropical selection state (replaces RL)
    std::vector<std::vector<int32_t>> state_action_scores_;  // Tropical scores [state][action]
    std::vector<int32_t> last_system_state_;  // Last observed state
    uint32_t selection_episode_;  // Selection iteration counter
    ternary::Trit tropical_initialized_;
    
    // Dynamic scaling state
    std::vector<uint32_t> load_history_;
    std::vector<uint32_t> latency_history_;
    std::vector<ternary::EnergyTrit> energy_history_;  // Ternary energy tracking
    uint32_t scaling_decision_count_;
    uint32_t last_scaling_time_;
    ternary::EnergyTrit energy_per_expert_;  // Ternary energy per expert
    uint32_t thermal_current_;
    
    // Internal RNG
    std::mt19937 rng_{42};
    
    // Priority routing state
    std::vector<std::vector<uint32_t>> priority_performance_history_; // [priority][time_step]
    std::vector<uint32_t> priority_queue_sizes_;
    std::vector<ternary::ProbTrit> priority_fairness_metrics_;  // Ternary fairness
    uint32_t priority_preemptions_;
    std::vector<std::vector<size_t>> priority_reserved_experts_; // [priority][expert_indices]
    std::vector<std::vector<int32_t>> expert_performance_history_; // [expert][time_steps]
    
    // Advanced load balancing state
    std::vector<std::vector<uint32_t>> load_prediction_history_; // [expert][future_time_steps]
    std::vector<ternary::Trit> bottleneck_flags_; // Per expert bottleneck detection
    std::vector<std::vector<ternary::ProbTrit>> ml_balancing_weights_; // Ternary ML weights
    std::vector<std::vector<size_t>> expert_groups_; // Hierarchical grouping
    uint32_t load_balancing_episodes_;
    ternary::ProbTrit load_balancing_accuracy_;  // Ternary accuracy tracking
    
    // Expert load tracking (CRITICAL: replaces dummy_loads)
    std::vector<size_t> expert_loads_;
    std::vector<size_t> expert_request_counts_;

    /** Set per route_topk / route_topk_from_tritpack5 / compute_routing_logits call (true when SYCL ran). */
    bool last_symplectic_logits_used_sycl_ = false;
    std::string last_symplectic_sycl_note_;
    
    // ========================================================================
    // Internal Helpers
    // ========================================================================
    
    /**
     * @brief Select Top-K indices from logits
     */
    std::vector<size_t> select_topk(const std::vector<int8_t>& scores, size_t k) const;
    
    /**
     * @brief Compute factorial for combinations
     */
    static size_t factorial(size_t n);
    
    /**
     * @brief Compute binomial coefficient C(n, k)
     */
    static size_t binomial_coefficient(size_t n, size_t k);
    
    /**
     * @brief Initialize entanglement coupling matrix
     */
    void initialize_entanglement_coupling();
    
    /**
     * @brief Compute coherence metric for entangled routing
     */
    int32_t compute_coherence(const std::vector<int32_t>& probabilities) const;
    
    /**
     * @brief Initialize advanced selection state
     */
    void initialize_advanced_selection();
    
    /**
     * @brief Compute input pattern similarity
     */
    int32_t compute_pattern_similarity(
        const std::vector<ternary::Trit>& input1,
        const std::vector<ternary::Trit>& input2
    ) const;
    
    /**
     * @brief Update tropical selection scores (replaces Q-learning)
     * 
     * Uses tropical update: score = max(score, new_score)
     * 
     * @param state Current system state
     * @param action Selected expert
     * @param performance_score Performance metric (fixed-point, higher = better)
     * @param next_state Next system state
     */
    void update_tropical_scores(
        const std::vector<int32_t>& state,
        size_t action,
        int32_t performance_score,
        const std::vector<int32_t>& next_state
    );
    
    /**
     * @brief Compute Pareto-optimal frontier
     */
    std::vector<size_t> compute_pareto_frontier(
        const std::vector<std::vector<int32_t>>& objective_scores_fixed
    ) const;
    
    /**
     * @brief Predict load using exponential smoothing
     */
    uint32_t predict_load(const std::vector<uint32_t>& load_history) const;
    
    /**
     * @brief Compute energy cost for expert count
     */
    /**
     * @brief Compute energy cost using fixed-point arithmetic (scale 1000 = 1.0)
     * @param expert_count Number of experts
     * @return Energy cost in fixed-point (pJ per operation)
     */
    int32_t compute_energy_cost_fixed(size_t expert_count) const;
    
    /**
     * @brief Estimate latency for expert count
     */
    uint32_t estimate_latency(size_t expert_count, uint32_t current_load) const;
    
    /**
     * @brief Check SLA compliance for priority level
     */
    ternary::Trit check_sla_compliance(
        PriorityLevel priority,
        const std::vector<uint32_t>& sla_constraints,
        uint32_t current_latency,
        uint32_t current_throughput
    ) const;
    
    /**
     * @brief Compute priority fairness metrics
     */
    /**
     * @brief Compute priority fairness as fixed-point (1000 = 1.0 = perfect fairness)
     * @return Jain's fairness index in fixed-point format
     */
    int32_t compute_priority_fairness_fixed() const;
    
    /**
     * @brief Reserve experts for high priority
     */
    void reserve_priority_experts(PriorityLevel priority, size_t expert_count);
    
    /**
     * @brief Advanced priority bias with context awareness
     */
    std::vector<int32_t> advanced_priority_bias(const std::vector<int32_t>& logits, PriorityLevel priority) const;
    
    /**
     * @brief Predict future loads using time series analysis
     */
    std::vector<uint32_t> predict_future_loads(const std::vector<size_t>& current_loads) const;
    
    /**
     * @brief Detect bottlenecks in expert utilization
     */
    std::vector<ternary::Trit> detect_bottlenecks(const std::vector<size_t>& expert_loads) const;
    
    /**
     * @brief Update ML balancing model
     */
    void update_ml_balancing_model(
        const std::vector<std::vector<int32_t>>& expert_performance,
        const std::vector<int32_t>& system_metrics,
        int32_t learning_rate_fixed
    );
    
    /**
     * @brief Create expert groups for hierarchical balancing
     */
    void create_expert_groups(size_t num_groups);
    
    /**
     * @brief Generate deterministic ternary random value
     * @return Ternary value in GF(3) space
     */
    ternary::Trit ternary_random() noexcept;
    
    /**
     * @brief Generate deterministic ternary probability
     * @return Ternary probability value
     */
    ternary::ProbTrit ternary_prob_random() noexcept;
};

/**
 * @brief Factory function for creating MoE router
 */
std::unique_ptr<MoERouter> create_moe_router(const ExpertConfig& config);

} // namespace q_mini_wasm_v2::core::moe