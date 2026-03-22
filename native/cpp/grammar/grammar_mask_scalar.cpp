#include "json_subset_pda.hpp"

#include <algorithm>
#include <cstring>

namespace qminiwasm::grammar {

void build_allowed_mask_scalar(const JsonSubsetPda& pda, std::size_t vocab_size, std::uint8_t* out_mask) {
  std::fill(out_mask, out_mask + vocab_size, 0);
  const int s = pda.state_id();
  if (s == 0) {
    out_mask[static_cast<std::size_t>(JsonTokenId::LBrace)] = 1;
    out_mask[static_cast<std::size_t>(JsonTokenId::WS)] = 1;
  } else if (s == 1) {
    out_mask[static_cast<std::size_t>(JsonTokenId::Quote)] = 1;
    out_mask[static_cast<std::size_t>(JsonTokenId::WS)] = 1;
  } else if (s == 2) {
    out_mask[static_cast<std::size_t>(JsonTokenId::Colon)] = 1;
  } else if (s == 3) {
    for (int d = 0; d < 10; ++d) {
      out_mask[static_cast<std::size_t>(JsonTokenId::Digit0) + static_cast<std::size_t>(d)] = 1;
    }
  } else if (s == 4) {
    out_mask[static_cast<std::size_t>(JsonTokenId::RBrace)] = 1;
    out_mask[static_cast<std::size_t>(JsonTokenId::WS)] = 1;
  } else {
    out_mask[static_cast<std::size_t>(JsonTokenId::End)] = 1;
  }
}

#if !QMINIWASM_HAS_AVX512_TU

void or_mask_all_allowed_avx512(std::uint8_t* logits_gate, const std::uint8_t* allowed, std::size_t n) {
  for (std::size_t i = 0; i < n; ++i) {
    if (!allowed[i]) {
      logits_gate[i] = 0;
    }
  }
}

#endif

}  // namespace qminiwasm::grammar
