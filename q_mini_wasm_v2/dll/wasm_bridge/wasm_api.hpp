#pragma once

#include <cstdint>
#include <cstddef>

#ifdef _WIN32
#ifdef Q_GF3_WASM_EXPORTS
#define Q_GF3_WASM_API __declspec(dllexport)
#else
#define Q_GF3_WASM_API __declspec(dllimport)
#endif
#else
#define Q_GF3_WASM_API
#endif

extern "C" {

/**
 * GF(3) WASM Bridge DLL Host API
 * Connects WebAssembly runtime linear memory to SYCL execution queues
 */

/**
 * Initialize SYCL device and runtime environment
 */
Q_GF3_WASM_API uint32_t Init_SYCL_Device(uint32_t device_index);

/**
 * Teardown SYCL runtime and release all resources
 */
Q_GF3_WASM_API void Teardown_SYCL_Device();

/**
 * Map WASM linear memory offset to SYCL buffer object
 */
Q_GF3_WASM_API void* WASM_MapMemoryOffset(
    uint32_t wasm_memory_offset,
    size_t buffer_size
);

/**
 * Synchronize SYCL execution queue (wait for all pending operations)
 */
Q_GF3_WASM_API uint32_t SYCL_SynchronizeQueue();

/**
 * Dispatch execution command from WASM module
 */
Q_GF3_WASM_API uint32_t SYCL_DispatchCommand(
    uint32_t command_id,
    const void* command_parameters,
    size_t parameter_size
);

/**
 * Get SYCL queue status
 */
Q_GF3_WASM_API uint32_t SYCL_GetQueueStatus();

} // extern "C"