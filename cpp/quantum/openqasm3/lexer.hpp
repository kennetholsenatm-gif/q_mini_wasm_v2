#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace qminiwasm::quantum::openqasm3 {

enum class TokenKind {
  kEof,
  kInvalid,
  kOpenqasm,
  kInclude,
  kQubit,
  kBit,
  kPi,
  kIdentifier,
  kNumber,
  kSemicolon,
  kComma,
  kLParen,
  kRParen,
  kLBracket,
  kRBracket,
  kArrow,
  kSlash,
  kStar,
  kPlus,
  kMinus,
  kString,
};

struct Token {
  TokenKind kind{TokenKind::kEof};
  std::size_t line{1};
  std::size_t column{1};
  std::string text{};
  double number_value{0.0};
};

class Lexer {
 public:
  explicit Lexer(std::string_view src);

  Token next();

 private:
  void skip_space_and_comments();
  std::optional<char> peek();
  char advance();
  Token make_token(TokenKind kind, std::size_t start_line, std::size_t start_col);

  std::string_view src_;
  std::size_t pos_{0};
  std::size_t line_{1};
  std::size_t col_{1};
};

}  // namespace qminiwasm::quantum::openqasm3
