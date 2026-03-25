// AVX2 matvec: unpack bytes to int8 trits (scalar unpack), then SIMD int32 dots.
// A packed _mm256_maddubs_epi16 trit-unpack path can layer on the same layout as w158_pack.
#include "ternary_forward.hpp"

#include "../pack/w158_pack.hpp"

#include <cstdint>
#include <vector>

#if defined(__AVX2__)
#include <immintrin.h>
#endif

namespace qminiwasm::kernels {

#if defined(__AVX2__)

static std::int32_t hsum_i32_256(__m256i v) {
  __m128i lo = _mm256_castsi256_si128(v);
  __m128i hi = _mm256_extracti128_si256(v, 1);
  __m128i s = _mm_add_epi32(lo, hi);
  s = _mm_hadd_epi32(s, s);
  s = _mm_hadd_epi32(s, s);
  return _mm_cvtsi128_si32(s);
}

/** Dot product after byte unpack: int8 {-1,0,1} × int8 activations via 32-bit lanes (no per-trit mod). */
static std::int32_t dot_i8_avx2_x8(const std::int8_t* w, const std::int8_t* a, int n) {
  __m256i acc = _mm256_setzero_si256();
  int i = 0;
  for (; i + 8 <= n; i += 8) {
    __m128i w8 = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(w + i));
    __m128i a8 = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(a + i));
    __m256i w32 = _mm256_cvtepi8_epi32(w8);
    __m256i a32 = _mm256_cvtepi8_epi32(a8);
    acc = _mm256_add_epi32(acc, _mm256_mullo_epi32(w32, a32));
  }
  std::int32_t s = hsum_i32_256(acc);
  for (; i < n; ++i) {
    s += static_cast<std::int32_t>(w[i]) * static_cast<std::int32_t>(a[i]);
  }
  return s;
}

#endif

void matvec_avx2(const std::uint8_t* packed_weights, std::size_t num_rows, int in_features,
                 const std::int8_t* activations, std::int32_t* out) {
  const int row_bytes = static_cast<int>((static_cast<std::size_t>(in_features) + pack::kTritsPerByte - 1) /
                                         pack::kTritsPerByte);
#if defined(__AVX2__)
  thread_local std::vector<std::int8_t> buf;
  buf.resize(static_cast<std::size_t>(in_features));
  for (std::size_t r = 0; r < num_rows; ++r) {
    unpack_packed_row_i8(packed_weights + r * row_bytes, in_features, buf.data());
    out[r] = dot_i8_avx2_x8(buf.data(), activations, in_features);
  }
#else
  matvec_scalar(packed_weights, num_rows, in_features, activations, out);
#endif
}

}  // namespace qminiwasm::kernels
