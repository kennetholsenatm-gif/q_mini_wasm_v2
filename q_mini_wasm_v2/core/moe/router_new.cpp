#include "router.hpp"
#include <algorithm>
#include <numeric>
#include <cmath>
#include <stdexcept>
#include <random>
#include <complex>
#include <unordered_map>

namespace q_mini_wasm_v2::core::moe {

// ============================================================================
// Constructors
// ============================================================================

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

std::vector<double> MoERouter::compute_routing_logits(const std::vector<ternary::Trit>& input) {
    std::vector<double> logits(config_.total_experts, 0.0);
    
    for (size_t e = 0; e < config_.total_experts; ++e) {
        // Compute tropical inner product between input and routing weights
        size_t min_size = std::min(input.size(), routing_weights_[e].size());
        std::vector<double> input_double(min_size);
        std::vector<double> weights_double(min_size);
        
        for (size_t i = 0; i < min_size; ++i) {
            input_double[i] = static_cast<double>(input[i]);
        }
        for (size_t i = 0; i < min_size; ++i) {
            weights_double[i] = static_cast<double>(routing_weights_[e][i]);
        }
        
        logits[e] = tropical_inner_product(input_double, weights_double);
    }
    
    return tropical_softmax(logits);
}