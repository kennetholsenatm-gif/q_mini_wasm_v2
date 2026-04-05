#include "message_passing.hpp"
#include <algorithm>
#include <execution>

namespace q_mini_wasm_v2::core::qgnn {

MessagePassingKernel::MessagePassingKernel(memory::MemoryArena& arena)
    : arena_(arena)
{
}

std::span<const stabilizer::StabilizerTableau> MessagePassingKernel::forward_pass(
    std::span<const int8_t> graph_adjacency,
    std::span<const stabilizer::StabilizerTableau> node_states
) noexcept {
    const size_t num_nodes = node_states.size();
    const size_t edge_stride = num_nodes;

    // Allocate output buffer from memory arena
    auto* output_states = arena_.allocate_array<stabilizer::StabilizerTableau>(num_nodes);

    // Parallel message passing iteration
    #ifdef _OPENMP
    #pragma omp parallel for
    #endif
    for (size_t target = 0; target < num_nodes; ++target) {
        stabilizer::StabilizerTableau accumulated = node_states[target].clone();

        // Aggregate messages from all neighbors
        for (size_t source = 0; source < num_nodes; ++source) {
            if (source == target) continue;

            const int8_t edge_weight = graph_adjacency[source * edge_stride + target];
            
            if (edge_weight == 0) continue; // No connection

            // Calculate symplectic attention coefficient
            auto source_trits = node_states[source].get_pauli_vector();
            auto target_trits = node_states[target].get_pauli_vector();
            
            const int8_t attention = symplectic_attention(source_trits, target_trits);
            
            if (attention == 0) continue; // No alignment, skip

            // Apply controlled entanglement gate based on edge weight and attention
            if (edge_weight == 1 && attention == 1) {
                accumulated.apply_csum(source, target);
            } else if (edge_weight == -1 && attention == -1) {
                accumulated.apply_inverse_csum(source, target);
            } else {
                accumulated.apply_swap(source, target);
            }
        }

        // Store updated node state
        output_states[target] = std::move(accumulated);
    }

    return std::span<const stabilizer::StabilizerTableau>(output_states, num_nodes);
}

} // namespace q_mini_wasm_v2::core::qgnn