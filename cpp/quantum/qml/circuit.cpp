#include "circuit.hpp"

#include <algorithm>
#include <type_traits>
#include <variant>

namespace qminiwasm::quantum::qml {

namespace {

int max_wire_from_ir(const openqasm3::CircuitIR& ir) {
  int mw = -1;
  for (const auto& op : ir.ops) {
    std::visit(
        [&](auto&& arg) {
          using T = std::decay_t<decltype(arg)>;
          if constexpr (std::is_same_v<T, openqasm3::OpApply1>) {
            mw = std::max(mw, arg.wire);
          } else if constexpr (std::is_same_v<T, openqasm3::OpApply2>) {
            mw = std::max({mw, arg.control, arg.target});
          } else if constexpr (std::is_same_v<T, openqasm3::OpMeasure>) {
            mw = std::max(mw, arg.q_wire);
          }
        },
        op);
  }
  return mw;
}

}  // namespace

void CircuitTape::clear() {
  ir_ = openqasm3::CircuitIR{};
}

void CircuitTape::hadamard(int wire) {
  ir_.ops.push_back(openqasm3::OpApply1{openqasm3::BuiltinGate1::kHadamard, wire, 0.0});
}

void CircuitTape::pauli_x(int wire) {
  ir_.ops.push_back(openqasm3::OpApply1{openqasm3::BuiltinGate1::kPauliX, wire, 0.0});
}

void CircuitTape::pauli_y(int wire) {
  ir_.ops.push_back(openqasm3::OpApply1{openqasm3::BuiltinGate1::kPauliY, wire, 0.0});
}

void CircuitTape::pauli_z(int wire) {
  ir_.ops.push_back(openqasm3::OpApply1{openqasm3::BuiltinGate1::kPauliZ, wire, 0.0});
}

void CircuitTape::rx(int wire, double theta) {
  ir_.ops.push_back(openqasm3::OpApply1{openqasm3::BuiltinGate1::kRx, wire, theta});
}

void CircuitTape::ry(int wire, double theta) {
  ir_.ops.push_back(openqasm3::OpApply1{openqasm3::BuiltinGate1::kRy, wire, theta});
}

void CircuitTape::rz(int wire, double theta) {
  ir_.ops.push_back(openqasm3::OpApply1{openqasm3::BuiltinGate1::kRz, wire, theta});
}

void CircuitTape::cnot(int control, int target) {
  ir_.ops.push_back(openqasm3::OpApply2{openqasm3::BuiltinGate2::kCx, control, target});
}

void CircuitTape::measure(int qubit_wire, int classical_bit) {
  ir_.ops.push_back(openqasm3::OpMeasure{qubit_wire, classical_bit});
}

std::expected<void, std::string> CircuitTape::execute(TrinarySimDevice& dev, std::uint64_t seed) {
  const int mw = max_wire_from_ir(ir_);
  if (mw < 0) {
    return std::unexpected(std::string("circuit tape has no operations"));
  }
  ir_.num_qubits = std::max(ir_.num_qubits, mw + 1);
  return dev.run_openqasm_ir(ir_, seed);
}

}  // namespace qminiwasm::quantum::qml
