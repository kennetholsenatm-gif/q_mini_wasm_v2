#include "tpem_bundle_builder_c_api.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

namespace {
constexpr std::array<std::uint8_t, 8> kMagic = {'Q', 'M', 'W', 'T', 'P', 'E', 'M', '1'};
constexpr std::size_t kHeaderSize = 8 + 4 + 4 + 8 + 32;

void append_le32(std::vector<std::uint8_t>& out, std::uint32_t v) {
  out.push_back(static_cast<std::uint8_t>(v & 0xFFu));
  out.push_back(static_cast<std::uint8_t>((v >> 8u) & 0xFFu));
  out.push_back(static_cast<std::uint8_t>((v >> 16u) & 0xFFu));
  out.push_back(static_cast<std::uint8_t>((v >> 24u) & 0xFFu));
}
void append_le64(std::vector<std::uint8_t>& out, std::uint64_t v) {
  for (int i = 0; i < 8; ++i) out.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFFu));
}
}  // namespace

extern "C" std::size_t qmw_tpem_build_bundle(const std::uint8_t* payload, std::size_t payload_len,
                                              std::uint32_t bundle_version, std::uint32_t pack_encoding_version,
                                              std::uint8_t* out_buf, std::size_t out_capacity) {
  std::vector<std::uint8_t> out;
  out.reserve(kHeaderSize + payload_len);
  out.insert(out.end(), kMagic.begin(), kMagic.end());
  append_le32(out, bundle_version);
  append_le32(out, pack_encoding_version);
  append_le64(out, static_cast<std::uint64_t>(payload_len));
  // Foundation: placeholder digest bytes (real SHA verification remains in Python path).
  for (int i = 0; i < 32; ++i) out.push_back(0);
  if (payload != nullptr && payload_len > 0) {
    out.insert(out.end(), payload, payload + payload_len);
  }
  if (out_buf != nullptr && out_capacity >= out.size()) {
    std::memcpy(out_buf, out.data(), out.size());
  }
  return out.size();
}
