#include "quantum_bridge_c_api.h"

#include "quantum_bridge.hpp"

extern "C" const char* qmw_native_quantum_stack_summary(void) {
  return "qminiwasm_native_quantum v1: OpenQASM3_subset lexer_parser IR; "
         "trinary_LibTorch_CPU statevector qubit_gates_on_qutrit_subspace; "
         "C_API qmw_openqasm_expval_pauli_z0";
}

extern "C" double qmw_openqasm_expval_pauli_z0(const char* openqasm_source, unsigned long long seed,
                                              int* err_out) {
  if (openqasm_source == nullptr) {
    if (err_out != nullptr) {
      *err_out = 1;
    }
    return 0.0;
  }
  auto r = qminiwasm::quantum::openqasm_expval_pauli_z0(openqasm_source, seed);
  if (!r) {
    if (err_out != nullptr) {
      *err_out = 2;
    }
    return 0.0;
  }
  if (err_out != nullptr) {
    *err_out = 0;
  }
  return *r;
}
