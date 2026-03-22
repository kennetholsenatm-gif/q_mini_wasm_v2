#include <cstdint>

/**
 * Host entry for optional SYCL joint_matrix ternary GEMM (Intel oneAPI).
 * Returns 0 when linked; default stub returns -1 (not available).
 */
extern "C" int qminiwasm_sycl_ternary_gemm(const std::uint8_t* /*packed_weights*/,
                                           const std::int8_t* /*activations*/, std::int32_t* /*out*/,
                                           int /*m*/, int /*n*/, int /*k*/) {
  return -1;
}
