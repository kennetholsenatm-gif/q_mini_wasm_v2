#include "../grammar/json_subset_pda.hpp"

#include <cstdint>
#include <vector>

bool test_grammar_mask() {
  qminiwasm::grammar::JsonSubsetPda pda;
  constexpr std::size_t vocab = 256;
  std::vector<std::uint8_t> allowed(vocab);
  std::vector<std::uint8_t> gate(vocab, 1);
  qminiwasm::grammar::build_allowed_mask_scalar(pda, vocab, allowed.data());
  std::vector<std::uint8_t> gate_dispatch = gate;
  qminiwasm::grammar::apply_grammar_mask_dispatch(gate_dispatch.data(), allowed.data(), vocab);
  for (std::size_t i = 0; i < vocab; ++i) {
    std::uint8_t expect = allowed[i] ? 1u : 0u;
    if (gate_dispatch[i] != expect) {
      return false;
    }
  }
  return true;
}
