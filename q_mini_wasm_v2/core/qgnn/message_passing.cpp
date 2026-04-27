#include "message_passing.hpp"
#include "../ternary/trit.hpp"
#include <algorithm>
#include <new>
#include <span>

namespace q_mini_wasm_v2::core::qgnn {

namespace {

std::span<const ternary::Trit> as_span(const std::vector<ternary::Trit>& v) noexcept {
    return std::span<const ternary::Trit>(v.data(), v.size());
}

} // namespace

MessagePassingKernel::MessagePassingKernel(memory::MemoryArena& arena)
    : arena_(arena)
{
}

std::span<const stabilizer::StabilizerTableau> MessagePassingKernel::forward_pass(
    std::span<const TernaryTreeEdge> edges,
    std::span<const stabilizer::StabilizerTableau> node_states
) noexcept {
    const size_t num_nodes = node_states.size();
    if (num_nodes == 0) {
        return {};
    }

    const size_t nbytes = num_nodes * sizeof(stabilizer::StabilizerTableau);
    void* mem = arena_.allocate(nbytes, alignof(stabilizer::StabilizerTableau));
    if (mem == nullptr) {
        return {};
    }

    auto* output_states = static_cast<stabilizer::StabilizerTableau*>(mem);
    for (size_t i = 0; i < num_nodes; ++i) {
        new (output_states + i) stabilizer::StabilizerTableau(node_states[i]);
    }

    for (const auto& edge : edges) {
        if (edge.weight == 0 || edge.source_node >= num_nodes || edge.target_node >= num_nodes) {
            continue;
        }

        const size_t source = edge.source_node;
        const size_t target = edge.target_node;

        const std::vector<ternary::Trit> source_trits = node_states[source].get_pauli_vector();
        const std::vector<ternary::Trit> target_trits = node_states[target].get_pauli_vector();

        const int8_t attention = symplectic_attention(as_span(source_trits), as_span(target_trits));

        if (attention == 0) {
            continue;
        }

        if (edge.weight == 1 && attention == 1) {
            output_states[target].apply_csum(source, target);
            output_states[source].apply_csum(target, source);
        } else if (edge.weight == -1 && attention == -1) {
            output_states[target].apply_phase(target);
            output_states[target].apply_csum(source, target);
            output_states[target].apply_phase(target);
        } else {
            output_states[target].apply_csum(source, target);
        }
    }

    return std::span<const stabilizer::StabilizerTableau>(output_states, num_nodes);
}

} // namespace q_mini_wasm_v2::core::qgnn
