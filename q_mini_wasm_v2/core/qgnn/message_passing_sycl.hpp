#pragma once

#include <sycl/sycl.hpp>
#include "message_passing.hpp"
#include "../memory/arena.hpp"

/**
 * @brief SYCL Accelerated QGNN Message Passing Kernel
 * 
 * Phase 3 §132-144: Quantum Graph Neural Network SYCL Hardware Mapping
 * 
 * Implements the QGNN message passing protocol optimized for heterogeneous
 * parallel execution on GPU, FPGA, and Flash Compute-in-Memory hardware.
 * 
 * All operations are mapped to SYCL work-groups with explicit local memory
 * caching for maximum memory bandwidth utilization.
 */

namespace q_mini_wasm_v2::core::qgnn {

class MessagePassingSycl {
public:
    /**
     * @brief Construct SYCL accelerated message passing kernel
     */
    explicit MessagePassingSycl(sycl::queue& queue, memory::MemoryArena& arena) noexcept;

    /**
     * @brief Execute parallel message passing iteration
     * @param graph_adjacency GF(3) entanglement graph adjacency matrix
     * @param node_states Stabilizer tableau states for all nodes
     * @return Updated node states after message aggregation
     */
    std::span<const stabilizer::StabilizerTableau> forward_pass(
        std::span<const int8_t> graph_adjacency,
        std::span<const stabilizer::StabilizerTableau> node_states
    ) noexcept;

    /**
     * @brief Batch symplectic attention calculation kernel
     * @return Symplectic alignment scores for all node pairs
     */
    void batch_symplectic_attention(
        std::span<const stabilizer::StabilizerTableau> node_states,
        std::span<int8_t> attention_matrix
    ) noexcept;

    /**
     * @brief Get SYCL queue reference
     */
    sycl::queue& get_queue() noexcept;

    /**
     * @brief Device memory allocation in SYCL shared address space
     */
    template <typename T>
    T* allocate_device(size_t count) noexcept {
        return sycl::malloc_device<T>(count, queue_);
    }

private:
    sycl::queue& queue_;
    memory::MemoryArena& arena_;

    static constexpr size_t WORK_GROUP_SIZE = 256;
    static constexpr size_t LOCAL_MEM_NODES = 32;

    void launch_message_passing_kernel(
        const int8_t* adjacency,
        const stabilizer::StabilizerTableau* node_states,
        stabilizer::StabilizerTableau* output_states,
        size_t node_count
    ) noexcept;
};

} // namespace q_mini_wasm_v2::core::qgnn