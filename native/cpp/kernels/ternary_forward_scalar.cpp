#include "ternary_forward.hpp"

#include "../pack/w158_pack.hpp"

#include <algorithm>
#include <cstdint>

namespace qminiwasm::kernels {

static int8_t digit_to_i8(int digit) {
  if (digit == 0) return -1;
  if (digit == 1) return 0;
  return 1;
}

void unpack_packed_row_i8(const std::uint8_t* packed_row, int in_features, std::int8_t* out_w) {
  int ti = 0;
  std::size_t bi = 0;
  while (ti < in_features) {
    const int k = std::min(pack::kTritsPerByte, in_features - ti);
    int rem = static_cast<int>(packed_row[bi++]) & 0xFF;
    for (int j = 0; j < k; ++j) {
      int power = 1;
      for (int t = 0; t < k - 1 - j; ++t) power *= 3;
      const int digit = rem / power;
      rem %= power;
      out_w[ti++] = digit_to_i8(digit);
    }
  }
}

std::int32_t packed_row_dot_scalar(const std::uint8_t* packed_row, int in_features,
                                    const std::int8_t* activations) {
  std::int32_t acc = 0;
  int ti = 0;
  std::size_t bi = 0;
  while (ti < in_features) {
    const int k = std::min(pack::kTritsPerByte, in_features - ti);
    int rem = static_cast<int>(packed_row[bi++]) & 0xFF;
    for (int j = 0; j < k; ++j) {
      int power = 1;
      for (int t = 0; t < k - 1 - j; ++t) power *= 3;
      const int digit = rem / power;
      rem %= power;
      const std::int8_t w = digit_to_i8(digit);
      acc += static_cast<std::int32_t>(w) * static_cast<std::int32_t>(activations[ti++]);
    }
  }
  return acc;
}

void matvec_scalar(const std::uint8_t* packed_weights, std::size_t num_rows, int in_features,
                   const std::int8_t* activations, std::int32_t* out) {
  const int row_bytes = static_cast<int>((static_cast<std::size_t>(in_features) + pack::kTritsPerByte - 1) /
                                           pack::kTritsPerByte);
  for (std::size_t r = 0; r < num_rows; ++r) {
    out[r] = packed_row_dot_scalar(packed_weights + r * row_bytes, in_features, activations);
  }
}

#if !QMINIWASM_HAS_AVX512_TU

void matvec_avx512(const std::uint8_t* packed_weights, std::size_t num_rows, int in_features,
                   const std::int8_t* activations, std::int32_t* out) {
  matvec_scalar(packed_weights, num_rows, in_features, activations, out);
}

#endif

}  // namespace qminiwasm::kernels
