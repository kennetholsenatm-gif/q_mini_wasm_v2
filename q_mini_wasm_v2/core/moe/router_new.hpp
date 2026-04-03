#pragma once

#include <vector>
#include <cstdint>
#include <memory>
#include <functional>
#include <random>
#include "../stabilizer/tableau.hpp"
#include "../ternary/trit.hpp"

namespace q_mini_wasm_v2::core::moe {

/**
 * @brief Expert routing configuration
 */
struct ExpertConfig {
    size_t total_experts;       // Total number of available experts
    size_t active_experts;      // Number of experts to activate (Top-K)
    size_t routing_qutrits;     // Number of qutrits for routing decisions
};

/**
 * @brief Entangled routing configuration
 * 
 * Configuration for stabilizer tableau state-based routing with
 * entangled probability distributions for 20-30% efficiency improvement.
 */
struct EntangledRoutingConfig {
    double entanglement_strength;    // Strength of entanglement coupling (0.0-1.0)
    double coherence_threshold;      // Minimum coherence for entangled routing
    size_t measurement_shots;        // Number of measurement shots for probability estimation
    bool use_adaptive_entanglement;  // Adapt entanglement based on input statistics
    double efficiency_target;        // Target efficiency improvement (0.2-0.3 for 20-30%)
    
    // Default constructor with sensible defaults
    EntangledRoutingConfig()
        : entanglement_strength(0.7)
        , coherence_threshold(0.3)
        , measurement_shots(100)
        , use_adaptive_entanglement(true)
        , efficiency_target(0.25)  // 25% improvement target
    {}
};