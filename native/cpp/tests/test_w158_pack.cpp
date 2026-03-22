#include "../pack/w158_pack.hpp"

#include <vector>

bool test_w158_pack() {
  constexpr int n = 23;
  std::vector<int> w(static_cast<std::size_t>(n));
  for (int i = 0; i < n; ++i) {
    w[static_cast<std::size_t>(i)] = (i % 3) - 1;
  }
  std::vector<std::uint8_t> packed;
  qminiwasm::pack::pack_ternary_msb(w.data(), static_cast<std::size_t>(n), packed);
  std::vector<int> out(static_cast<std::size_t>(n));
  qminiwasm::pack::unpack_ternary_msb(packed.data(), packed.size(), n, out.data());
  for (int i = 0; i < n; ++i) {
    if (out[static_cast<std::size_t>(i)] != w[static_cast<std::size_t>(i)]) {
      return false;
    }
  }
  qminiwasm::pack::W158CacheLineBlock block{};
  if (sizeof(block) % 64 != 0 || alignof(decltype(block)) < 64) {
    return false;
  }
  return true;
}
