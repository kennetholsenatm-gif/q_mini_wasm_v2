#include "ternary_forward.hpp"

#include "../pack/w158_pack.hpp"

#include <cstdint>
#include <vector>

#if defined(__AVX512F__) && defined(__AVX512VL__) && defined(__AVX512BW__)
#include <immintrin.h>
#endif

namespace qminiwasm::kernels {

#if defined(__AVX512F__) && defined(__AVX512VL__) && defined(__AVX512BW__)

static std::int32_t hsum_i32_512(__m512i v) {
  __m256i lo = _mm512_castsi512_si256(v);
  __m256i hi = _mm512_extracti64x4_epi64(v, 1);
  __m256i s = _mm256_add_epi32(lo, hi);
  s = _mm256_hadd_epi32(s, s);
  s = _mm256_hadd_epi32(s, s);
  __m128i t = _mm_add_epi32(_mm256_castsi256_si128(s), _mm256_extracti128_si256(s, 1));
  return _mm_cvtsi128_si32(t);
}

static std::int32_t dot_i8_avx512_x32(const std::int8_t* w, const std::int8_t* a, int n) {
  __m512i acc = _mm512_setzero_si512();
  int i = 0;
  for (; i + 16 <= n; i += 16) {
    __m128i w8 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(w + i));
    __m128i a8 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(a + i));
    __m512i w32 = _mm512_cvtepi8_epi32(w8);
    __m512i a32 = _mm512_cvtepi8_epi32(a8);
    acc = _mm512_add_epi32(acc, _mm512_mullo_epi32(w32, a32));
  }
  std::int32_t s = hsum_i32_512(acc);
  for (; i < n; ++i) {
    s += static_cast<std::int32_t>(w[i]) * static_cast<std::int32_t>(a[i]);
  }
  return s;
}

#endif

void matvec_avx512(const std::uint8_t* packed_weights, std::size_t num_rows, int in_features,
                   const std::int8_t* activations, std::int32_t* out) {
  const int row_bytes = static_cast<int>((static_cast<std::size_t>(in_features) + pack::kTritsPerByte - 1) /
                                         pack::kTritsPerByte);
#if defined(__AVX512F__) && defined(__AVX512VL__) && defined(__AVX512BW__)
  thread_local std::vector<std::int8_t> buf;
  buf.resize(static_cast<std::size_t>(in_features));
  for (std::size_t r = 0; r < num_rows; ++r) {
    unpack_packed_row_i8(packed_weights + r * row_bytes, in_features, buf.data());
    out[r] = dot_i8_avx512_x32(buf.data(), activations, in_features);
  }
#else
#error "ternary_forward_avx512.cpp must be compiled with AVX-512 F/VL/BW flags"
#endif
}

}  // namespace qminiwasm::kernels
