#include "lexer.hpp"

#include <cctype>
#include <cmath>
#include <stdexcept>

namespace qminiwasm::quantum::openqasm3 {

Lexer::Lexer(std::string_view src) : src_(src) {}

std::optional<char> Lexer::peek() {
  if (pos_ >= src_.size()) {
    return std::nullopt;
  }
  return src_[pos_];
}

char Lexer::advance() {
  if (pos_ >= src_.size()) {
    return '\0';
  }
  const char c = src_[pos_++];
  if (c == '\n') {
    ++line_;
    col_ = 1;
  } else {
    ++col_;
  }
  return c;
}

void Lexer::skip_space_and_comments() {
  while (true) {
    const auto p = peek();
    if (!p) {
      break;
    }
    if (std::isspace(static_cast<unsigned char>(*p)) != 0) {
      (void)advance();
      continue;
    }
    if (*p == '/' && pos_ + 1 < src_.size() && src_[pos_ + 1] == '/') {
      while (peek() && *peek() != '\n') {
        (void)advance();
      }
      continue;
    }
    break;
  }
}

Token Lexer::make_token(TokenKind kind, std::size_t start_line, std::size_t start_col) {
  Token t;
  t.kind = kind;
  t.line = start_line;
  t.column = start_col;
  return t;
}

Token Lexer::next() {
  skip_space_and_comments();
  const std::size_t start_line = line_;
  const std::size_t start_col = col_;
  const auto p = peek();
  if (!p) {
    return make_token(TokenKind::kEof, start_line, start_col);
  }

  // Multi-char operators before single '-'
  if (*p == '-' && pos_ + 1 < src_.size() && src_[pos_ + 1] == '>') {
    (void)advance();
    (void)advance();
    return make_token(TokenKind::kArrow, start_line, start_col);
  }

  if (*p == ';') {
    (void)advance();
    return make_token(TokenKind::kSemicolon, start_line, start_col);
  }
  if (*p == ',') {
    (void)advance();
    return make_token(TokenKind::kComma, start_line, start_col);
  }
  if (*p == '(') {
    (void)advance();
    return make_token(TokenKind::kLParen, start_line, start_col);
  }
  if (*p == ')') {
    (void)advance();
    return make_token(TokenKind::kRParen, start_line, start_col);
  }
  if (*p == '[') {
    (void)advance();
    return make_token(TokenKind::kLBracket, start_line, start_col);
  }
  if (*p == ']') {
    (void)advance();
    return make_token(TokenKind::kRBracket, start_line, start_col);
  }
  if (*p == '/') {
    (void)advance();
    return make_token(TokenKind::kSlash, start_line, start_col);
  }
  if (*p == '*') {
    (void)advance();
    return make_token(TokenKind::kStar, start_line, start_col);
  }
  if (*p == '+') {
    (void)advance();
    return make_token(TokenKind::kPlus, start_line, start_col);
  }
  if (*p == '-') {
    (void)advance();
    return make_token(TokenKind::kMinus, start_line, start_col);
  }

  if (*p == '"') {
    (void)advance();
    std::string s;
    while (auto q = peek()) {
      if (*q == '"') {
        (void)advance();
        break;
      }
      if (*q == '\n' || *q == '\r') {
        break;
      }
      s.push_back(advance());
    }
    auto t = make_token(TokenKind::kString, start_line, start_col);
    t.text = std::move(s);
    return t;
  }

  // Numbers: digits or .digits
  if (std::isdigit(static_cast<unsigned char>(*p)) != 0 || *p == '.') {
    std::string num;
    while (auto q = peek()) {
      if (std::isdigit(static_cast<unsigned char>(*q)) != 0 || *q == '.' || *q == 'e' || *q == 'E' ||
          *q == '-' || *q == '+') {
        num.push_back(advance());
      } else {
        break;
      }
    }
    auto t = make_token(TokenKind::kNumber, start_line, start_col);
    t.text = num;
    try {
      t.number_value = std::stod(num);
    } catch (...) {
      t.number_value = 0.0;
    }
    return t;
  }

  // Identifiers and keywords
  if (std::isalpha(static_cast<unsigned char>(*p)) != 0 || *p == '_') {
    std::string id;
    while (auto q = peek()) {
      if (std::isalnum(static_cast<unsigned char>(*q)) != 0 || *q == '_') {
        id.push_back(advance());
      } else {
        break;
      }
    }
    auto t = make_token(TokenKind::kIdentifier, start_line, start_col);
    t.text = id;
    if (id == "OPENQASM") {
      t.kind = TokenKind::kOpenqasm;
    } else if (id == "include") {
      t.kind = TokenKind::kInclude;
    } else if (id == "qubit") {
      t.kind = TokenKind::kQubit;
    } else if (id == "bit") {
      t.kind = TokenKind::kBit;
    } else if (id == "pi" || id == "PI") {
      t.kind = TokenKind::kPi;
    }
    return t;
  }

  Token err = make_token(TokenKind::kInvalid, start_line, start_col);
  err.text = std::string("unexpected char: ") + *p;
  (void)advance();
  return err;
}

}  // namespace qminiwasm::quantum::openqasm3
