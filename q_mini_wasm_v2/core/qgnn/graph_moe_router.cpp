#include "graph_moe_router.hpp"
#include <algorithm>
#include <numeric>
#include <cmath>

namespace q_mini_wasm_v2::core::qgnn {

GraphMoERouter::GraphMoERouter(const GraphConfig& config)
    : config_(config)
    , expert_graph_(create_qgnn_graph())
    , routing_round_(0)
    , accumulated_energy_(ternary::EnergyTrit::LOW)
    , initialized_(false)
{
    // Initialize statistics
    routing_stats_ = RoutingStats{
        .total_routings = 0,
        .avg_latency_us = 0,
        .avg_energy_per_routing = ternary::EnergyTrit::MEDIUM,
        .avg_confidence = ternary::ProbTrit::MED_PROB,
        .load_balance_score = 1.0
    };
    
    // Initialize expert graph
    initialize_expert_graph();
    initialized_ = true;
}

void GraphMoERouter::initialize_expert_graph() {
    // Create expert nodes
    expert_ids_.reserve(config_.max_experts);
    for (size_t i = 0; i < config_.max_experts; ++i) {
        NodeID expert_id = expert_graph_->add_expert_node(config_.specialization_dim);
        expert_ids_.push_back(expert_id);
        
        // Initialize with random specialization
        if (auto* expert = expert_graph_->get_expert_node(expert_id)) {
            for (auto& spec : expert->specialization_vector) {
                // Simple deterministic initialization
                spec = static_cast<ternary::Trit>((i % 3) - 1);
            }
        }
    }
    
    // Create default entanglement pattern
    create_default_entanglement();
}

void GraphMoERouter::create_default_entanglement() {
    // Create ring topology for default entanglement
    for (size_t i = 0; i < expert_ids_.size(); ++i) {
        NodeID current = expert_ids_[i];
        NodeID next = expert_ids_[(i + 1) % expert_ids_.size()];
        
        // Add bidirectional entanglement
        expert_graph_->add_entanglement_edge(
            current, next, 
            ternary::Trit::POSITIVE, 
            ternary::EnergyTrit::LOW
        );
        
        expert_graph_->add_entanglement_edge(
            next, current, 
            ternary::Trit::POSITIVE, 
            ternary::EnergyTrit::LOW
        );
    }
    
    // Add some cross-connections for robustness
    for (size_t i = 0; i < expert_ids_.size(); i += 3) {
        if (i + 2 < expert_ids_.size()) {
            NodeID current = expert_ids_[i];
            NodeID skip = expert_ids_[i + 2];
            
            expert_graph_->add_entanglement_edge(
                current, skip,
                ternary::Trit::NEGATIVE,  // Weaker connection
                ternary::EnergyTrit::LOW
            );
        }
    }
}

NodeID GraphMoERouter::add_expert(const std::vector<ternary::Trit>& specialization) {
    if (expert_ids_.size() >= config_.max_experts) {
        return NodeID(0, ternary::Trit::ZERO);  // Invalid ID
    }
    
    NodeID expert_id = expert_graph_->add_expert_node(config_.specialization_dim);
    expert_ids_.push_back(expert_id);
    
    // Set specialization if provided
    if (!specialization.empty() && auto* expert = expert_graph_->get_expert_node(expert_id)) {
        size_t copy_size = std::min(specialization.size(), expert->specialization_vector.size());
        for (size_t i = 0; i < copy_size; ++i) {
            expert->specialization_vector[i] = specialization[i];
        }
    }
    
    // Connect to existing experts
    if (expert_ids_.size() > 1) {
        NodeID last_expert = expert_ids_[expert_ids_.size() - 2];
        expert_graph_->add_entanglement_edge(
            last_expert, expert_id,
            ternary::Trit::POSITIVE,
            ternary::EnergyTrit::LOW
        );
    }
    
    return expert_id;
}

void GraphMoERouter::add_entanglement(const NodeID& expert1, const NodeID& expert2, 
                                      ternary::Trit strength) {
    if (expert_graph_->has_node(expert1) && expert_graph_->has_node(expert2)) {
        expert_graph_->add_entanglement_edge(
            expert1, expert2, 
            strength, 
            ternary::EnergyTrit::LOW
        );
    }
}

GraphMoERouter::RoutingResult GraphMoERouter::route_quantum_graph(
    const std::vector<ternary::Trit>& input,
    size_t top_k
) {
    RoutingResult result;
    
    if (!initialized_ || input.empty() || expert_ids_.empty()) {
        return result;
    }
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // Perform quantum message passing
    auto activated_experts = quantum_message_passing(input, 3);
    
    // Select top-k experts using graph attention
    auto selected_experts = graph_attention_selection(input, top_k);
    
    // Build result
    result.selected_experts.reserve(selected_experts.size());
    result.routing_distribution.reserve(selected_experts.size());
    
    ternary::EnergyTrit total_energy = ternary::EnergyTrit::LOW;
    ternary::ProbTrit total_confidence = ternary::ProbTrit::LOW_PROB;
    
    for (const auto* expert : selected_experts) {
        result.selected_experts.push_back(expert->node_id);
        
        // Compute routing probability based on attention
        ternary::ProbTrit prob = compute_symplectic_attention(input, expert);
        result.routing_distribution.push_back(static_cast<ternary::Trit>(prob));
        
        // Accumulate energy and confidence
        total_energy = ternary::energy_ops::add(total_energy, expert->energy_level);
        total_confidence = ternary::prob_ops::add(total_confidence, prob);
    }
    
    result.total_energy = total_energy;
    result.confidence_score = total_confidence;
    
    // Calculate latency
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
    result.routing_latency_us = static_cast<uint32_t>(duration.count());
    
    // Update statistics
    routing_stats_.total_routings++;
    routing_stats_.avg_energy_per_routing = total_energy;
    routing_stats_.avg_confidence = total_confidence;
    
    accumulated_energy_ = ternary::energy_ops::add(accumulated_energy_, total_energy);
    routing_round_++;
    
    return result;
}

std::vector<ExpertNode*> GraphMoERouter::quantum_message_passing(
    const std::vector<ternary::Trit>& input,
    size_t iterations
) {
    std::vector<ExpertNode*> all_experts = expert_graph_->get_all_experts();
    
    for (size_t iter = 0; iter < iterations; ++iter) {
        // Message passing phase
        for (auto* expert : all_experts) {
            if (!expert) continue;
            
            // Get neighbors
            auto neighbors = expert_graph_->get_neighbors(expert->node_id);
            
            // Aggregate messages from neighbors
            std::vector<ternary::Trit> aggregated_message(
                expert->specialization_vector.size(), 
                ternary::Trit::ZERO
            );
            
            for (auto* neighbor : neighbors) {
                if (!neighbor) continue;
                
                // Simple message aggregation (in GF(3) space)
                for (size_t i = 0; i < aggregated_message.size(); ++i) {
                    if (i < neighbor->specialization_vector.size()) {
                        aggregated_message[i] = ternary::trit_ops::add(
                            aggregated_message[i],
                            neighbor->specialization_vector[i]
                        );
                    }
                }
            }
            
            // Update expert state based on aggregated message
            for (size_t i = 0; i < expert->specialization_vector.size(); ++i) {
                if (i < input.size()) {
                    // Combine input with aggregated message
                    ternary::Trit combined = ternary::trit_ops::add(input[i], aggregated_message[i]);
                    expert->specialization_vector[i] = ternary::trit_ops::add(
                        expert->specialization_vector[i],
                        combined
                    );
                }
            }
            
            // Update quantum state (simplified)
            // In a full implementation, this would use stabilizer operations
            expert->load_metric = std::min(100u, expert->load_metric + 1);
        }
    }
    
    return all_experts;
}

std::vector<ExpertNode*> GraphMoERouter::graph_attention_selection(
    const std::vector<ternary::Trit>& input,
    size_t top_k
) {
    std::vector<ExpertNode*> all_experts = expert_graph_->get_all_experts();
    std::vector<std::pair<ternary::ProbTrit, ExpertNode*>> expert_scores;
    
    // Compute attention scores
    for (auto* expert : all_experts) {
        if (!expert) continue;
        
        ternary::ProbTrit score = compute_symplectic_attention(input, expert);
        expert_scores.emplace_back(score, expert);
    }
    
    // Sort by score (descending)
    std::sort(expert_scores.begin(), expert_scores.end(), 
              [](const auto& a, const auto& b) {
                  return static_cast<int>(a.first) > static_cast<int>(b.first);
              });
    
    // Select top-k
    std::vector<ExpertNode*> selected;
    selected.reserve(std::min(top_k, expert_scores.size()));
    
    for (size_t i = 0; i < std::min(top_k, expert_scores.size()); ++i) {
        selected.push_back(expert_scores[i].second);
    }
    
    return selected;
}

ternary::ProbTrit GraphMoERouter::compute_symplectic_attention(
    const std::vector<ternary::Trit>& input,
    const ExpertNode* expert
) const {
    if (!expert || input.empty()) {
        return ternary::ProbTrit::LOW_PROB;
    }
    
    // Compute symplectic inner product between input and expert specialization
    int32_t score = 0;
    size_t min_size = std::min(input.size(), expert->specialization_vector.size());
    
    for (size_t i = 0; i < min_size; ++i) {
        // Symplectic pairing: input_X * expert_Z - input_Z * expert_X
        // Simplified to ternary dot product for this implementation
        int8_t input_val = static_cast<int8_t>(input[i]);
        int8_t expert_val = static_cast<int8_t>(expert->specialization_vector[i]);
        score += input_val * expert_val;
    }
    
    // Normalize score to probability range
    // Higher absolute score = higher probability
    int32_t abs_score = std::abs(score);
    
    if (abs_score > static_cast<int32_t>(min_size) * 2 / 3) {
        return ternary::ProbTrit::HIGH_PROB;
    } else if (abs_score > static_cast<int32_t>(min_size) / 3) {
        return ternary::ProbTrit::MED_PROB;
    } else {
        return ternary::ProbTrit::LOW_PROB;
    }
}

GraphMoERouter::RoutingResult GraphMoERouter::graph_load_balance() {
    RoutingResult result;
    
    // Update load metrics first
    update_load_metrics();
    
    // Get all experts sorted by load
    std::vector<ExpertNode*> all_experts = expert_graph_->get_all_experts();
    std::sort(all_experts.begin(), all_experts.end(),
              [](const ExpertNode* a, const ExpertNode* b) {
                  return a->load_metric < b->load_metric;
              });
    
    // Select least loaded experts
    size_t select_count = std::min(config_.active_experts, all_experts.size());
    result.selected_experts.reserve(select_count);
    
    ternary::EnergyTrit total_energy = ternary::EnergyTrit::LOW;
    
    for (size_t i = 0; i < select_count; ++i) {
        result.selected_experts.push_back(all_experts[i]->node_id);
        result.routing_distribution.push_back(ternary::Trit::POSITIVE);
        total_energy = ternary::energy_ops::add(total_energy, all_experts[i]->energy_level);
    }
    
    result.total_energy = total_energy;
    result.confidence_score = ternary::ProbTrit::MED_PROB;
    result.routing_latency_us = 50;  // Estimated latency
    
    return result;
}

size_t GraphMoERouter::scale_experts_graph_aware(uint32_t target_load) {
    uint32_t current_load = expert_graph_->get_global_load();
    
    if (current_load > target_load + 10) {
        // Scale down - reduce active experts
        size_t new_active = std::max(size_t(1), config_.active_experts * 8 / 10);
        config_.active_experts = new_active;
    } else if (current_load < target_load - 10) {
        // Scale up - increase active experts
        size_t new_active = std::min(config_.max_experts, config_.active_experts * 12 / 10);
        config_.active_experts = new_active;
    }
    
    return config_.active_experts;
}

void GraphMoERouter::update_load_metrics() {
    expert_graph_->update_load_metrics();
}

QGNNGraph::GraphStats GraphMoERouter::get_graph_stats() const {
    return expert_graph_->get_stats();
}

GraphMoERouter::RoutingStats GraphMoERouter::get_routing_stats() const {
    return routing_stats_;
}

void GraphMoERouter::reset_stats() {
    routing_stats_ = RoutingStats{
        .total_routings = 0,
        .avg_latency_us = 0,
        .avg_energy_per_routing = ternary::EnergyTrit::MEDIUM,
        .avg_confidence = ternary::ProbTrit::MED_PROB,
        .load_balance_score = 1.0
    };
    routing_round_ = 0;
    accumulated_energy_ = ternary::EnergyTrit::LOW;
}

std::unique_ptr<GraphMoERouter> create_graph_moe_router(
    const GraphMoERouter::GraphConfig& config
) {
    return std::make_unique<GraphMoERouter>(config);
}

} // namespace q_mini_wasm_v2::core::qgnn
