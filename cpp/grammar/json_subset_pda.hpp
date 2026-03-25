#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace qminiwasm::grammar {

/** Minimal JSON-subset tokenizer ids for toy vocab (256 tokens). */
enum class JsonTokenId : std::uint8_t {
  WS = 0,
  LBrace = 1,
  RBrace = 2,
  Colon = 3,
  Comma = 4,
  Quote = 5,
  Digit0 = 16,
  // ... reserved layout: digits 16-25, letters for true/false/null stubs
  End = 255,
};

/**
 * Hand-built PDA state for sequence: '{' ws* 'key' ws* ':' ws* number ws* '}'
 * Represented as stack of small integers; transition on token id.
 */
class JsonSubsetPda {
 public:
  JsonSubsetPda();

  /** Current PDA state id (for mask table lookup). */
  int state_id() const { return static_cast<int>(stack_.empty() ? 0 : stack_.back()); }

  /** Try transition; returns false if illegal token for state. */
  bool push_token(std::uint8_t token_id);

  void reset();

 private:
  std::vector<int> stack_;
};

/** Per-state allowed mask: length ``vocab_size`` bytes 0/1. */
void build_allowed_mask_scalar(const JsonSubsetPda& pda, std::size_t vocab_size, std::uint8_t* out_mask);

/** Zero gate[i] where allowed[i]==0; AVX-512 path when TU built with AVX-512. */
void or_mask_all_allowed_avx512(std::uint8_t* logits_gate, const std::uint8_t* allowed, std::size_t n);

void apply_grammar_mask_dispatch(std::uint8_t* logits_gate, const std::uint8_t* allowed, std::size_t n);

}  // namespace qminiwasm::grammar
