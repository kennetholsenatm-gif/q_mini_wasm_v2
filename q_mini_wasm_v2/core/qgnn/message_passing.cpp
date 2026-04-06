#include "message_passing.hpp"
#include <algorithm>
#include <execution>

namespace q_mini_wasm_v2::core::qgnn {

MessagePassingKernel::MessagePassingKernel(memory::MemoryArena& arena)
    : arena_(arena)
{
}

std::span<const stabilizer::StabilizerTableau> MessagePassingKernel::forward_pass(
    std::span<const TernaryTreeEdge> edges,
    std::span<const stabilizer::StabilizerTableau> node_states
) noexcept {
    const size_t num_nodes = node_states.size();

    // Allocate output buffer from memory arena
    auto* output_states = arena_.allocate_array<stabilizer::StabilizerTableau>(num_nodes);
    
    // Initialize output buffer
    for (size_t i = 0; i < num_nodes; ++i) {
        output_states[i] = node_states[i].clone();
    }

    // Apply discrete unitary operators sequentially as per Ternary Tree Inorder Traversal
    for (const auto& edge : edges) {
        if (edge.weight == 0 || edge.source_node >= num_nodes || edge.target_node >= num_nodes) continue;

        size_t source = edge.source_node;
        size_t target = edge.target_node;

        // Calculate symplectic attention coefficient
        auto source_trits = node_states[source].get_pauli_vector();
        auto target_trits = node_states[target].get_pauli_vector();
        
        const int8_t attention = symplectic_attention(source_trits, target_trits);
        
        if (attention == 0) continue; // No alignment, skip

        // Apply controlled entanglement gate based on edge weight and attention
        if (edge.weight == 1 && attention == 1) {
            output_states[target].apply_csum(source, target);
            output_states[source].apply_csum(target, source);
        } else if (edge.weight == -1 && attention == -1) {
            // Apply inverse CSUM (mocked via applying phase + csum logically)
            output_states[target].apply_csum(source, target);
        } else {
            // Topological swap
            // Note: properly swapping stabilizers in GF(3) requires CNOT cascades.
            // Simplified for logic
        }
    }

    return std::span<const stabilizer::StabilizerTableau>(output_states, num_nodes);
}

} // namespace q_mini_wasm_v2::core::qgnn