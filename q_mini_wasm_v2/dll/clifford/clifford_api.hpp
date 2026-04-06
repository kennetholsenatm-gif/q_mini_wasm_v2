#pragma once

#include <cstdint>
#include <cstddef>

#ifdef _WIN32
#ifdef Q_GF3_CLIFFORD_EXPORTS
#define Q_GF3_CLIFFORD_API __declspec(dllexport)
#else
#define Q_GF3_CLIFFORD_API __declspec(dllimport)
#endif
#else
#define Q_GF3_CLIFFORD_API
#endif

extern "C" {

/**
 * Qutrit Clifford Operations DLL Host API
 * Handles execution of ternary quantum logic circuits
 */

// Opaque event handle for dependency graph tracking
typedef void* CliffordEvent;

/**
 * Enqueue single qutrit gate application (3x3 unitary matrix)
 * Returns event handle that can be waited on or used as dependency
 */
Q_GF3_CLIFFORD_API CliffordEvent Clifford_ApplySingleQutritGate(
    uint32_t qutrit_index,
    const uint8_t unitary_matrix[9],
    CliffordEvent* dependencies,
    size_t dependency_count
);

/**
 * Enqueue two qutrit SUM/CNOT gate application (9x9 matrix)
 */
Q_GF3_CLIFFORD_API CliffordEvent Clifford_ApplyTwoQutritGate(
    uint32_t control_qutrit,
    uint32_t target_qutrit,
    const uint8_t unitary_matrix[81],
    CliffordEvent* dependencies,
    size_t dependency_count
);

/**
 * Enqueue full Clifford circuit sequence
 */
Q_GF3_CLIFFORD_API CliffordEvent Clifford_EnqueueCircuit(
    const uint32_t* gate_sequence,
    size_t gate_count,
    CliffordEvent* dependencies,
    size_t dependency_count
);

/**
 * Wait for event completion (blocking)
 */
Q_GF3_CLIFFORD_API uint32_t Clifford_WaitEvent(CliffordEvent event);

/**
 * Release event handle
 */
Q_GF3_CLIFFORD_API void Clifford_ReleaseEvent(CliffordEvent event);

/**
 * Get state vector pointer on device
 */
Q_GF3_CLIFFORD_API void* Clifford_GetStateVectorDevicePointer(size_t qutrit_count);

} // extern "C"