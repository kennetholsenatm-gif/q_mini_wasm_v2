#include "parser.hpp"

#include "lexer.hpp"

#define _USE_MATH_DEFINES
#include <cmath>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef _MSC_VER
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#endif

namespace qminiwasm::quantum::openqasm3 {

namespace {

struct Register {
  int base{0};
  int width{1};
};

class Parser {
 public:
  explicit Parser(std::string_view src) : lex_(src) {}

  std::expected<CircuitIR, ParseError> parse_program() {
    CircuitIR circ;
    Token t = next_significant();
    if (t.kind == TokenKind::kOpenqasm) {
      t = next_significant();
      if (t.kind != TokenKind::kNumber) {
        return err(t, "expected version number after OPENQASM");
      }
      if (!consume_semicolon()) {
        Token bad = pull();
        return err(bad, "expected ';' after OPENQASM version");
      }
    } else {
      push_front(std::move(t));
    }

    while (true) {
      t = next_significant();
      if (t.kind == TokenKind::kEof) {
        break;
      }
      if (t.kind == TokenKind::kInvalid) {
        return err(t, t.text.empty() ? "invalid token" : t.text);
      }
      if (t.kind == TokenKind::kInclude) {
        if (auto e = skip_include_line()) {
          return std::unexpected(*e);
        }
        continue;
      }
      if (t.kind == TokenKind::kQubit) {
        if (auto e = parse_qubit_decl()) {
          return std::unexpected(*e);
        }
        continue;
      }
      if (t.kind == TokenKind::kBit) {
        if (auto e = parse_bit_decl()) {
          return std::unexpected(*e);
        }
        continue;
      }
      if (t.kind == TokenKind::kIdentifier) {
        if (auto e = parse_gate_or_measure(t.text, circ)) {
          return std::unexpected(*e);
        }
        continue;
      }
      return err(t, "unexpected token");
    }

    circ.num_qubits = q_next_;
    circ.num_clbits = c_next_;
    return circ;
  }

 private:
  Lexer lex_;
  std::vector<Token> buf_{};
  int q_next_{0};
  int c_next_{0};
  std::unordered_map<std::string, Register> qregs_{};
  std::unordered_map<std::string, Register> cregs_{};

  ParseError make_err(std::size_t line, std::size_t col, std::string msg) {
    return ParseError{std::move(msg), line, col};
  }

  std::expected<void, ParseError> err(Token t, std::string msg) {
    return std::unexpected(make_err(t.line, t.column, std::move(msg)));
  }

  Token pull() {
    if (!buf_.empty()) {
      Token t = buf_.back();
      buf_.pop_back();
      return t;
    }
    return lex_.next();
  }

  void push_front(Token t) { buf_.push_back(std::move(t)); }

  Token next_significant() {
    while (true) {
      Token t = pull();
      if (t.kind != TokenKind::kSemicolon) {
        return t;
      }
    }
  }

  bool consume_semicolon() {
    Token t = pull();
    if (t.kind == TokenKind::kSemicolon) {
      return true;
    }
    push_front(std::move(t));
    return false;
  }

  std::optional<ParseError> skip_include_line() {
    Token t = pull();
    if (t.kind != TokenKind::kString) {
      return make_err(t.line, t.column, "expected include path string");
    }
    if (!consume_semicolon()) {
      Token bad = pull();
      return make_err(bad.line, bad.column, "expected ';' after include");
    }
    return std::nullopt;
  }

  std::optional<ParseError> parse_qubit_decl() {
    int width = 1;
    Token first = pull();
    if (first.kind == TokenKind::kLBracket) {
      Token n = pull();
      if (n.kind != TokenKind::kNumber) {
        return make_err(n.line, n.column, "expected qubit register width");
      }
      width = static_cast<int>(n.number_value);
      if (width < 1) {
        return make_err(n.line, n.column, "qubit width must be >= 1");
      }
      Token close = pull();
      if (close.kind != TokenKind::kRBracket) {
        return make_err(close.line, close.column, "expected ']' after qubit width");
      }
    } else {
      push_front(std::move(first));
    }

    Token name = pull();
    if (name.kind != TokenKind::kIdentifier) {
      return make_err(name.line, name.column, "expected qubit register name");
    }
    Register r{.base = q_next_, .width = width};
    q_next_ += width;
    qregs_[name.text] = r;

    if (!consume_semicolon()) {
      Token bad = pull();
      return make_err(bad.line, bad.column, "expected ';' after qubit decl");
    }
    return std::nullopt;
  }

