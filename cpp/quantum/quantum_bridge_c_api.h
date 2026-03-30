#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Run OpenQASM 3 subset on native trinary (qutrit) LibTorch simulator; return <Z> on wire 0.
 * err_out: 0 ok, 1 null input, 2 parse/exec error (see return 0.0).
 */
double qmw_openqasm_expval_pauli_z0(const char* openqasm_source, unsigned long long seed, int* err_out);

/**
 * Static UTF-8 summary of the native quantum stack (OpenQASM 3 subset + trinary LibTorch device).
 * For tooling, telemetry, and operator dashboards; never NULL when linked with QMINIWASM_WITH_QUANTUM.
 */
const char* qmw_native_quantum_stack_summary(void);

#ifdef __cplusplus
}
#endif
