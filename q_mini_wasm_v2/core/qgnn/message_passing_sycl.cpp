#include "message_passing_sycl.hpp"
#include <new>
#include <span>
#include <vector>

namespace q_mini_wasm_v2::core::qgnn {

namespace {

int8_t symplectic_for_pair(const stabilizer::StabilizerTableau& a, const stabilizer::StabilizerTableau& b) {
    const std::vector<ternary::Trit> va = a.get_pauli_vector();
    const std::vector<ternary::Trit> vb = b.get_pauli_vector();
    return MessagePassingKernel::symplectic_attention(
        std::span<const ternary::Trit>(va.data(), va.size()),
        std::span<const ternary::Trit>(vb.data(), vb.size()));
}

} // namespace

MessagePassingSycl::MessagePassingSycl(sycl::queue& queue, memory::MemoryArena& arena) noexcept
    : queue_(queue)
    , arena_(arena)
{
}

std::span<const stabilizer::StabilizerTableau> MessagePassingSycl::forward_pass(
    std::span<const int8_t> graph_adjacency,
    std::span<const stabilizer::StabilizerTableau> node_states
) noexcept
{
    const size_t node_count = node_states.size();
    if (node_count == 0) {
        return {};
    }

    const size_t nbytes = node_count * sizeof(stabilizer::StabilizerTableau);
    void* mem = arena_.allocate(nbytes, alignof(stabilizer::StabilizerTableau));
    if (mem == nullptr) {
        return {};
    }

    auto* output_states = static_cast<stabilizer::StabilizerTableau*>(mem);
    for (size_t i = 0; i < node_count; ++i) {
        new (output_states + i) stabilizer::StabilizerTableau(node_states[i]);
    }

    launch_message_passing_kernel(
        graph_adjacency.data(),
        node_states.data(),
        output_states,
        node_count
    );

    return std::span<const stabilizer::StabilizerTableau>(output_states, node_count);
}

void MessagePassingSycl::batch_symplectic_attention(
    std::span<const stabilizer::StabilizerTableau> node_states,
    std::span<int8_t> attention_matrix
) noexcept
{
    const size_t node_count = node_states.size();
    // Host task: symplectic_attention uses StabilizerTableau host API; GPU parallel_for would need a dedicated kernel.
    queue_.submit([&](sycl::handler& cgh) {
        cgh.host_task([=]() {
            for (size_t i = 0; i < node_count; ++i) {
                for (size_t j = 0; j < node_count; ++j) {
                    attention_matrix[i * node_count + j] = symplectic_for_pair(node_states[i], node_states[j]);
                }
            }
        });
    }).wait();
}

sycl::queue& MessagePassingSycl::get_queue() noexcept
{
    return queue_;
}

void MessagePassingSycl::launch_message_passing_kernel(
    const int8_t* adjacency,
    const stabilizer::StabilizerTableau* node_states,
    stabilizer::StabilizerTableau* output_states,
    size_t node_count
) noexcept
{
    // Host task: same aggregation as the previous parallel_for sketch, but StabilizerTableau is host-only.
    // A future GPU path should serialize tableaux to buffers and launch a dedicated SYCL kernel.
    queue_.submit([&](sycl::handler& cgh) {
        cgh.host_task([=]() {
            for (size_t node_id = 0; node_id < node_count; ++node_id) {
                stabilizer::StabilizerTableau new_state = node_states[node_id];

                for (size_t neighbour = 0; neighbour < node_count; ++neighbour) {
                    const int8_t edge = adjacency[node_id * node_count + neighbour];

                    if (edge != 0) {
                        const int8_t attention = symplectic_for_pair(node_states[node_id], node_states[neighbour]);

                        if (attention > 0) {
                            new_state.apply_csum(node_id, neighbour);
                        }
                    }
                }

                output_states[node_id] = new_state;
            }
        });
    }).wait();
}

} // namespace q_mini_wasm_v2::core::qgnn