  std::optional<ParseError> parse_bit_decl() {
    int width = 1;
    Token first = pull();
    if (first.kind == TokenKind::kLBracket) {
      Token n = pull();
      if (n.kind != TokenKind::kNumber) {
        return make_err(n.line, n.column, "expected classical register width");
      }
      width = static_cast<int>(n.number_value);
      if (width < 1) {
        return make_err(n.line, n.column, "bit width must be >= 1");
      }
      Token close = pull();
      if (close.kind != TokenKind::kRBracket) {
        return make_err(close.line, close.column, "expected ']'");
      }
    } else {
      push_front(std::move(first));
    }

    Token name = pull();
    if (name.kind != TokenKind::kIdentifier) {
      return make_err(name.line, name.column, "expected classical register name");
    }
    Register r{.base = c_next_, .width = width};
    c_next_ += width;
    cregs_[name.text] = r;

    if (!consume_semicolon()) {
      Token bad = pull();
      return make_err(bad.line, bad.column, "expected ';' after bit decl");
    }
    return std::nullopt;
  }

  std::expected<int, ParseError> qubit_wire(const std::string& reg_name, std::size_t line, std::size_t col,
                                            bool indexed, int index) {
    auto it = qregs_.find(reg_name);
    if (it == qregs_.end()) {
      return std::unexpected(make_err(line, col, "unknown qubit register: " + reg_name));
    }
    int off = 0;
    if (indexed) {
      off = index;
    } else if (it->second.width != 1) {
      return std::unexpected(make_err(line, col, "index required for quantum register"));
    }
    if (off < 0 || off >= it->second.width) {
      return std::unexpected(make_err(line, col, "qubit index out of range"));
    }
    return it->second.base + off;
  }

  std::expected<int, ParseError> clbit_wire(const std::string& reg_name, std::size_t line, std::size_t col,
                                            bool indexed, int index) {
    auto it = cregs_.find(reg_name);
    if (it == cregs_.end()) {
      return std::unexpected(make_err(line, col, "unknown classical register: " + reg_name));
    }
    int off = 0;
    if (indexed) {
      off = index;
    } else if (it->second.width != 1) {
      return std::unexpected(make_err(line, col, "index required for classical register"));
    }
    if (off < 0 || off >= it->second.width) {
      return std::unexpected(make_err(line, col, "classical bit index out of range"));
    }
    return it->second.base + off;
  }

  std::expected<int, ParseError> parse_wire_ref_q() {
    Token name = pull();
    if (name.kind != TokenKind::kIdentifier) {
      return std::unexpected(make_err(name.line, name.column, "expected qubit identifier"));
    }
    Token next = pull();
    if (next.kind == TokenKind::kLBracket) {
      Token num = pull();
      if (num.kind != TokenKind::kNumber) {
        return std::unexpected(make_err(num.line, num.column, "expected index"));
      }
      Token close = pull();
      if (close.kind != TokenKind::kRBracket) {
        return std::unexpected(make_err(close.line, close.column, "expected ']'"));
      }
      return qubit_wire(name.text, num.line, num.column, true, static_cast<int>(num.number_value));
    }
    push_front(std::move(next));
    return qubit_wire(name.text, name.line, name.column, false, 0);
  }

  std::expected<int, ParseError> parse_wire_ref_c() {
    Token name = pull();
    if (name.kind != TokenKind::kIdentifier) {
      return std::unexpected(make_err(name.line, name.column, "expected classical identifier"));
    }
    Token next = pull();
    if (next.kind == TokenKind::kLBracket) {
      Token num = pull();
      if (num.kind != TokenKind::kNumber) {
        return std::unexpected(make_err(num.line, num.column, "expected index"));
      }
      Token close = pull();
      if (close.kind != TokenKind::kRBracket) {
        return std::unexpected(make_err(close.line, close.column, "expected ']'"));
      }
      return clbit_wire(name.text, num.line, num.column, true, static_cast<int>(num.number_value));
    }
    push_front(std::move(next));
    return clbit_wire(name.text, name.line, name.column, false, 0);
  }

