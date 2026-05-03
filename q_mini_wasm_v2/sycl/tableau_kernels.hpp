#pragma once

#include <vector>
#include <cstdint>
#include <optional>

#ifdef USE_SYCL
#include <sycl/sycl.hpp>
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
void gf3_uint8_mul_batch_sycl(sycl::queue& q, uint8_t* result, const uint8_t* a, const uint8_t* b, size_t count);

/** Element-wise GF(3) add mod 3 on uint8 lanes (values treated mod 3). */
void gf3_uint8_add_batch_sycl(sycl::queue& q, uint8_t* result, const uint8_t* a, const uint8_t* b, size_t count);

/**
 * @brief WASM-bridge tableau layout: row-major slab, `stride = 2 * num_qutrits`, X then Z blocks (see wasm_api host reference path).
 *
 * Pointers must be SYCL-usable (e.g. USM from `sycl::malloc_shared` on the same queue's context).
 */
void wasm_tableau_hadamard_sycl(sycl::queue& q, uint8_t* tableau, size_t num_qutrits, size_t target);
void wasm_tableau_phase_sycl(sycl::queue& q, uint8_t* tableau, size_t num_qutrits, size_t target);
void wasm_tableau_csum_sycl(sycl::queue& q, uint8_t* tableau, size_t num_qutrits, size_t control, size_t target);

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
 * @brief Device-resident symplectic logits (one int8 per expert, USM). Move-only.
 *
 * Use for chaining follow-on SYCL work without a full host readback. Call @ref copy_to_host when CPU access is needed.
 */
class SymplecticRoutingScoresDevice {
public:
    SymplecticRoutingScoresDevice() = default;
    ~SymplecticRoutingScoresDevice();
    SymplecticRoutingScoresDevice(SymplecticRoutingScoresDevice&&) noexcept;
    SymplecticRoutingScoresDevice& operator=(SymplecticRoutingScoresDevice&&) noexcept;
    SymplecticRoutingScoresDevice(const SymplecticRoutingScoresDevice&) = delete;

    bool empty() const noexcept { return ptr_ == nullptr || n_experts_ == 0; }
    size_t expert_count() const noexcept { return n_experts_; }
    sycl::queue& queue() noexcept { return q_; }
    const sycl::queue& queue() const noexcept { return q_; }
    /** SYCL device USM pointer (valid until this object is destroyed). */
    int8_t* device_ptr() noexcept { return ptr_; }
    const int8_t* device_ptr() const noexcept { return ptr_; }

    std::vector<int8_t> copy_to_host() const;

    /** Construct device-resident scores; @c std::nullopt on invalid inputs or allocation failure. */
    static std::optional<SymplecticRoutingScoresDevice> try_create_usm(
        const std::vector<uint8_t>& input_tritpack5,
        size_t input_trit_count,
        const std::vector<uint8_t>& weights_tritpack5,
        size_t total_experts,
        size_t routing_qutrits);

private:
    void free_ptr();
    /** Mutable so @ref copy_to_host() const can submit USM D2H memcpy on oneAPI. */
    mutable sycl::queue q_{};
    int8_t* ptr_ = nullptr;
    size_t n_experts_ = 0;
};

/**
 * @brief Same symplectic routing scores as @ref moe_routing_symplectic_scores_sycl, then Top-K on device.
 *
 * Symplectic logits are in GF(3) {-1,0,1}; selection walks expert index in order and picks tiers 1, 0, -1
 * (deterministic, index-stable tie break). Returns @p k expert indices; only @c k uint32 values are read back
 * (not the full E-score vector), so the host no longer does partial_sort over E on every route.
 */
std::vector<std::uint32_t> moe_routing_symplectic_topk_indices_sycl(
    const std::vector<std::uint8_t>& input_tritpack5,
    size_t input_trit_count,
    const std::vector<std::uint8_t>& weights_tritpack5,
    size_t total_experts,
    size_t routing_qutrits,
    size_t k);

/**
 * @brief Batched symplectic route: @p num_rows packed inputs concatenated (each @c ceil(input_trit_count/5) bytes).
 * @return Row-major @c num_rows * k expert indices (uint32).
 */
