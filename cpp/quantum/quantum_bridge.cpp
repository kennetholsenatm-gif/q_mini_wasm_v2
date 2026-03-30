#include "quantum_bridge.hpp"

#include "openqasm3/parser.hpp"
#include "trinary/device.hpp"

namespace qminiwasm::quantum {

std::expected<double, std::string> openqasm_expval_pauli_z0(std::string_view openqasm_source, std::uint64_t seed) {
  auto ir = openqasm3::parse_openqasm_program(openqasm_source);
  if (!ir) {
    return std::unexpected(ir.error().message);
  }
  trinary::TrinarySimDevice dev;
  if (auto r = dev.run_openqasm_ir(*ir, seed); !r) {
    return std::unexpected(r.error());
  }
  return dev.expval_pauli_z(0);
}

}  // namespace qminiwasm::quantum
