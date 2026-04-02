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