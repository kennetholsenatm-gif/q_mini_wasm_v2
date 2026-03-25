#include "json_subset_pda.hpp"

#include "../simd/cpuid.hpp"

namespace qminiwasm::grammar {

void apply_grammar_mask_dispatch(std::uint8_t* logits_gate, const std::uint8_t* allowed, std::size_t n) {
  const simd::CpuFeatures f = simd::detect_cpu_features();
#if QMINIWASM_HAS_AVX512_TU
  if (f.avx512f) {
    or_mask_all_allowed_avx512(logits_gate, allowed, n);
    return;
  }
#endif
  for (std::size_t i = 0; i < n; ++i) {
    if (!allowed[i]) {
      logits_gate[i] = 0;
    }
  }
}

}  // namespace qminiwasm::grammar
