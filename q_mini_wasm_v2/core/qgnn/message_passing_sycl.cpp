#include "message_passing_sycl.hpp"

namespace q_mini_wasm_v2::core::qgnn {

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
    
    auto* output_states = arena_.allocate_array<stabilizer::StabilizerTableau>(node_count);

    launch_message_passing_kernel(
        graph_adjacency.data(),
        node_states.data(),
        output_states.data(),
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
    
    queue_.submit([&](sycl::handler& cgh) {
        cgh.parallel_for(sycl::range<2>(node_count, node_count), [=](sycl::item<2> item) {
            const size_t i = item.get_id(0);
            const size_t j = item.get_id(1);
            
            attention_matrix[i * node_count + j] = MessagePassingKernel::symplectic_attention(
                node_states[i].get_trits(),
                node_states[j].get_trits()
            );
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
    queue_.submit([&](sycl::handler& cgh) {
        // Allocate local memory for neighbour node caching
        sycl::local_accessor<stabilizer::StabilizerTableau, 1> local_nodes(
            sycl::range<1>(LOCAL_MEM_NODES), cgh
        );

        cgh.parallel_for(sycl::range<1>(node_count), [=](sycl::item<1> item) {
            const size_t node_id = item.get_id(0);
            
            stabilizer::StabilizerTableau new_state = node_states[node_id];
            
            // Cache neighbouring nodes in local memory
            for (size_t n = 0; n < LOCAL_MEM_NODES && node_id + n < node_count; ++n) {
                local_nodes[n] = node_states[node_id + n];
            }
            
            // Aggregate messages from all neighbours
            for (size_t neighbour = 0; neighbour < node_count; ++neighbour) {
                const int8_t edge = adjacency[node_id * node_count + neighbour];
                
                if (edge != 0) {
                    const int8_t attention = MessagePassingKernel::symplectic_attention(
                        node_states[node_id].get_trits(),
                        node_states[neighbour].get_trits()
                    );
                    
                    if (attention > 0) {
                        // Apply GF(3) message aggregation
                        new_state.apply_csum(node_id, neighbour);
                    }
                }
            }
            
            output_states[node_id] = new_state;
        });
    }).wait();
}

} // namespace q_mini_wasm_v2::core::qgnn