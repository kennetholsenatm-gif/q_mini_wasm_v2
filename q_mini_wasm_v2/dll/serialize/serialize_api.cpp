#include "serialize_api.hpp"
#include <cstdlib>
#include <cstring>
#include <vector>
#include <mutex>

extern "C" {

Q_GF3_SERIALIZE_API void* PackGF3_to_Device(
    const uint8_t* host_gf3_array,
    size_t element_count
) {
    if (!host_gf3_array || element_count == 0) return nullptr;
    
    size_t buffer_size = (element_count + 3) / 4;
    uint8_t* buffer = static_cast<uint8_t*>(std::malloc(buffer_size));
    
    if (!buffer) return nullptr;
    
    std::memset(buffer, 0, buffer_size);
    
    for (size_t i = 0; i < element_count; ++i) {
        size_t byte_idx = i / 4;
        size_t shift = (i % 4) * 2;
        buffer[byte_idx] |= (host_gf3_array[i] & 0x03) << shift;
    }
    
    return buffer;
}

Q_GF3_SERIALIZE_API uint32_t UnpackGF3_to_Host(
    void* device_buffer,
    uint8_t* host_gf3_array,
    size_t element_count
) {
    if (!device_buffer || !host_gf3_array || element_count == 0) {
        return 0xFFFFFFFF;
    }
    
    const uint8_t* buffer = static_cast<const uint8_t*>(device_buffer);
    
    for (size_t i = 0; i < element_count; ++i) {
        size_t byte_idx = i / 4;
        size_t shift = (i % 4) * 2;
        host_gf3_array[i] = (buffer[byte_idx] >> shift) & 0x03;
    }
    
    return 0;
}

Q_GF3_SERIALIZE_API size_t GF3_GetPackedBufferSize(size_t element_count) {
    return (element_count + 3) / 4;
}

Q_GF3_SERIALIZE_API void* GF3_AllocateUSMBuffer(size_t element_count) {
    return std::malloc(GF3_GetPackedBufferSize(element_count));
}

Q_GF3_SERIALIZE_API void GF3_FreeBuffer(void* buffer) {
    if (buffer) {
        std::free(buffer);
    }
}

} // extern "C"