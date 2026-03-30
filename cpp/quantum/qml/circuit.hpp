#pragma once

#include "../openqasm3/ir.hpp"
#include "../trinary/device.hpp"

#include <cstdint>
#include <expected>
#include <string>

namespace qminiwasm::quantum::qml {

/** Minimal PennyLane-style tape: record OpenQASM-like ops into CircuitIR, execute on TrinarySimDevice. */
class CircuitTape {
 public:
  void clear();
  openqasm3::CircuitIR& ir() { return ir_; }
  const openqasm3::CircuitIR& ir() const { return ir_; }

  void hadamard(int wire);
  void pauli_x(int wire);
  void pauli_y(int wire);
  void pauli_z(int wire);
  void rx(int wire, double theta);
  void ry(int wire, double theta);
  void rz(int wire, double theta);
  void cnot(int control, int target);
  void measure(int qubit_wire, int classical_bit);

  std::expected<void, std::string> execute(TrinarySimDevice& dev, std::uint64_t seed = 1);

 private:
  openqasm3::CircuitIR ir_{};
};

}  // namespace qminiwasm::quantum::qml
