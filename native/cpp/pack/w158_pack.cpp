#include "w158_pack.hpp"

#include <algorithm>
#include <stdexcept>

namespace qminiwasm::pack {

std::size_t packed_byte_count(std::size_t n_trits) {
  if (n_trits == 0) return 0;
  return (n_trits + kTritsPerByte - 1) / kTritsPerByte;
}

void pack_ternary_msb(const int* weights, std::size_t n, std::vector<std::uint8_t>& out) {
  out.clear();
  std::size_t i = 0;
  while (i < n) {
    const int k = static_cast<int>(std::min<std::size_t>(kTritsPerByte, n - i));
    int byte_val = 0;
    for (int j = 0; j < k; ++j) {
      const int trit = signed_to_digit(weights[i + static_cast<std::size_t>(j)]);
      int power = 1;
      for (int t = 0; t < k - 1 - j; ++t) power *= 3;
      byte_val += trit * power;
    }
    out.push_back(static_cast<std::uint8_t>(byte_val & 0xFF));
    i += static_cast<std::size_t>(k);
  }
}

void unpack_ternary_msb(const std::uint8_t* packed, std::size_t packed_len, int total_trits,
                        int* out_weights) {
  if (total_trits < 0) {
    throw std::invalid_argument("total_trits must be non-negative");
  }
  std::size_t written = 0;
  for (std::size_t bi = 0; bi < packed_len && written < static_cast<std::size_t>(total_trits);
       ++bi) {
    int rem = static_cast<int>(packed[bi]) & 0xFF;
    const int remaining = total_trits - static_cast<int>(written);
    const int k = std::min(kTritsPerByte, remaining);
    for (int j = 0; j < k; ++j) {
      int power = 1;
      for (int t = 0; t < k - 1 - j; ++t) power *= 3;
      const int trit = rem / power;
      rem %= power;
      out_weights[written++] = digit_to_signed(trit);
    }
  }
}

}  // namespace qminiwasm::pack
