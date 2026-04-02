#pragma once

#include <vector>
#include <string>
#include <memory>
#include <functional>

namespace q_mini_wasm_v2::wui {

/**
 * @brief Node visual states based on cognitive load management
 * 
 * Implements visual hierarchy per research:
 * - Critical path: high contrast, distinct geometry
 * - Secondary: lower opacity, muted colors
 * - Tertiary: minimal visual weight
 */
enum class NodeVisualState {
    CRITICAL,       // High-contrast borders, elevated shadows
    ACTIVE,         // Standard visibility
    INACTIVE,       // Lower opacity
    ERROR,          // Red indicators for flow state preservation
    SUCCESS         // Green indicators for positive feedback
};

/**
 * @brief Node types for Gestalt grouping
 * 
 * Law of Similarity: Nodes sharing roles share shapes/colors
 */
enum class NodeType {
    INPUT,          // Data ingestion nodes
    PROCESSING,     // Computation nodes (tableau, MoE)
    OUTPUT,         // Result nodes
    ROUTING,        // MoE routing nodes
    STORAGE,        // Memory/storage nodes
    CONTROL         // Flow control nodes
};

/**
 * @brief Graph node implementing cognitive ergonomics principles
 * 
 * Based on research "Cognitive Ergonomics in Complex Web User Interfaces":
 * - Visual hierarchy via pre-attentive processing
 * - Gestalt grouping (proximity, similarity)
 * - Progressive disclosure for complexity management
 * - Hick-Hyman law for decision optimization
 * - Fitts's law for target acquisition
 */
struct GraphNode {
    // Identity
    std::string id;
    std::string label;
    NodeType type;
    NodeVisualState state;
    
    // Position (for spatial context preservation)
    double x;
    double y;
    
    // Visual properties (Gestalt principles)
    struct VisualProperties {
        double opacity;         // 0.0-1.0 for visual hierarchy
        std::string color;      // Color based on type (similarity)
        std::string shape;      // Shape based on role
        double size;            // Size based on importance
        bool show_shadow;       // Elevation indicator
    } visual;
    
    // Progressive disclosure state
    struct DisclosureState {
        bool expanded;          // Is detailed view visible?
        size_t visible_options; // Hick's Law: 5-9 max
        std::vector<std::string> hidden_details;
    } disclosure;
    
    // Connection points for edges
    std::vector<std::string> input_ports;
    std::vector<std::string> output_ports;
    
    // Data payload
    std::vector<int8_t> data;  // Ternary data (GF(3) values)
};

/**
 * @brief Connection edge between nodes
 */
struct GraphEdge {
    std::string id;
    std::string source_node;
    std::string source_port;
    std::string target_node;
    std::string target_port;
    
    // Visual properties
    double width;
    std::string color;
    bool animated;  // For active data flow indication
};

/**
 * @brief Visual graph container implementing Gestalt principles
 * 
 * - Law of Proximity: Related nodes clustered
 * - Law of Prägnanz: Simple, ordered layout
 * - Spatial context preservation (no context-destroying drilling)
 */
class VisualGraph {
public:
    VisualGraph();
    ~VisualGraph();
    
    // Node management
    void add_node(const GraphNode& node);
    void remove_node(const std::string& id);
    GraphNode* get_node(const std::string& id);
    std::vector<GraphNode*> get_nodes_by_type(NodeType type);
    
    // Edge management
    void add_edge(const GraphEdge& edge);
    void remove_edge(const std::string& id);
    
    // Layout operations (Gestalt grouping)
    void auto_layout();  // Apply proximity grouping
    void cluster_by_type();  // Apply similarity grouping
    
    // Progressive disclosure
    void expand_node(const std::string& id);
    void collapse_node(const std::string& id);
    void set_visible_options(const std::string& id, size_t count);
    
    // Visual state management
    void set_node_state(const std::string& id, NodeVisualState state);
    void highlight_critical_path();
    void show_error(const std::string& node_id, const std::string& message);
    
    // Query operations
    size_t node_count() const { return nodes_.size(); }
    size_t edge_count() const { return edges_.size(); }
    
private:
    std::vector<GraphNode> nodes_;
    std::vector<GraphEdge> edges_;
    
    // Spatial index for proximity queries
    void update_spatial_index();
};

/**
 * @brief Dashboard metrics display
 * 
 * Implements Miller's Law: Chunk metrics into 7±2 groups
 */
struct DashboardMetrics {
    // Chunked metric groups (Miller's Law compliance)
    struct PerformanceChunk {
        double throughput;      // Operations per second
        double latency_avg;     // Average latency
        double latency_p99;     // 99th percentile
    } performance;
    
    struct ResourceChunk {
        double cpu_usage;
        double memory_usage;
        double energy_estimate;  // pJ per operation
    } resources;
    
    struct QualityChunk {
        double accuracy;
        double goodness_delta;  // Forward-Forward metric
        size_t active_experts;  // MoE active count
    } quality;
};

/**
 * @brief Error display for flow state preservation
 * 
 * Research: "Graceful Error Recovery and Flow State Preservation"
 * - Inline error indicators (proximal to source)
 * - Actionable resolution suggestions
 * - Deep links to documentation
 */
struct ErrorDisplay {
    std::string node_id;
    std::string message;
    std::string resolution;
    std::string doc_link;
    bool inline_display;  // Show near error source, not at top
};

} // namespace q_mini_wasm_v2::wui