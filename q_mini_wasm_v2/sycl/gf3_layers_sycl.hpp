#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#if defined(USE_SYCL) && USE_SYCL
#include <sycl/sycl.hpp>
#endif

namespace q_mini_wasm_v2::sycl_kernels {

/** Host (TOML) runtime for GF3 SYCL: call from pipeline init before any GF3 SYCL queue use. */
void gf3_sycl_apply_runtime_host_config(
    int32_t gpu_device_index, uint64_t min_weight_cells, bool submit_grid_log_to_stderr) noexcept;

/**
 * One-shot SYCL device probe (safe before Training_InitSession): resolves GPU like training.sycl_gpu_device_index,
 * reads global_mem_size. Returns 0 on success; -1 bad args; -3 resolution/query failure (see err_utf8_out).
 */
int gf3_sycl_probe_device_resources(
    int32_t gpu_device_index,
    uint64_t* global_mem_bytes_out,
    uint32_t* is_gpu_u32_out,
    char* name_utf8_out,
    size_t name_cap,
    char* err_utf8_out,
    size_t err_cap) noexcept;

#if defined(USE_SYCL) && USE_SYCL
/** Thread-local queue for the configured GF(3) SYCL device (same as layer kernels). */
sycl::queue& gf3_layers_queue_for_thread();
#endif

/** True when both dimensions are positive (SYCL builds run GF3 linear on GPU for every valid shape). */
bool gf3_sycl_forward_enabled_for_shape(size_t input_dim, size_t output_dim) noexcept;

/** I/O is TritPack5-encoded GF(3) trits; kernel implements max-plus linear map (same algebra as CPU forward). */
bool gf3_tropical_linear_forward_sycl(
    const std::vector<uint8_t>& input_packed,
    const std::vector<uint8_t>& weights_packed,
    const std::vector<uint8_t>& bias_packed,
    bool use_bias,
    size_t input_dim,
    size_t output_dim,
    std::vector<uint8_t>& out_packed);

bool gf3_hebbian_update_sycl(
    const std::vector<uint8_t>& input_packed,
    int32_t goodness_delta,
    int8_t learning_rate,
    size_t input_dim,
    size_t output_dim,
    std::vector<uint8_t>& weights_packed);

/**
 * Generate contrastive negatives (TritPack5 I/O; same polynomial byte layout as @c q::ternary::pack_batch_t5).
 * @p positive_packed must hold at least ceil(trit_count/5) bytes; @p negative_packed is resized to match.
 */
bool gf3_generate_negative_sycl(
    const std::vector<uint8_t>& positive_packed,
    size_t trit_count,
    uint32_t corruption_seed,
    std::vector<uint8_t>& negative_packed);

/**
 * Batched forward: activations, weights, biases, outputs are flat TritPack5 streams
 * (B*dim trits packed with @c q::ternary::pack_batch_t5 on the concatenated trit vector).
 */
bool gf3_tropical_linear_forward_batched_pack5_io_sycl(
    const std::vector<uint8_t>& batch_in_packed,
    const std::vector<uint8_t>& batch_w_packed,
    const std::vector<uint8_t>& batch_bias_packed,
    bool use_bias,
    size_t batch_size,
    size_t input_dim,
    size_t output_dim,
    std::vector<uint8_t>& batch_out_packed,
    bool wait_for_completion = true);

/** Batched Hebbian: packed layer inputs, in-place packed stacked weight matrices (same flat layout as forward). */
bool gf3_hebbian_update_batched_pack5_io_sycl(
    const std::vector<uint8_t>& batch_in_packed,
    const std::vector<int32_t>& delta,
    int8_t learning_rate,
    size_t batch_size,
    size_t input_dim,
    size_t output_dim,
    std::vector<uint8_t>& batch_w_packed,
    bool wait_for_completion = true);

/** Two SYCL submits (pos, neg) + one wait — better GPU feed than separate forwards (each waited per call). */
bool gf3_linear_forward_batched_pos_neg_fused_pack5_io_sycl(
    const std::vector<uint8_t>& batch_pos_in_packed,
    const std::vector<uint8_t>& batch_neg_in_packed,
    const std::vector<uint8_t>& batch_w_packed,
    const std::vector<uint8_t>& batch_bias_packed,
    bool use_bias,
    size_t batch_size,
    size_t input_dim,
    size_t output_dim,
    std::vector<uint8_t>& batch_pos_out_packed,
    std::vector<uint8_t>& batch_neg_out_packed);

} // namespace q_mini_wasm_v2::sycl_kernels
