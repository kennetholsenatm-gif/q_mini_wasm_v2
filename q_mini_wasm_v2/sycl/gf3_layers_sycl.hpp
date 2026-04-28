#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace q_mini_wasm_v2::sycl_kernels {

/** QMINI_GF3_SYCL=0 disables; else use when shape is large enough (see .cpp). */
bool gf3_sycl_forward_enabled_for_shape(size_t input_dim, size_t output_dim) noexcept;

/** Tropical linear layer on default SYCL queue when USE_SYCL; else returns false. */
bool gf3_tropical_linear_forward_sycl(
    const std::vector<int8_t>& input_trits,
    const std::vector<int8_t>& weights_row_major,
    const std::vector<int8_t>& bias_trits,
    bool use_bias,
    size_t input_dim,
    size_t output_dim,
    std::vector<int8_t>& out_trits
);

/** Standard GF(3) multiply-accumulate forward + ternary activation when USE_SYCL. */
bool gf3_standard_linear_forward_sycl(
    const std::vector<int8_t>& input_trits,
    const std::vector<int8_t>& weights_row_major,
    const std::vector<int8_t>& bias_trits,
    bool use_bias,
    size_t input_dim,
    size_t output_dim,
    std::vector<int8_t>& out_trits
);

/** Hebbian update in parallel over (i,j) when USE_SYCL; overwrites weights_row_major. */
bool gf3_hebbian_update_sycl(
    const std::vector<int8_t>& input_trits,
    int32_t goodness_delta,
    int8_t learning_rate,
    size_t input_dim,
    size_t output_dim,
    std::vector<int8_t>& weights_row_major
);

} // namespace q_mini_wasm_v2::sycl_kernels