  std::optional<ParseError> parse_measure_statement(CircuitIR& circ) {
    auto q = parse_wire_ref_q();
    if (!q) {
      return q.error();
    }
    Token arrow = pull();
    if (arrow.kind != TokenKind::kArrow) {
      return make_err(arrow.line, arrow.column, "expected '->' in measure");
    }
    auto c = parse_wire_ref_c();
    if (!c) {
      return c.error();
    }
    circ.ops.push_back(OpMeasure{*q, *c});
    if (!consume_semicolon()) {
      Token bad = pull();
      return make_err(bad.line, bad.column, "expected ';' after measure");
    }
    return std::nullopt;
  }

  std::optional<ParseError> parse_angle(double& out) {
    double sign = 1.0;
    Token t = pull();
    if (t.kind == TokenKind::kMinus) {
      sign = -1.0;
      t = pull();
    } else if (t.kind == TokenKind::kPlus) {
      t = pull();
    }

    if (t.kind == TokenKind::kPi) {
      out = sign * M_PI;
    } else if (t.kind == TokenKind::kNumber) {
      out = sign * t.number_value;
    } else {
      return make_err(t.line, t.column, "expected angle literal");
    }

    Token op = pull();
    if (op.kind == TokenKind::kSlash) {
      Token den = pull();
      if (den.kind != TokenKind::kNumber || den.number_value == 0.0) {
        return make_err(den.line, den.column, "expected nonzero denominator");
      }
      out /= den.number_value;
      return std::nullopt;
    }
    push_front(std::move(op));
    return std::nullopt;
  }

  std::optional<ParseError> parse_gate_or_measure(const std::string& gate, CircuitIR& circ) {
    if (gate == "measure") {
      return parse_measure_statement(circ);
    }

    BuiltinGate1 g1{};
    bool is1 = true;
    BuiltinGate2 g2{};
    bool has_param = false;

    if (gate == "h" || gate == "H") {
      g1 = BuiltinGate1::kHadamard;
    } else if (gate == "x" || gate == "X") {
      g1 = BuiltinGate1::kPauliX;
    } else if (gate == "y" || gate == "Y") {
      g1 = BuiltinGate1::kPauliY;
    } else if (gate == "z" || gate == "Z") {
      g1 = BuiltinGate1::kPauliZ;
    } else if (gate == "rx") {
      g1 = BuiltinGate1::kRx;
      has_param = true;
    } else if (gate == "ry") {
      g1 = BuiltinGate1::kRy;
      has_param = true;
    } else if (gate == "rz") {
      g1 = BuiltinGate1::kRz;
      has_param = true;
    } else if (gate == "cx" || gate == "CX") {
      is1 = false;
      g2 = BuiltinGate2::kCx;
    } else if (gate == "cy" || gate == "CY") {
      is1 = false;
      g2 = BuiltinGate2::kCy;
    } else if (gate == "cz" || gate == "CZ") {
      is1 = false;
      g2 = BuiltinGate2::kCz;
    } else {
      Token fake{};
      fake.line = 1;
      fake.column = 1;
      return make_err(fake.line, fake.column, "unsupported gate: " + gate);
    }

    double param = 0.0;
    if (has_param) {
      Token lp = pull();
      if (lp.kind != TokenKind::kLParen) {
        return make_err(lp.line, lp.column, "expected '(' for gate parameter");
      }
      if (auto e = parse_angle(param)) {
        return *e;
      }
      Token rp = pull();
      if (rp.kind != TokenKind::kRParen) {
        return make_err(rp.line, rp.column, "expected ')' after angle");
      }
    }

    if (is1) {
      auto q = parse_wire_ref_q();
      if (!q) {
        return q.error();
      }
      circ.ops.push_back(OpApply1{g1, *q, param});
    } else {
      auto qc = parse_wire_ref_q();
      if (!qc) {
        return qc.error();
      }
      Token comma = pull();
      if (comma.kind != TokenKind::kComma) {
        return make_err(comma.line, comma.column, "expected ',' between gate operands");
      }
      auto qt = parse_wire_ref_q();
      if (!qt) {
        return qt.error();
      }
      circ.ops.push_back(OpApply2{g2, *qc, *qt});
    }

    if (!consume_semicolon()) {
      Token bad = pull();
      return make_err(bad.line, bad.column, "expected ';' after gate");
    }
    return std::nullopt;
  }
};

}  // namespace

std::expected<CircuitIR, ParseError> parse_openqasm_program(std::string_view source) {
  Parser p(source);
  return p.parse_program();
}

}  // namespace qminiwasm::quantum::openqasm3
</think>


<｜tool▁calls▁begin｜><｜tool▁call▁begin｜>
Read