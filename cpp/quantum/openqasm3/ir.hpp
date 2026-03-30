#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace qminiwasm::quantum::openqasm3 {

enum class BuiltinGate1 { kHadamard, kPauliX, kPauliY, kPauliZ, kRx, kRy, kRz };

enum class BuiltinGate2 { kCx, kCy, kCz };

struct OpApply1 {
  BuiltinGate1 gate{};
  int wire{-1};
  double param{0.0};  // radians for rx/ry/rz; ignored otherwise
};

struct OpApply2 {
  BuiltinGate2 gate{};
  int control{-1};
  int target{-1};
};

struct OpMeasure {
  int q_wire{-1};
  int c_bit{-1};
};

using CircuitOp = std::variant<OpApply1, OpApply2, OpMeasure>;

/** Flattened circuit after semantic analysis (wire indices are global). */
struct CircuitIR {
  int num_qubits{0};
  int num_clbits{0};
  std::vector<CircuitOp> ops{};
};

struct ParseError {
  std::string message;
  std::size_t line{1};
  std::size_t column{1};
};

}  // namespace qminiwasm::quantum::openqasm3
