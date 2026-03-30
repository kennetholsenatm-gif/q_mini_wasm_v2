#include "escalation_c_api.h"

#if QMINIWASM_HAS_QUANTUM
#include "../quantum/quantum_bridge_c_api.h"
#endif

extern "C" {

double qmw_escalation_run_native_openqasm_if_applicable(QmwEscalationTier last_resolved_tier, const char* openqasm_source,
                                                         unsigned long long seed, int* err_out) {
  auto set_err = [err_out](int c, double v) -> double {
    if (err_out != nullptr) {
      *err_out = c;
    }
    return v;
  };

  if (last_resolved_tier != QMW_ESCALATION_TIER4_NATIVE_QUANTUM_SIM) {
    return set_err(3, 0.0);
  }
#if QMINIWASM_HAS_QUANTUM
  return qmw_openqasm_expval_pauli_z0(openqasm_source, seed, err_out);
#else
  (void)openqasm_source;
  (void)seed;
  return set_err(100, 0.0);
#endif
}

}
