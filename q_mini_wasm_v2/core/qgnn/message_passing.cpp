#include "message_passing.hpp"
#include "../ternary/trit.hpp"
#include <algorithm>
#include <execution>

namespace q_mini_wasm_v2::core::qgnn {

using namespace core::ternary;

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
            // Standard CSUM (CNOT) - conjugate target X by source X
            output_states[target].apply_csum(source, target);
            output_states[source].apply_csum(target, source);
        } else if (edge.weight == -1 && attention == -1) {
            // Inverse CSUM: apply phase then csum (logical inverse)
            // In GF(3), inverse of CSUM requires: target Z -= source Z, target X -= source X
            output_states[target].apply_phase(target);  // ω† phase
            output_states[target].apply_csum(source, target);
            output_states[target].apply_phase(target);  // Complete inverse
        } else {
            // General edge case: apply full GF(3) symplectic transformation
            // CNOT cascade for proper stabilizer propagation
            // Rule: target's X picks up source's X (mod 3)
            //       source's Z picks up target's Z (mod 3)
            
            // First: propagate X from source to target
            auto source_pauli = output_states[source].get_pauli_vector();
            auto target_pauli = output_states[target].get_pauli_vector();
            
            // Symplectic inner product determines coupling strength
            int8_t coupling = 0;
            for (size_t i = 0; i < std::min(source_pauli.size(), target_pauli.size()); ++i) {
                coupling = ternary::trit_ops::add(coupling, 
                    ternary::trit_ops::multiply(source_pauli[i], target_pauli[i]));
            }
            
            // Apply CSUM with coupling coefficient
            if (coupling != 0) {
                output_states[target].apply_csum(source, target);
                // Propagate phase based on coupling
                if (coupling == 1) {
                    output_states[target].apply_phase(target);
                } else if (coupling == -1) {
                    output_states[target].apply_phase(target);
                    output_states[target].apply_phase(target);
                }
            }
        }
    }

    return std::span<const stabilizer::StabilizerTableau>(output_states, num_nodes);
}

} // namespace q_mini_wasm_v2::core::qgnn