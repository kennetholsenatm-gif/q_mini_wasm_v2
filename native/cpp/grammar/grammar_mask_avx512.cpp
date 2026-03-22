#include "json_subset_pda.hpp"

#if defined(__AVX512F__)
#include <immintrin.h>
#endif

namespace qminiwasm::grammar {

#if defined(__AVX512F__)

void or_mask_all_allowed_avx512(std::uint8_t* logits_gate, const std::uint8_t* allowed, std::size_t n) {
  std::size_t i = 0;
  for (; i + 64 <= n; i += 64) {
    __m512i g = _mm512_loadu_si512(reinterpret_cast<const __m512i*>(logits_gate + i));
    __m512i a = _mm512_loadu_si512(reinterpret_cast<const __m512i*>(allowed + i));
    __m512i one = _mm512_set1_epi8(1);
    __m512i inv_a = _mm512_xor_si512(a, one);
    __m512i out = _mm512_and_si512(g, inv_a);
    _mm512_storeu_si512(reinterpret_cast<__m512i*>(logits_gate + i), out);
  }
  for (; i < n; ++i) {
    if (!allowed[i]) {
      logits_gate[i] = 0;
    }
  }
}

#else
#error "grammar_mask_avx512.cpp must be compiled with AVX-512 flags"
#endif

}  // namespace qminiwasm::grammar
