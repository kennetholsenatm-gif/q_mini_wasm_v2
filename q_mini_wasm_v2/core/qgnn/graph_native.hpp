#pragma once

#include "../ternary/trit.hpp"
#include "../stabilizer/tableau.hpp"
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <memory>
#include <cstdint>

namespace q_mini_wasm_v2::core::qgnn {

/**
 * @brief Graph-native node identifier for QGNN
 * 
 * Uses ternary GF(3) space for efficient quantum graph operations
 * Each node ID encodes both position and quantum state information
 */
struct NodeID {
    uint32_t id;  // Unique identifier
    ternary::Trit quantum_state;  // Node quantum state
    
    NodeID(uint32_t identifier = 0, ternary::Trit state = ternary::Trit::ZERO)
        : id(identifier), quantum_state(state) {}
    
    bool operator==(const NodeID& other) const noexcept {
        return id == other.id && quantum_state == other.quantum_state;
    }
    
    bool operator<(const NodeID& other) const noexcept {
        return id < other.id || (id == other.id && quantum_state < other.quantum_state);
    }
};

/**
 * @brief Hash function for NodeID
 */
struct NodeIDHash {
    size_t operator()(const NodeID& node_id) const noexcept {
        return std::hash<uint32_t>{}(node_id.id) ^ 
               (static_cast<size_t>(node_id.quantum_state) << 16);
    }
};

/**
 * @brief Ternary edge weight for quantum graph connections
 * 
 * Represents entanglement strength and coupling in GF(3) space
 */
struct TernaryEdge {
    NodeID source;
    NodeID target;
    ternary::Trit weight;  // Edge weight in GF(3): {-1, 0, 1}
    ternary::EnergyTrit energy_cost;  // Energy cost for edge traversal
    
    TernaryEdge(NodeID src, NodeID tgt, ternary::Trit w = ternary::Trit::ZERO, 
               ternary::EnergyTrit energy = ternary::EnergyTrit::LOW)
        : source(src), target(tgt), weight(w), energy_cost(energy) {}
    
    bool operator==(const TernaryEdge& other) const noexcept {
        return source == other.source && target == other.target && weight == other.weight;
    }
};

/**
 * @brief Graph-native expert node for QGNN MoE routing
 * 
 * Replaces tightly-coupled expert arrays with scalable graph structure
 */
struct ExpertNode {
    NodeID node_id;
    stabilizer::StabilizerTableau quantum_state;  // Expert's quantum state
    std::vector<ternary::Trit> specialization_vector;  // Expert specialization
    ternary::EnergyTrit energy_level;  // Current energy consumption
    uint32_t load_metric;  // Current load (0-100)
    ternary::ProbTrit availability;  // Availability probability
    
    ExpertNode(NodeID id, size_t specialization_dim = 10)
        : node_id(id)
        , quantum_state(8)  // 8 qutrits for expert state
        , specialization_vector(specialization_dim, ternary::Trit::ZERO)
        , energy_level(ternary::EnergyTrit::MEDIUM)
        , load_metric(50)
        , availability(ternary::ProbTrit::HIGH_PROB) {}
};

/**
 * @brief Sparse adjacency list for efficient graph operations
 * 
 * Uses hash maps for O(1) edge access and efficient scaling
 */
class SparseAdjacencyList {
private:
    std::unordered_map<NodeID, std::vector<TernaryEdge>, NodeIDHash> outgoing_edges_;
    std::unordered_map<NodeID, std::vector<TernaryEdge>, NodeIDHash> incoming_edges_;
    std::unordered_set<NodeID, NodeIDHash> nodes_;

public:
    /**
     * @brief Add a node to the graph
     */
    void add_node(const NodeID& node_id) {
        nodes_.insert(node_id);
        if (outgoing_edges_.find(node_id) == outgoing_edges_.end()) {
            outgoing_edges_[node_id] = std::vector<TernaryEdge>();
        }
        if (incoming_edges_.find(node_id) == incoming_edges_.end()) {
            incoming_edges_[node_id] = std::vector<TernaryEdge>();
        }
    }
    
    /**
     * @brief Add a directed edge to the graph
     */
    void add_edge(const TernaryEdge& edge) {
        // Ensure nodes exist
        add_node(edge.source);
        add_node(edge.target);
        
        // Add edge to adjacency lists
        outgoing_edges_[edge.source].push_back(edge);
        incoming_edges_[edge.target].push_back(edge);
    }
    
    /**
     * @brief Get outgoing edges for a node
     */
    const std::vector<TernaryEdge>& get_outgoing_edges(const NodeID& node_id) const {
        static const std::vector<TernaryEdge> empty;
        auto it = outgoing_edges_.find(node_id);
        return it != outgoing_edges_.end() ? it->second : empty;
    }
    
    /**
     * @brief Get incoming edges for a node
     */
    const std::vector<TernaryEdge>& get_incoming_edges(const NodeID& node_id) const {
        static const std::vector<TernaryEdge> empty;
        auto it = incoming_edges_.find(node_id);
        return it != incoming_edges_.end() ? it->second : empty;
    }
    
    /**
     * @brief Check if node exists in graph
     */
    bool has_node(const NodeID& node_id) const {
        return nodes_.find(node_id) != nodes_.end();
    }
    
    /**
     * @brief Get all nodes in the graph
     */
    std::vector<NodeID> get_all_nodes() const {
        std::vector<NodeID> nodes;
        nodes.reserve(nodes_.size());
        for (const auto& node : nodes_) {
            nodes.push_back(node);
        }
        return nodes;
    }
    
