#pragma once

#include <vector>
#include <cstdint>

#ifdef USE_SYCL
#include <CL/sycl.hpp>
#endif

namespace q_mini_wasm_v2::sycl_kernels {

/**
 * @brief SYCL-accelerated Stabilizer Tableau Operations
 * 
 * Parallelizes qutrit Clifford gate operations across multi-core/multi-thread
 * architectures using SYCL for hardware-agnostic GPU/CPU acceleration.
 * 
 * Key optimizations:
 * 1. Parallel tableau row updates
 * 2. Vectorized modulo-3 arithmetic
 * 3. Coalesced memory access patterns
 * 4. Async execution for overlapping computation
 */

/**
 * @brief Parallel Hadamard gate application
 * @param tableau_data Raw tableau matrix data (2n x 2n over GF(3))
 * @param phase_data Phase vector data (2n entries)
 * @param n Number of qutrits
 * @param target_qutrit Target qutrit index
 */
void parallel_apply_hadamard(
    std::vector<int8_t>& tableau_data,
    std::vector<int8_t>& phase_data,
    size_t n,
    size_t target_qutrit
);

/**
 * @brief Parallel Phase gate application
 */
void parallel_apply_phase(
    std::vector<int8_t>& tableau_data,
    std::vector<int8_t>& phase_data,
    size_t n,
    size_t target_qutrit
);

/**
 * @brief Parallel Controlled-SUM gate application
 */
void parallel_apply_csum(
    std::vector<int8_t>& tableau_data,
    std::vector<int8_t>& phase_data,
    size_t n,
    size_t control,
    size_t target
);

/**
 * @brief Parallel measurement operations
 * @return Measurement outcomes for all qutrits
 */
std::vector<int8_t> parallel_measure_all(
    std::vector<int8_t>& tableau_data,
    std::vector<int8_t>& phase_data,
    size_t n
);

/**
 * @brief Parallel MoE routing computation
 * @param routing_weights Routing weight matrix
 * @param input_features Input feature vector
 * @param num_experts Number of experts
 * @return Routing logits for each expert
 */
std::vector<double> parallel_compute_routing_logits(
    const std::vector<int8_t>& routing_weights,
    const std::vector<int8_t>& input_features,
    size_t num_experts
);

/**
 * @brief Parallel Forward-Forward layer computation
 * @param weights Layer weight matrix
 * @param biases Layer bias vector
 * @param input Input activations
 * @param output Output activations
 */
void parallel_forward_layer(
    const std::vector<int8_t>& weights,
    const std::vector<int8_t>& biases,
    const std::vector<int8_t>& input,
    std::vector<int8_t>& output
);

/**
 * @brief Parallel modulo-3 arithmetic batch operations
 * @param a First operand array
 * @param b Second operand array
 * @param result Result array
 * @param operation 0=add, 1=subtract, 2=multiply
 */
void parallel_mod3_arithmetic(
    const std::vector<int8_t>& a,
    const std::vector<int8_t>& b,
    std::vector<int8_t>& result,
    int operation
);

#ifdef USE_SYCL

/**
 * @brief Element-wise GF(3) multiply on {0,1,2} representatives (USM/host-visible buffers).
 *
 * Intended for WASM / bridge paths that already use SYCL USM allocations; falls back to CPU on failure.
 */
void gf3_uint8_mul_batch_sycl(cl::sycl::queue& q, uint8_t* result, const uint8_t* a, const uint8_t* b, size_t count);

/** Element-wise GF(3) add mod 3 on uint8 lanes (values treated mod 3). */
void gf3_uint8_add_batch_sycl(cl::sycl::queue& q, uint8_t* result, const uint8_t* a, const uint8_t* b, size_t count);

/**
 * @brief WASM-bridge tableau layout: row-major slab, `stride = 2 * num_qutrits`, X then Z blocks (see wasm_api cpu_fallback).
 *
 * Pointers must be SYCL-usable (e.g. USM from `sycl::malloc_shared` on the same queue's context).
 */
void wasm_tableau_hadamard_sycl(cl::sycl::queue& q, uint8_t* tableau, size_t num_qutrits, size_t target);
void wasm_tableau_phase_sycl(cl::sycl::queue& q, uint8_t* tableau, size_t num_qutrits, size_t target);
void wasm_tableau_csum_sycl(cl::sycl::queue& q, uint8_t* tableau, size_t num_qutrits, size_t control, size_t target);

/**
 * @brief Parallel MoE symplectic routing scores (one int8 score per expert).
 *
 * @param input_tritpack5 TritPack5 input: ceil(input_trit_count/5) bytes, base-3 polynomial (5 trits / byte, 0..242).
 * @param input_trit_count Logical trit length of the packed stream (same convention as unpacked routing).
 * @param weights_tritpack5 Expert rows concatenated: each row is ceil(routing_qutrits/5) TritPack5 bytes (E rows, same encoding).
 */
std::vector<int8_t> moe_routing_symplectic_scores_sycl(
    const std::vector<uint8_t>& input_tritpack5,
    size_t input_trit_count,
    const std::vector<uint8_t>& weights_tritpack5,
    size_t total_experts,
    size_t routing_qutrits);

/**
 * @brief Float vector -> int8 trits {-1,0,+1} with thresholds +/-0.33f (matches CPU training path).
 * @param src_len length of valid @p src; indices >= src_len in the output are padded with 0.
 */
std::vector<int8_t> quantize_float_buffer_to_trits_sycl(const float* src, size_t src_len, size_t out_dim);

/** int32 -> trits via v%3 corrected to nonnegative, then mapped to {-1,0,1} (matches CPU path). */
std::vector<int8_t> quantize_i32_buffer_to_trits_sycl(const int32_t* src, size_t src_len, size_t out_dim);

/** Byte string -> trits using (byte % 3) - 1 at output index i from src[i % slen]. */
std::vector<int8_t> quantize_string_bytes_to_trits_sycl(const char* src, size_t slen, size_t out_dim);

/**
 * @brief SYCL queue for async execution
 */
class SYCLQueue {
public:
    SYCLQueue();
    ~SYCLQueue();
    
    /**
     * @brief Submit parallel tableau update kernel
     */
    void submit_tableau_update(
        cl::sycl::buffer<int8_t, 2>& tableau_buf,
        cl::sycl::buffer<int8_t, 1>& phase_buf,
        size_t target_qutrit
    );
    
    /**
     * @brief Wait for all submitted kernels to complete
     */
    void wait();
    
private:
    cl::sycl::queue queue_;
};

#endif // USE_SYCL

} // namespace q_mini_wasm_v2::sycl_kernels