#include "ternary_forward.hpp"

#include <immintrin.h>

namespace qminiwasm::kernels {

void matvec_avx2(const std::uint8_t* packed_weights, std::size_t num_rows, int in_features,
                 const std::int8_t* activations, std::int32_t* out) {
  (void)packed_weights;
  (void)num_rows;
  (void)in_features;
  (void)activations;
  (void)out;
  // Placeholder: AVX2 tile path can widen int8 products; delegate to scalar for correctness v1.
  matvec_scalar(packed_weights, num_rows, in_features, activations, out);
}

}  // namespace qminiwasm::kernels
