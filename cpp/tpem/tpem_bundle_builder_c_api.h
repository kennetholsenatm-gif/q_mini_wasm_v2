#pragma once

#include <cstddef>
#include <cstdint>

extern "C" {

/**
 * Build TPEM bundle bytes: header + payload.
 * If out_buf is null, returns required size.
 */
std::size_t qmw_tpem_build_bundle(const std::uint8_t* payload, std::size_t payload_len, std::uint32_t bundle_version,
                                  std::uint32_t pack_encoding_version, std::uint8_t* out_buf,
                                  std::size_t out_capacity);

}
