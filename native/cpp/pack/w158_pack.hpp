#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace qminiwasm::pack {

inline constexpr int kTritsPerByte = 5;
inline constexpr int kPackEncodingVersion = 2;

inline int signed_to_digit(int w) {
  if (w < 0) return 0;
  if (w == 0) return 1;
  return 2;
}

inline int digit_to_signed(int trit) {
  if (trit == 0) return -1;
  if (trit == 1) return 0;
  return 1;
}

/** MSB-first group packing (matches Python trit_pack.pack_ternary_list). */
void pack_ternary_msb(const int* weights, std::size_t n, std::vector<std::uint8_t>& out);

/** Unpack exactly ``total_trits`` signed ternary values. */
void unpack_ternary_msb(const std::uint8_t* packed, std::size_t packed_len, int total_trits,
                        int* out_weights);

/** Byte size for ``n_trits`` weights after packing. */
std::size_t packed_byte_count(std::size_t n_trits);

/**
 * Cache-line-aligned storage: ``kBlockBytes`` is a multiple of 64 (64 packed bytes = 320 trits).
 */
inline constexpr std::size_t kW158BlockPackedBytes = 64;

struct alignas(64) W158CacheLineBlock {
  std::uint8_t data[kW158BlockPackedBytes]{};
};

static_assert(sizeof(W158CacheLineBlock) % 64 == 0, "W158 block must be cache-line sized");
static_assert(alignof(W158CacheLineBlock) >= 64, "W158 block alignment");

}  // namespace qminiwasm::pack
