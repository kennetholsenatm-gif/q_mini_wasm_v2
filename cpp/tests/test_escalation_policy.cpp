#include "../escalation/escalation_c_api.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace {

QmwEscalationPolicy default_policy() {
  QmwEscalationPolicy p{};
  p.enable_quantum_routing = 1;
  p.enable_native_qsim = 1;
  p.allow_external_qpu = 1;
  p.qpu_fidelity_threshold = 0.9f;
  p.max_tier_hops = 8;
  p.current_hop_index = 0;
  return p;
}

}  // namespace

bool test_escalation_policy() {
  if (qmw_escalation_abi_version() < 1) {
    return false;
  }

  QmwEscalationTarget t{};
  QmwEscalationPolicy p = default_policy();
  if (qmw_escalation_resolve_next(QMW_ESCALATION_TIER1_EDGE, 0, &p, &t) != 0) {
    return false;
  }
  if (t.next_tier != QMW_ESCALATION_TIER2_STATE_MIGRATION ||
      t.backend_kind != QMW_ESCALATION_BACKEND_CLASSICAL_ROUTING) {
    return false;
  }

  if (qmw_escalation_resolve_next(QMW_ESCALATION_TIER2_STATE_MIGRATION, 0, &p, &t) != 0) {
    return false;
  }
  if (t.next_tier != QMW_ESCALATION_TIER3_QAHR_CLASSICAL) {
    return false;
  }

  p.enable_quantum_routing = 0;
  if (qmw_escalation_resolve_next(QMW_ESCALATION_TIER2_STATE_MIGRATION, 0, &p, &t) != 0) {
    return false;
  }
  if (t.next_tier != QMW_ESCALATION_TIER_TERMINAL ||
      t.reason_code != QMW_ESCALATION_REASON_TERMINAL_POLICY_CLASSICAL) {
    return false;
  }
  p.enable_quantum_routing = 1;

  p = default_policy();
  if (qmw_escalation_resolve_next(QMW_ESCALATION_TIER3_QAHR_CLASSICAL, 0, &p, &t) != 0) {
    return false;
  }
#if QMINIWASM_HAS_QUANTUM
  if (t.next_tier != QMW_ESCALATION_TIER4_NATIVE_QUANTUM_SIM ||
      t.backend_kind != QMW_ESCALATION_BACKEND_NATIVE_OPENQASM_SIM) {
    return false;
  }
#else
  if (t.next_tier != QMW_ESCALATION_TIER5_EXTERNAL_QPU) {
    return false;
  }
#endif

  p = default_policy();
#if QMINIWASM_HAS_QUANTUM
  if (qmw_escalation_resolve_next(QMW_ESCALATION_TIER3_QAHR_CLASSICAL, 0, &p, &t) != 0) {
    return false;
  }
  if (qmw_escalation_resolve_next(QMW_ESCALATION_TIER4_NATIVE_QUANTUM_SIM, QMW_ESCALATION_FLAG_LOW_QPU_FIDELITY, &p,
                                  &t) != 0) {
   return false;
  }
  if (t.next_tier != QMW_ESCALATION_TIER5_EXTERNAL_QPU) {
    return false;
  }
#endif

  p = default_policy();
  p.enable_native_qsim = 0;
  p.allow_external_qpu = 0;
  if (qmw_escalation_resolve_next(QMW_ESCALATION_TIER3_QAHR_CLASSICAL, 0, &p, &t) != 0) {
    return false;
  }
  if (t.reason_code != QMW_ESCALATION_REASON_TERMINAL_NO_BACKEND) {
    return false;
  }

  p = default_policy();
  p.current_hop_index = 8;
  if (qmw_escalation_resolve_next(QMW_ESCALATION_TIER1_EDGE, 0, &p, &t) != 0) {
    return false;
  }
  if (t.reason_code != QMW_ESCALATION_REASON_TERMINAL_HOP_CAP) {
    return false;
  }

  if (qmw_escalation_resolve_next(QMW_ESCALATION_TIER1_EDGE, 0, nullptr, &t) != QMW_ESCALATION_REASON_ERROR_POLICY) {
    return false;
  }

  QmwEscalationEnvelopeHeader h{};
  qmw_escalation_header_init(&h, 2, 3, 1024u, 0u);
  if (h.magic != QMW_ESCALATION_ENVELOPE_MAGIC || h.format_version != 1u || h.source_tier != 2 || h.target_tier != 3 ||
      h.payload_byte_length != 1024u) {
    return false;
  }
  std::vector<std::uint8_t> buf(sizeof(QmwEscalationEnvelopeHeader));
  std::memcpy(buf.data(), &h, sizeof(h));
  uint32_t err = 999u;
  if (qmw_escalation_header_validate(buf.data(), buf.size(), &err) != 0 || err != 0u) {
    return false;
  }
  if (qmw_escalation_header_validate(buf.data(), buf.size() - 1u, &err) == 0) {
    return false;
  }
  buf[0] ^= 0xff;
  if (qmw_escalation_header_validate(buf.data(), buf.size(), &err) == 0) {
    return false;
  }

  int qerr = -1;
  double q = qmw_escalation_run_native_openqasm_if_applicable(QMW_ESCALATION_TIER3_QAHR_CLASSICAL, "OPENQASM 3.0;", 0ull,
                                                               &qerr);
  (void)q;
  if (qerr != 3) {
    return false;
  }
#if !QMINIWASM_HAS_QUANTUM
  q = qmw_escalation_run_native_openqasm_if_applicable(QMW_ESCALATION_TIER4_NATIVE_QUANTUM_SIM, "OPENQASM 3.0;", 0ull,
                                                       &qerr);
  (void)q;
  if (qerr != 100) {
    return false;
  }
#endif

  return true;
}
