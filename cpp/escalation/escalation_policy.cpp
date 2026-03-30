#include "escalation_c_api.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

extern "C" {

int qmw_escalation_abi_version(void) { return 1; }

int qmw_escalation_resolve_next(QmwEscalationTier current_tier, uint32_t flags,
                                const QmwEscalationPolicy* policy, QmwEscalationTarget* out_target) {
  if (out_target == nullptr) {
    return QMW_ESCALATION_REASON_ERROR_NULL;
  }
  out_target->next_tier = QMW_ESCALATION_TIER_TERMINAL;
  out_target->backend_kind = QMW_ESCALATION_BACKEND_NONE;
  out_target->reason_code = QMW_ESCALATION_REASON_TERMINAL_END;

  if (policy == nullptr) {
    out_target->reason_code = QMW_ESCALATION_REASON_ERROR_POLICY;
    return QMW_ESCALATION_REASON_ERROR_POLICY;
  }

  const uint32_t max_hops = policy->max_tier_hops == 0 ? 8u : policy->max_tier_hops;
  if (policy->current_hop_index >= max_hops) {
    out_target->reason_code = QMW_ESCALATION_REASON_TERMINAL_HOP_CAP;
    return 0;
  }

  const bool need_qahr = (flags & QMW_ESCALATION_FLAG_PAYLOAD_REQUIRES_QAHR) != 0u;
  const bool low_fidelity = (flags & QMW_ESCALATION_FLAG_LOW_QPU_FIDELITY) != 0u;
  const bool qroute = policy->enable_quantum_routing != 0u;
  const bool external_qpu = policy->allow_external_qpu != 0u;

  switch (current_tier) {
    case QMW_ESCALATION_TIER1_EDGE:
      out_target->next_tier = QMW_ESCALATION_TIER2_STATE_MIGRATION;
      out_target->backend_kind = QMW_ESCALATION_BACKEND_CLASSICAL_ROUTING;
      out_target->reason_code = QMW_ESCALATION_REASON_OK;
      break;
    case QMW_ESCALATION_TIER2_STATE_MIGRATION:
      if (qroute || need_qahr) {
        out_target->next_tier = QMW_ESCALATION_TIER3_QAHR_CLASSICAL;
        out_target->backend_kind = QMW_ESCALATION_BACKEND_CLASSICAL_ROUTING;
        out_target->reason_code = QMW_ESCALATION_REASON_OK;
      } else {
        out_target->reason_code = QMW_ESCALATION_REASON_TERMINAL_POLICY_CLASSICAL;
      }
      break;
    case QMW_ESCALATION_TIER3_QAHR_CLASSICAL:
#if QMINIWASM_HAS_QUANTUM
      if (policy->enable_native_qsim != 0u) {
        out_target->next_tier = QMW_ESCALATION_TIER4_NATIVE_QUANTUM_SIM;
        out_target->backend_kind = QMW_ESCALATION_BACKEND_NATIVE_OPENQASM_SIM;
        out_target->reason_code = QMW_ESCALATION_REASON_OK;
        break;
      }
#endif
      if (external_qpu) {
        out_target->next_tier = QMW_ESCALATION_TIER5_EXTERNAL_QPU;
        out_target->backend_kind = QMW_ESCALATION_BACKEND_EXTERNAL_PROVIDER;
        out_target->reason_code = QMW_ESCALATION_REASON_OK;
      } else {
        out_target->reason_code = QMW_ESCALATION_REASON_TERMINAL_NO_BACKEND;
      }
      break;
    case QMW_ESCALATION_TIER4_NATIVE_QUANTUM_SIM:
      if (low_fidelity && external_qpu) {
        out_target->next_tier = QMW_ESCALATION_TIER5_EXTERNAL_QPU;
        out_target->backend_kind = QMW_ESCALATION_BACKEND_EXTERNAL_PROVIDER;
        out_target->reason_code = QMW_ESCALATION_REASON_OK;
      } else {
        out_target->reason_code = QMW_ESCALATION_REASON_TERMINAL_END;
      }
      break;
    case QMW_ESCALATION_TIER5_EXTERNAL_QPU:
    case QMW_ESCALATION_TIER_TERMINAL:
    default:
      out_target->reason_code = QMW_ESCALATION_REASON_TERMINAL_END;
      break;
  }

  (void)policy->qpu_fidelity_threshold;
  return 0;
}

void qmw_escalation_header_init(QmwEscalationEnvelopeHeader* h, std::uint8_t source_tier, std::uint8_t target_tier,
                                std::uint32_t payload_byte_length, std::uint32_t flags) {
  if (h == nullptr) {
    return;
  }
  std::memset(h, 0, sizeof(*h));
  h->magic = QMW_ESCALATION_ENVELOPE_MAGIC;
  h->format_version = static_cast<std::uint16_t>(QMW_ESCALATION_ENVELOPE_FORMAT_VERSION);
  h->source_tier = source_tier;
  h->target_tier = target_tier;
  h->payload_byte_length = payload_byte_length;
  h->flags = flags;
}

int qmw_escalation_header_validate(const uint8_t* buf, size_t len, uint32_t* out_err) {
  auto set_err = [out_err](uint32_t c) -> int {
    if (out_err != nullptr) {
      *out_err = c;
    }
    return c == 0u ? 0 : -1;
  };
  if (buf == nullptr || len < sizeof(QmwEscalationEnvelopeHeader)) {
    return set_err(1u);
  }
  QmwEscalationEnvelopeHeader h{};
  std::memcpy(&h, buf, sizeof(h));
  if (h.magic != QMW_ESCALATION_ENVELOPE_MAGIC) {
    return set_err(2u);
  }
  if (h.format_version != static_cast<std::uint16_t>(QMW_ESCALATION_ENVELOPE_FORMAT_VERSION)) {
    return set_err(3u);
  }
  return set_err(0u);
}

}
