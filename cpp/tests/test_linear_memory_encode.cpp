#include "../wasm/linear_memory_encode.hpp"

#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace {

bool near(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) <= eps; }

}  // namespace

bool test_linear_memory_encode() {
  constexpr int kD = 16;
  constexpr int kMeta = 8;
  constexpr int kBody = kD - kMeta;
  std::vector<float> out(static_cast<std::size_t>(kD));

  const std::uint8_t mem[] = {10, 20};
  qminiwasm::wasm::encode_linear_memory_u8(mem, sizeof(mem), 5, 3, kD, kMeta, out.data());

  if (!near(out[0], 1.0f)) {
    return false;
  }
  if (!near(out[1], 3.0f / 65535.0f)) {
    return false;
  }
  if (!near(out[2], 5.0f / 65535.0f)) {
    return false;
  }
  if (!near(out[3], 2.0f / 16777215.0f)) {
    return false;
  }
  for (int i = 4; i < kMeta; ++i) {
    if (!near(out[static_cast<std::size_t>(i)], 0.0f)) {
      return false;
    }
  }
  if (!near(out[8], 10.0f / 255.0f) || !near(out[9], 20.0f / 255.0f)) {
    return false;
  }
  for (int i = 10; i < kD; ++i) {
    if (!near(out[static_cast<std::size_t>(i)], 0.0f)) {
      return false;
    }
  }

  // Empty memory: normalized length slot zero.
  std::vector<float> out2(static_cast<std::size_t>(kD), 99.0f);
  qminiwasm::wasm::encode_linear_memory_u8(nullptr, 0, 0, 0, kD, kMeta, out2.data());
  if (!near(out2[0], 1.0f) || !near(out2[3], 0.0f)) {
    return false;
  }

  // Truncate to body width (8 bytes kept from 20-byte logical mem).
  std::vector<std::uint8_t> big(20, 7);
  std::vector<float> out3(static_cast<std::size_t>(kD));
  qminiwasm::wasm::encode_linear_memory_u8(big.data(), big.size(), 0, 0, kD, kMeta, out3.data());
  for (int i = 0; i < kBody; ++i) {
    if (!near(out3[static_cast<std::size_t>(kMeta + i)], 7.0f / 255.0f)) {
      return false;
    }
  }

  // INT_MIN normalization (stable vs Python abs).
  std::vector<float> out4(static_cast<std::size_t>(kD));
  qminiwasm::wasm::encode_linear_memory_u8(nullptr, 0, 0, std::numeric_limits<std::int32_t>::min(), kD, kMeta,
                                           out4.data());
  if (!near(out4[1], 0.0f)) {
    return false;
  }

  return true;
}
