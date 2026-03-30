#include "../quantum/openqasm3/parser.hpp"
#include "../quantum/qml/circuit.hpp"
#include "../quantum/quantum_bridge.hpp"
#include "../quantum/trinary/device.hpp"
#include "../quantum/quantum_bridge_c_api.h"

#include <cmath>
#include <cstring>

bool test_openqasm_quantum() {
  const char* ground = R"(
qubit q;
)";
  auto g = qminiwasm::quantum::openqasm3::parse_openqasm_program(ground);
  if (!g) {
    return false;
  }
  qminiwasm::quantum::trinary::TrinarySimDevice dev_ground;
  if (!dev_ground.run_openqasm_ir(*g, 1)) {
    return false;
  }
  if (std::fabs(dev_ground.expval_pauli_z(0) - 1.0) > 1e-5) {
    return false;
  }

  const char* plus = R"(
qubit q;
h q;
)";
  auto p = qminiwasm::quantum::openqasm3::parse_openqasm_program(plus);
  if (!p) {
    return false;
  }
  qminiwasm::quantum::trinary::TrinarySimDevice dev_plus;
  if (!dev_plus.run_openqasm_ir(*p, 2)) {
    return false;
  }
  if (std::fabs(dev_plus.expval_pauli_z(0)) > 1e-4) {
    return false;
  }

  const char* bell = R"(
OPENQASM 3.0;
qubit q0;
qubit q1;
h q0;
cx q0, q1;
)";
  auto ir = qminiwasm::quantum::openqasm3::parse_openqasm_program(bell);
  if (!ir) {
    return false;
  }
  qminiwasm::quantum::trinary::TrinarySimDevice dev;
  if (!dev.run_openqasm_ir(*ir, 42)) {
    return false;
  }
  if (std::fabs(dev.expval_pauli_z(0)) > 1e-4) {
    return false;
  }

  auto bridge = qminiwasm::quantum::openqasm_expval_pauli_z0(bell, 42);
  if (!bridge) {
    return false;
  }
  if (std::fabs(*bridge) > 1e-4) {
    return false;
  }

  qminiwasm::quantum::qml::CircuitTape tape;
  tape.clear();
  tape.hadamard(0);
  tape.cnot(0, 1);
  qminiwasm::quantum::trinary::TrinarySimDevice dev2;
  if (!tape.execute(dev2, 99)) {
    return false;
  }
  if (std::fabs(dev2.expval_pauli_z(0)) > 1e-4) {
    return false;
  }

  int err = 0;
  const double zc =
      qmw_openqasm_expval_pauli_z0(bell, 42ULL, &err);
  if (err != 0) {
    return false;
  }
  if (std::fabs(zc) > 1e-4) {
    return false;
  }

  const char* summary = qmw_native_quantum_stack_summary();
  if (summary == nullptr || std::strstr(summary, "OpenQASM3") == nullptr) {
    return false;
  }

  return true;
}
