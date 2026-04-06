#pragma once

#include <cstdint>
#include <cstddef>

#ifdef _WIN32
#ifdef Q_GF3_MAXPLUS_EXPORTS
#define Q_GF3_MAXPLUS_API __declspec(dllexport)
#else
#define Q_GF3_MAXPLUS_API __declspec(dllimport)
#endif
#else
#define Q_GF3_MAXPLUS_API
#endif

extern "C" {

/**
 * GF(3) Max-Plus Topologies DLL Host API
 * Graph operations using Max-Plus algebra over ternary field
 */

/**
 * Load adjacency matrix or topology tensor to device
 */
Q_GF3_MAXPLUS_API void* MaxPlus_LoadTopology(
    const int32_t* adjacency_matrix,
    size_t dimension
);

/**
 * Execute Max-Plus General Matrix Multiply (GEMM) operation
 */
Q_GF3_MAXPLUS_API uint32_t MaxPlus_ExecuteGEMM(
    void* topology_handle,
    const int32_t* input_vector,
    int32_t* output_vector,
    size_t vector_length
);

/**
 * Extract steady state from topology
 */
Q_GF3_MAXPLUS_API uint32_t MaxPlus_ExtractSteadyState(
    void* topology_handle,
    int32_t* result_buffer
);

/**
 * Update topology tensor with new weights
 */
Q_GF3_MAXPLUS_API uint32_t MaxPlus_UpdateTopology(
    void* topology_handle,
    const int32_t* updated_weights
);

/**
 * Release topology handle and device resources
 */
Q_GF3_MAXPLUS_API void MaxPlus_ReleaseTopology(void* topology_handle);

} // extern "C"