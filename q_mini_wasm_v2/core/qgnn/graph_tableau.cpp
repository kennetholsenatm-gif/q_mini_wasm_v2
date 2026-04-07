#include "graph_tableau.hpp"
#include <algorithm>
#include <stdexcept>

namespace q_mini_wasm_v2::core::qgnn {

// ============================================================================
// GF(3) Arithmetic Helpers
// ============================================================================

Trit GraphTableau::gf3_mul(Trit a, Trit b) {
    // Multiplication in GF(3): map {-1,0,1} to {2,0,1}, multiply mod 3, map back
    auto to_gf = [](Trit x) -> int8_t { return (x == -1) ? 2 : x; };
    auto from_gf = [](int8_t x) -> Trit { return (x == 2) ? -1 : static_cast<Trit>(x); };
    return from_gf((to_gf(a) * to_gf(b)) % 3);
}

Trit GraphTableau::omega_phase(Trit a) {
    // ω^a where ω = e^(2πi/3), the primitive 3rd root of unity
    // Phase values: ω^0=1, ω^1=ω, ω^2=ω², ω^3=1, ...
    // Returns {0, 1, 2} corresponding to phase powers
    return (a % 3 + 3) % 3;  // Normalize to {0, 1, 2}
}

// ============================================================================
// Constructor / Initialization
// ============================================================================

GraphTableau::GraphTableau(size_t num_qutrits)
    : num_qutrits_(num_qutrits)
    , num_generators_(2 * num_qutrits)
    , x_part_(2 * num_qutrits, std::vector<Trit>(num_qutrits, 0))
    , z_part_(2 * num_qutrits, std::vector<Trit>(num_qutrits, 0))
    , phases_(2 * num_qutrits, 0)
    , node_to_tree_order_(num_qutrits)
    , tree_to_node_order_(num_qutrits)
{
    // Initialize generators: first n are X-type, second n are Z-type
    // Standard stabilizer state: all |0⟩ (all +1 eigenvalues of Z)
    for (size_t i = 0; i < num_qutrits; ++i) {
        // X generators start as X_i
        x_part_[i][i] = 1;
        // Z generators start as Z_i
        z_part_[num_qutrits + i][i] = 1;
    }
    
    // Initialize identity tree mapping
    for (size_t i = 0; i < num_qutrits; ++i) {
        node_to_tree_order_[i] = i;
        tree_to_node_order_[i] = i;
    }
}

GraphTableau::GraphTableau(size_t num_nodes, 
                              const std::vector<std::pair<size_t, size_t>>& edges)
    : GraphTableau(num_nodes)
{
    // Apply entangling operations for each edge
    for (const auto& [a, b] : edges) {
        if (a < num_nodes && b < num_nodes) {
            add_edge(a, b);
        }
    }
}

// ============================================================================
// Single-Qutrit Gates
// ============================================================================

void GraphTableau::apply_hadamard(size_t qutrit_idx) {
    if (qutrit_idx >= num_qutrits_) return;
    
    // Hadamard: X <-> Z swap for this qutrit
    // For each generator, swap X and Z components
    for (size_t row = 0; row < num_generators_; ++row) {
        std::swap(x_part_[row][qutrit_idx], z_part_[row][qutrit_idx]);
    }
    
    // Update phase: requires tracking symplectic inner product
    // Simplified: actual implementation would track phase properly
}

void GraphTableau::apply_phase(size_t qutrit_idx) {
    if (qutrit_idx >= num_qutrits_) return;
    
    // Phase gate S: Z -> Z, X -> XZ
    // Update X generators: X -> XZ means add Z component
    for (size_t row = 0; row < num_qutrits_; ++row) {
        if (x_part_[row][qutrit_idx] != 0) {
            z_part_[row][qutrit_idx] = gf3_add(z_part_[row][qutrit_idx], x_part_[row][qutrit_idx]);
        }
    }
}

// ============================================================================
// Multi-Qutrit Gates
// ============================================================================

void GraphTableau::apply_csum(size_t control_idx, size_t target_idx) {
    if (control_idx >= num_qutrits_ || target_idx >= num_qutrits_) return;
    if (control_idx == target_idx) return;
    
    // Controlled-SUM: CX gates in ternary
    // X_c -> X_c X_t (control's X propagates to target)
    // Z_t -> Z_c^-1 Z_t (target's Z propagates backward to control)
    
    for (size_t row = 0; row < num_generators_; ++row) {
        // If generator has X on control, add X to target
        if (x_part_[row][control_idx] != 0) {
            x_part_[row][target_idx] = gf3_add(x_part_[row][target_idx], x_part_[row][control_idx]);
        }
        // If generator has Z on target, subtract Z from control
        if (z_part_[row][target_idx] != 0) {
            Trit neg_z = gf3_mul(z_part_[row][target_idx], -1);
            z_part_[row][control_idx] = gf3_add(z_part_[row][control_idx], neg_z);
        }
    }
}

void GraphTableau::apply_cz(size_t control_idx, size_t target_idx) {
    if (control_idx >= num_qutrits_ || target_idx >= num_qutrits_) return;
    
    // CZ = H_target · CX · H_target
    apply_hadamard(target_idx);
    apply_csum(control_idx, target_idx);
    apply_hadamard(target_idx);
}

void GraphTableau::apply_message_passing(size_t from_node, size_t to_node, Trit weight) {
    if (from_node >= num_qutrits_ || to_node >= num_qutrits_) return;
    
    // Message passing with ternary weight
    // Weight ∈ {-1, 0, +1} determines message phase and propagation
    
    if (weight == 0) return;  // No message
    
    // Apply weighted CZ for message propagation
    // For +1: standard entanglement
    // For -1: conjugate operation
    apply_cz(from_node, to_node);
    
    if (weight == -1) {
        // Additional phase correction for negative weight
        apply_phase(from_node);
        apply_phase(from_node);  // Phase^2 = phase * phase
    }
}

// ============================================================================
// Graph Operations
// ============================================================================

void GraphTableau::add_node() {
    size_t new_idx = num_qutrits_++;
    num_generators_ = 2 * num_qutrits_;
    
    // Extend tableau
    for (auto& row : x_part_) {
        row.push_back(0);
    }
    for (auto& row : z_part_) {
        row.push_back(0);
    }
    
    // Add new rows for new qutrit
    x_part_.emplace_back(num_qutrits_, 0);
    z_part_.emplace_back(num_qutrits_, 0);
    x_part_.emplace_back(num_qutrits_, 0);
    z_part_.emplace_back(num_qutrits_, 0);
    phases_.push_back(0);
    phases_.push_back(0);
    
    // Initialize new generator
    x_part_[new_idx][new_idx] = 1;
    z_part_[num_qutrits_ + new_idx][new_idx] = 1;
    
    // Update tree mapping
    node_to_tree_order_.push_back(new_idx);
    tree_to_node_order_.push_back(new_idx);
}

void GraphTableau::add_edge(size_t node_a, size_t node_b) {
    if (node_a >= num_qutrits_ || node_b >= num_qutrits_) return;
    
    // Create entanglement via CZ
    apply_cz(node_a, node_b);
}

Trit GraphTableau::measure(size_t node_idx) {
    if (node_idx >= num_qutrits_) return 0;
    
    // Measure in computational basis (Z eigenvalue)
    // Find anticommuting generators and update
    
    // Simplified measurement: return stored phase
    // Full implementation requires Gaussian elimination
    
    return phases_[num_qutrits_ + node_idx];
}

// ============================================================================
// Properties
// ============================================================================

size_t GraphTableau::memory_bytes() const {
    // x_part: 2n × n trits
    // z_part: 2n × n trits
    // phases: 2n trits
    // Total: 4n² + 2n trits
    // Packed: ~4n² bytes (1 trit ≈ 1 byte in this implementation)
    return 4 * num_qutrits_ * num_qutrits_ * sizeof(Trit);
}

bool GraphTableau::is_valid() const {
    // Check generators commute
    for (size_t i = 0; i < num_generators_; ++i) {
        for (size_t j = i + 1; j < num_generators_; ++j) {
            // Compute symplectic inner product
            Trit result = 0;
            for (size_t k = 0; k < num_qutrits_; ++k) {
                result = gf3_add(result, gf3_mul(x_part_[i][k], z_part_[j][k]));
                result = gf3_add(result, gf3_mul(-x_part_[j][k], z_part_[i][k]));
            }
            if (result != 0) return false;  // Generators don't commute
        }
    }
    return true;
}

size_t GraphTableau::stabilizer_weight(size_t generator_idx) const {
    if (generator_idx >= num_generators_) return 0;
    
    size_t weight = 0;
    for (size_t i = 0; i < num_qutrits_; ++i) {
        if (x_part_[generator_idx][i] != 0 || z_part_[generator_idx][i] != 0) {
            ++weight;
        }
    }
    return weight;
}

std::vector<std::pair<size_t, size_t>> GraphTableau::get_edges() const {
    std::vector<std::pair<size_t, size_t>> edges;
    
    // Detect entanglement by checking which nodes have correlated stabilizers
    // Two nodes are "connected" if they share non-trivial stabilizer generators
    for (size_t i = 0; i < num_qutrits_; ++i) {
        for (size_t j = i + 1; j < num_qutrits_; ++j) {
            bool entangled = false;
            
            // Check if nodes i and j are correlated in any generator
            for (size_t gen = 0; gen < num_generators_ && !entangled; ++gen) {
                // Both nodes have non-identity Pauli on same generator = entanglement
                bool i_non_trivial = (x_part_[gen][i] != 0) || (z_part_[gen][i] != 0);
                bool j_non_trivial = (x_part_[gen][j] != 0) || (z_part_[gen][j] != 0);
                
                if (i_non_trivial && j_non_trivial) {
                    entangled = true;
                }
            }
            
            if (entangled) {
                edges.emplace_back(i, j);
            }
        }
    }
    
    return edges;
}

void GraphTableau::remove_edge(size_t node_a, size_t node_b) {
    if (node_a >= num_qutrits_ || node_b >= num_qutrits_) return;
    if (node_a == node_b) return;
    
    // To remove edge, we apply inverse entangling operation
    // CZ is self-inverse, so apply CZ again to undo
    apply_cz(node_a, node_b);
}

// ============================================================================
// Ternary-Tree Mapping (Phase 3)
// ============================================================================

void GraphTableau::map_to_ternary_tree() {
    // Optimize memory layout for ternary-tree structure
    // Improves cache locality for graph operations
    
    // Simple heuristic: sort nodes by degree (high degree = root)
    std::vector<std::pair<size_t, size_t>> degrees;  // (degree, node)
    
    for (size_t node = 0; node < num_qutrits_; ++node) {
        size_t deg = 0;
        for (size_t gen = 0; gen < num_generators_; ++gen) {
            if (x_part_[gen][node] != 0 || z_part_[gen][node] != 0) {
                ++deg;
            }
        }
        degrees.emplace_back(deg, node);
    }
    
    std::sort(degrees.rbegin(), degrees.rend());  // Sort by degree descending
    
    // Build tree ordering
    for (size_t i = 0; i < degrees.size(); ++i) {
        tree_to_node_order_[i] = degrees[i].second;
        node_to_tree_order_[degrees[i].second] = i;
    }
}

size_t GraphTableau::get_tree_order(size_t node_idx) const {
    if (node_idx >= num_qutrits_) return node_idx;
    return node_to_tree_order_[node_idx];
}

// ============================================================================
// Internal Operations
// ============================================================================

void GraphTableau::row_add(size_t target, size_t source) {
    // Add generator 'source' to 'target' (mod 3)
    for (size_t i = 0; i < num_qutrits_; ++i) {
        x_part_[target][i] = gf3_add(x_part_[target][i], x_part_[source][i]);
        z_part_[target][i] = gf3_add(z_part_[target][i], z_part_[source][i]);
    }
    phases_[target] = gf3_add(phases_[target], phases_[source]);
}

void GraphTableau::swap_rows(size_t a, size_t b) {
    x_part_[a].swap(x_part_[b]);
    z_part_[a].swap(z_part_[b]);
    std::swap(phases_[a], phases_[b]);
}

void GraphTableau::gaussian_elimination() {
    // Put tableau in reduced row echelon form
    // Required for measurement operations
    
    size_t rank = 0;
    for (size_t col = 0; col < num_qutrits_ && rank < num_generators_; ++col) {
        // Find pivot
        size_t pivot = num_generators_;
        for (size_t row = rank; row < num_generators_; ++row) {
            if (x_part_[row][col] != 0 || z_part_[row][col] != 0) {
                pivot = row;
                break;
            }
        }
        
        if (pivot == num_generators_) continue;
        
        swap_rows(rank, pivot);
        
        // Eliminate column
        for (size_t row = 0; row < num_generators_; ++row) {
            if (row != rank && (x_part_[row][col] != 0 || z_part_[row][col] != 0)) {
                row_add(row, rank);
            }
        }
        
        ++rank;
    }
}

// ============================================================================
// TropicalGNNLayer
// ============================================================================

TropicalGNNLayer::TropicalGNNLayer(const std::vector<Edge>& edges)
    : edges_(edges)
{
    // Determine number of nodes
    num_nodes_ = 0;
    for (const auto& e : edges) {
        num_nodes_ = std::max(num_nodes_, std::max(e.from, e.to) + 1);
    }
}

std::vector<Trit> TropicalGNNLayer::forward(const std::vector<Trit>& node_features) {
    std::vector<Trit> output(num_nodes_, 0);
    
    // Tropical message passing: max over neighbors
    for (size_t node = 0; node < num_nodes_; ++node) {
        Trit max_val = -1;
        
        for (const auto& edge : edges_) {
            if (edge.to == node) {
                // Tropical addition: max
                Trit msg = node_features[edge.from] + edge.weight;
                if (msg > max_val) max_val = msg;
            }
        }
        
        output[node] = max_val;
    }
    
    return output;
}

std::vector<float> TropicalGNNLayer::compute_attention(const GraphTableau& tableau) {
    // Symplectic attention: measure stabilizer weights as importance
    std::vector<float> attention(num_nodes_, 0.0f);
    
    for (size_t node = 0; node < num_nodes_; ++node) {
        // Attention = normalized stabilizer weight
        size_t weight = 0;
        for (size_t gen = 0; gen < 2 * num_nodes_; ++gen) {
            // Check if generator acts on this node
            if (tableau.stabilizer_weight(gen) > 0) {
                ++weight;
            }
        }
        attention[node] = static_cast<float>(weight) / (2 * num_nodes_);
    }
    
    return attention;
}

// ============================================================================
// ZXOptimizer
// ============================================================================

bool ZXOptimizer::is_wigner_positive(const GraphTableau& tableau) {
    // Check if state has non-negative Wigner function
    // Simplified: check if tableau is in a form representing a stabilizer state
    return tableau.is_valid();
}

void ZXOptimizer::optimize(GraphTableau& tableau) {
    // Apply ZX-calculus rewrite rules
    // 1. Fuse connected nodes of same color
    // 2. Apply bialgebra rules
    // 3. Remove redundant spiders
    
    // Simplified: just run Gaussian elimination
    tableau.gaussian_elimination();
}

bool ZXOptimizer::has_non_clifford(const GraphTableau& tableau) const {
    // Check for magic state injection (T gate equivalent in GF(3))
    // Non-Clifford operations break Gottesman-Knill
    
    // Simplified: check stabilizer weights for non-Clifford signatures
    for (size_t gen = 0; gen < tableau.num_qutrits() * 2; ++gen) {
        if (tableau.stabilizer_weight(gen) > 3) {
            // High-weight generators may indicate non-Clifford
            return true;
        }
    }
    return false;
}

} // namespace q_mini_wasm_v2::core::qgnn