std::vector<std::uint32_t> moe_routing_symplectic_topk_indices_sycl_batched(
    const std::vector<std::uint8_t>& inputs_tritpack5_rows_concat,
    size_t input_trit_count,
    const std::vector<std::uint8_t>& weights_tritpack5,
    size_t total_experts,
    size_t routing_qutrits,
    size_t k,
    size_t num_rows);

/**
 * @brief Quantum-entangled score mixing on SYCL from device-resident symplectic logits.
 *
 * Computes:
 *   out[i] = base_logits[i] + sum_{j!=i} ((base_logits[j] * coupling[i,j]) / 100) / 4
 * where @p coupling_flat_rowmajor is an E x E row-major matrix.
 *
 * @param q SYCL queue that owns @p logits_device.
 * @param logits_device USM device pointer with E int8 logits in {-1,0,1}.
 * @param total_experts E
 * @param coupling_flat_rowmajor row-major E*E int32 coupling matrix.
 * @return E int32 mixed scores on host (empty on invalid input).
 */
std::vector<int32_t> moe_quantum_entangled_scores_from_logits_device_sycl(
    sycl::queue& q,
    const int8_t* logits_device,
    size_t total_experts,
    const std::vector<int32_t>& coupling_flat_rowmajor);

/**
 * @brief Multi-objective weighted routing scores from device-resident symplectic logits.
 *
 * Matches @c MoERouter::multi_objective_selection CPU math: four objectives (perf from logits,
 * load/energy/latency from @p ternary_seed), weighted sum with fixed-point weights 250/500/750,
 * then narrows to int8 with @f$(acc \cdot 127) / 1000 - 128@f$.
 */
std::vector<int8_t> moe_multi_objective_final_scores_from_logits_device_sycl(
    sycl::queue& q,
    const int8_t* logits_device,
    size_t total_experts,
    std::uint32_t ternary_seed,
    std::uint32_t num_objectives,
    std::int32_t weight_dim0,
    std::int32_t weight_dim1,
    std::int32_t weight_dim2,
    std::int32_t weight_dim3);

/**
 * @brief Float vector -> TritPack5 (thresholds +/-0.33f; deterministic {-1,0,1} lanes).
 * @param src_len length of valid @p src; trit indices >= src_len are padded with 0.
 */
std::vector<uint8_t> quantize_float_buffer_to_trits_sycl(const float* src, size_t src_len, size_t out_dim);

/** int32 -> TritPack5 via v%3 corrected to nonnegative, then mapped to {-1,0,1} (same as directory-line mapping). */
std::vector<uint8_t> quantize_i32_buffer_to_trits_sycl(const int32_t* src, size_t src_len, size_t out_dim);

/** Byte string -> TritPack5 using (byte % 3) - 1 at output trit index i from src[i % slen]. */
std::vector<uint8_t> quantize_string_bytes_to_trits_sycl(const char* src, size_t slen, size_t out_dim);

/**
 * @brief Pack balanced int8 trits {-1,0,1} to TritPack5 (same polynomial layout as @c q::ternary::pack_batch_t5).
 */
std::vector<uint8_t> pack_int8_lanes_to_tritpack5_sycl(const std::vector<int8_t>& lanes, size_t trit_count);

/**
 * @brief Reserve device memory early (best-effort) on the **same SYCL device/queue as GF3 training**
 * (`gf3_layers_queue_for_thread`), not `default_selector`.
 *
 * Allocation is chunked and retained until next call or process exit.
 * @return Number of bytes successfully reserved on device.
 */
size_t reserve_sycl_device_memory_bytes(
    size_t target_bytes,
    size_t chunk_bytes,
    bool touch_pages,
    std::string* note = nullptr);

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
        sycl::buffer<int8_t, 2>& tableau_buf,
        sycl::buffer<int8_t, 1>& phase_buf,
        size_t target_qutrit
    );
    
    /**
     * @brief Wait for all submitted kernels to complete
     */
    void wait();
    
private:
    sycl::queue queue_;
};

#endif // USE_SYCL

} // namespace q_mini_wasm_v2::sycl_kernels