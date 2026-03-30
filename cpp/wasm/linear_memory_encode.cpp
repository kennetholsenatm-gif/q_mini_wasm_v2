#include "linear_memory_encode.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace qminiwasm::wasm {

namespace {

constexpr float kEncodingVersion = 1.0f;

inline std::int64_t abs_i64(std::int32_t v) {
  const auto x = static_cast<std::int64_t>(v);
  return x >= 0 ? x : -x;
}

inline int abs_mod65536(std::int32_t v) {
  return static_cast<int>(abs_i64(v) % 65536);
}

}  // namespace

void encode_linear_memory_u8(const std::uint8_t* mem, std::size_t len, std::int32_t result_i32,
                             std::int32_t first_arg, int d_model, int meta_slots, float* out) {
  if (out == nullptr) {
    throw std::invalid_argument("encode_linear_memory_u8: out is null");
  }
  if (d_model <= 0) {
    throw std::invalid_argument("encode_linear_memory_u8: d_model must be > 0");
  }
  if (meta_slots < 0 || meta_slots > d_model) {
    throw std::invalid_argument("encode_linear_memory_u8: meta_slots out of range");
  }
  const int body_slots = d_model - meta_slots;
  if (body_slots < 0) {
    throw std::invalid_argument("encode_linear_memory_u8: body_slots negative");
  }

  for (int i = 0; i < d_model; ++i) {
    out[i] = 0.0f;
  }
  out[0] = kEncodingVersion;
  out[1] = static_cast<float>(abs_mod65536(first_arg)) / 65535.0f;
  out[2] = static_cast<float>(abs_mod65536(result_i32)) / 65535.0f;
  constexpr std::size_t kMaxLenNorm = 16777215ULL;
  const std::size_t capped = std::min(len, kMaxLenNorm);
  out[3] = static_cast<float>(capped) / static_cast<float>(kMaxLenNorm);

  if (mem == nullptr && len > 0) {
    throw std::invalid_argument("encode_linear_memory_u8: mem is null but len > 0");
  }
  const int n = static_cast<int>(std::min<std::size_t>(static_cast<std::size_t>(body_slots), len));
  for (int i = 0; i < n; ++i) {
    const auto v = static_cast<std::uint8_t>(mem[static_cast<std::size_t>(i)]);
    out[meta_slots + i] = static_cast<float>(v) / 255.0f;
  }
}

}  // namespace qminiwasm::wasm
