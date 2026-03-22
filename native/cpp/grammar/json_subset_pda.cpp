#include "json_subset_pda.hpp"

namespace qminiwasm::grammar {

JsonSubsetPda::JsonSubsetPda() { reset(); }

void JsonSubsetPda::reset() {
  stack_.clear();
  stack_.push_back(0);
}

bool JsonSubsetPda::push_token(std::uint8_t tid) {
  const int s = state_id();
  // Simplified transitions: 0 wants '{', 1 wants string start, etc.
  if (s == 0 && tid == static_cast<std::uint8_t>(JsonTokenId::LBrace)) {
    stack_.push_back(1);
    return true;
  }
  if (s == 1 && tid == static_cast<std::uint8_t>(JsonTokenId::Quote)) {
    stack_.push_back(2);
    return true;
  }
  if (s == 2 && tid == static_cast<std::uint8_t>(JsonTokenId::Colon)) {
    stack_.push_back(3);
    return true;
  }
  if (s == 3 && tid >= static_cast<std::uint8_t>(JsonTokenId::Digit0) &&
      tid <= static_cast<std::uint8_t>(JsonTokenId::Digit0) + 9) {
    stack_.push_back(4);
    return true;
  }
  if (s == 4 && tid == static_cast<std::uint8_t>(JsonTokenId::RBrace)) {
    stack_.push_back(5);
    return true;
  }
  (void)tid;
  return false;
}

}  // namespace qminiwasm::grammar