    /**
     * @brief Get number of nodes in graph
     */
    size_t node_count() const noexcept {
        return nodes_.size();
    }
    
    /**
     * @brief Get number of edges in graph
     */
    size_t edge_count() const noexcept {
        size_t total = 0;
        for (const auto& [node, edges] : outgoing_edges_) {
            total += edges.size();
        }
        return total;
    }
    
    /**
     * @brief Clear the graph
     */
    void clear() {
        outgoing_edges_.clear();
        incoming_edges_.clear();
        nodes_.clear();
    }
};

/**
 * @brief QGNN Graph structure for scalable quantum neural networks
 * 
 * Replaces O(N²) arrays with O(E) sparse graph representation
 * Enables efficient message passing and quantum entanglement operations
 */
class QGNNGraph {
private:
    SparseAdjacencyList adjacency_;
    std::unordered_map<NodeID, std::unique_ptr<ExpertNode>, NodeIDHash> expert_nodes_;
    ternary::EnergyTrit total_energy_;
    uint32_t global_load_;
    
public:
    /**
     * @brief Construct QGNN graph
     */
    QGNNGraph() : total_energy_(ternary::EnergyTrit::LOW), global_load_(0) {}
    
    /**
     * @brief Add an expert node to the graph
     */
    NodeID add_expert_node(size_t specialization_dim = 10) {
        static uint32_t next_id = 0;
        NodeID node_id(next_id++, ternary::Trit::ZERO);
        
        auto expert_node = std::make_unique<ExpertNode>(node_id, specialization_dim);
        expert_nodes_[node_id] = std::move(expert_node);
        adjacency_.add_node(node_id);
        
        return node_id;
    }
    
    /**
     * @brief Add entanglement edge between experts
     */
    void add_entanglement_edge(const NodeID& source, const NodeID& target, 
                               ternary::Trit entanglement_strength,
                               ternary::EnergyTrit energy_cost = ternary::EnergyTrit::LOW) {
        TernaryEdge edge(source, target, entanglement_strength, energy_cost);
        adjacency_.add_edge(edge);
        
        // Update total energy
        total_energy_ = ternary::energy_ops::add(total_energy_, energy_cost);
    }
    
    /**
     * @brief Get expert node by ID
     */
    ExpertNode* get_expert_node(const NodeID& node_id) {
        auto it = expert_nodes_.find(node_id);
        return it != expert_nodes_.end() ? it->second.get() : nullptr;
    }
    
    /**
     * @brief Get expert node by ID (const)
     */
    const ExpertNode* get_expert_node(const NodeID& node_id) const {
        auto it = expert_nodes_.find(node_id);
        return it != expert_nodes_.end() ? it->second.get() : nullptr;
    }
    
    /**
     * @brief Get all expert nodes
     */
    std::vector<ExpertNode*> get_all_experts() {
        std::vector<ExpertNode*> experts;
        experts.reserve(expert_nodes_.size());
        
        for (auto& [node_id, expert_node] : expert_nodes_) {
            experts.push_back(expert_node.get());
        }
        
        return experts;
    }
    
    /**
     * @brief Get neighbors of a node
     */
    std::vector<ExpertNode*> get_neighbors(const NodeID& node_id) {
        std::vector<ExpertNode*> neighbors;
        
        const auto& edges = adjacency_.get_outgoing_edges(node_id);
        neighbors.reserve(edges.size());
        
        for (const auto& edge : edges) {
            if (auto* neighbor = get_expert_node(edge.target)) {
                neighbors.push_back(neighbor);
            }
        }
        
        return neighbors;
    }
    
    /**
     * @brief Update global load metrics
     */
    void update_load_metrics() {
        uint32_t total_load = 0;
        for (const auto& [node_id, expert_node] : expert_nodes_) {
            total_load += expert_node->load_metric;
        }
        
        global_load_ = expert_nodes_.empty() ? 0 : total_load / expert_nodes_.size();
    }
    
    /**
     * @brief Get global load metric
     */
    uint32_t get_global_load() const noexcept {
        return global_load_;
    }
    
    /**
     * @brief Get total energy consumption
     */
    ternary::EnergyTrit get_total_energy() const noexcept {
        return total_energy_;
    }
    
    /**
     * @brief Get graph statistics
     */
    struct GraphStats {
        size_t node_count;
        size_t edge_count;
        uint32_t global_load;
        ternary::EnergyTrit total_energy;
        double avg_degree;
    };
    
    GraphStats get_stats() const {
        GraphStats stats;
        stats.node_count = adjacency_.node_count();
        stats.edge_count = adjacency_.edge_count();
        stats.global_load = global_load_;
        stats.total_energy = total_energy_;
        stats.avg_degree = stats.node_count > 0 ? 
            static_cast<double>(stats.edge_count) / stats.node_count : 0.0;
        
        return stats;
    }
    
    /**
     * @brief Clear the graph
     */
    void clear() {
        adjacency_.clear();
        expert_nodes_.clear();
        total_energy_ = ternary::EnergyTrit::LOW;
        global_load_ = 0;
    }
    
    /**
     * @brief Check if graph is empty
     */
    bool empty() const noexcept {
        return expert_nodes_.empty();
    }
};

/**
 * @brief Factory function for creating QGNN graphs
 */
std::unique_ptr<QGNNGraph> create_qgnn_graph();

} // namespace q_mini_wasm_v2::core::qgnn
