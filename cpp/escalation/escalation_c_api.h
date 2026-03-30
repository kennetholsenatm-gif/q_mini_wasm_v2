#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Bump when this header's contract changes. */
int qmw_escalation_abi_version(void);

/**
 * Hierarchical escalation tiers (Certainty-Gated Escalation / multi-hop).
 * Tier 2 (State 2) is not terminal when policy enables further processing.
 */
typedef enum QmwEscalationTier {
  QMW_ESCALATION_TIER_TERMINAL = 0,
  QMW_ESCALATION_TIER1_EDGE = 1,
  QMW_ESCALATION_TIER2_STATE_MIGRATION = 2,
  QMW_ESCALATION_TIER3_QAHR_CLASSICAL = 3,
  QMW_ESCALATION_TIER4_NATIVE_QUANTUM_SIM = 4,
  QMW_ESCALATION_TIER5_EXTERNAL_QPU = 5,
} QmwEscalationTier;

typedef enum QmwEscalationBackendKind {
  QMW_ESCALATION_BACKEND_NONE = 0,
  QMW_ESCALATION_BACKEND_CLASSICAL_ROUTING = 1,
  QMW_ESCALATION_BACKEND_NATIVE_OPENQASM_SIM = 2,
  QMW_ESCALATION_BACKEND_EXTERNAL_PROVIDER = 3,
} QmwEscalationBackendKind;

/** Bitmask for qmw_escalation_resolve_next. */
#define QMW_ESCALATION_FLAG_LOW_QPU_FIDELITY (1u << 0)
#define QMW_ESCALATION_FLAG_PAYLOAD_REQUIRES_QAHR (1u << 1)

/**
 * Policy subset mirroring Python HierarchicalConfig (enable_quantum_routing, qpu_fidelity, etc.).
 * All boolean fields use 0 = false, 1 = true.
 */
typedef struct QmwEscalationPolicy {
  uint32_t enable_quantum_routing;
  uint32_t enable_native_qsim;
  uint32_t allow_external_qpu;
  float qpu_fidelity_threshold;
  /** Max handoff steps for a single runaway chain; pass 0 to use default (8). */
  uint32_t max_tier_hops;
  /** Zero-based hop index of this resolution; if hop_index + 1 >= max_tier_hops, next is TERMINAL. */
  uint32_t current_hop_index;
} QmwEscalationPolicy;

typedef struct QmwEscalationTarget {
  QmwEscalationTier next_tier;
  QmwEscalationBackendKind backend_kind;
  /** Diagnostic: 0 ok, positive = terminal/stop reason, negative = error */
  int reason_code;
} QmwEscalationTarget;

/** reason_code values (non-zero) */
#define QMW_ESCALATION_REASON_OK 0
#define QMW_ESCALATION_REASON_TERMINAL_END 1
#define QMW_ESCALATION_REASON_TERMINAL_HOP_CAP 2
#define QMW_ESCALATION_REASON_TERMINAL_POLICY_CLASSICAL 3
#define QMW_ESCALATION_REASON_TERMINAL_NO_BACKEND 4
#define QMW_ESCALATION_REASON_ERROR_NULL  -1
#define QMW_ESCALATION_REASON_ERROR_POLICY -2

/**
 * Single-step resolution: from current tier, policy, and flags, compute the next tier and backend.
 * Pure (no I/O). Caller chains calls updating current tier to multi-hop.
 */
int qmw_escalation_resolve_next(QmwEscalationTier current_tier, uint32_t flags,
                                const QmwEscalationPolicy* policy, QmwEscalationTarget* out_target);

/** Binary envelope prefix for WLES / payload framing (body follows header). Magic: little-endian 0x01574D51 ("QMW\\x01"). */
typedef struct QmwEscalationEnvelopeHeader {
  uint32_t magic;
  uint16_t format_version;
  uint16_t reserved0;
  uint8_t source_tier;
  uint8_t target_tier;
  uint16_t reserved1;
  uint32_t payload_byte_length;
  uint32_t flags;
} QmwEscalationEnvelopeHeader;

#define QMW_ESCALATION_ENVELOPE_MAGIC 0x01574D51u
#define QMW_ESCALATION_ENVELOPE_FORMAT_VERSION 1u

void qmw_escalation_header_init(QmwEscalationEnvelopeHeader* h, uint8_t source_tier, uint8_t target_tier,
                                uint32_t payload_byte_length, uint32_t flags);
/** Returns 0 on success; non-zero error code written to *out_err if out_err non-null. */
int qmw_escalation_header_validate(const uint8_t* buf, size_t len, uint32_t* out_err);

/**
 * Run native OpenQASM expval only when the last resolved tier is T4 (native sim).
 * err_out: 0 ok, 1 null qasm, 2 parse/exec (from quantum stack), 3 wrong tier, 100 quantum not built.
 */
double qmw_escalation_run_native_openqasm_if_applicable(QmwEscalationTier last_resolved_tier, const char* openqasm_source,
                                                       unsigned long long seed, int* err_out);

#ifdef __cplusplus
}
#endif
