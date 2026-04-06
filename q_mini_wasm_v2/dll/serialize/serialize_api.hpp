#pragma once

#include <cstdint>
#include <cstddef>

#ifdef _WIN32
#ifdef Q_GF3_SERIALIZE_EXPORTS
#define Q_GF3_SERIALIZE_API __declspec(dllexport)
#else
#define Q_GF3_SERIALIZE_API __declspec(dllimport)
#endif
#else
#define Q_GF3_SERIALIZE_API
#endif

extern "C" {

/**
 * GF(3) Serialization and Deserialization DLL Host API
 * Manages packing/unpacking ternary data between host and accelerator
 */

/**
 * Pack array of GF(3) elements into tightly packed 2-bit format
 * Transfers packed data to accelerator device memory
 * Returns pointer to device buffer
 */
Q_GF3_SERIALIZE_API void* PackGF3_to_Device(
    const uint8_t* host_gf3_array,
    size_t element_count
);

/**
 * Unpack 2-bit packed device buffer back to host GF(3) array
 */
Q_GF3_SERIALIZE_API uint32_t UnpackGF3_to_Host(
    void* device_buffer,
    uint8_t* host_gf3_array,
    size_t element_count
);

/**
 * Get required device buffer size for given element count
 */
Q_GF3_SERIALIZE_API size_t GF3_GetPackedBufferSize(size_t element_count);

/**
 * Allocate Unified Shared Memory (USM) buffer for GF(3) data
 */
Q_GF3_SERIALIZE_API void* GF3_AllocateUSMBuffer(size_t element_count);

/**
 * Free device or USM buffer
 */
Q_GF3_SERIALIZE_API void GF3_FreeBuffer(void* buffer);

} // extern "C"