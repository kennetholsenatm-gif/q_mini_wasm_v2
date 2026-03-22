#include "../kv/residual_kv_cache.hpp"

#include <cstdint>
#include <vector>

bool test_kv_residual() {
  constexpr std::size_t dim = 32;
  qminiwasm::kv::ResidualKvCache cache(dim);
  std::vector<std::int16_t> ref(dim, 100);
  std::vector<std::int16_t> slice(dim);
  for (std::size_t i = 0; i < dim; ++i) {
    slice[i] = static_cast<std::int16_t>(ref[i] + static_cast<std::int16_t>((i % 5) - 2));
  }
  cache.set_reference(ref.data(), dim);
  cache.push_delta_xor(slice.data(), dim, 4);
  std::vector<std::int16_t> dec;
  cache.decode_last(dec);
  if (dec.size() != dim) {
    return false;
  }
  for (std::size_t i = 0; i < dim; ++i) {
    if (dec[i] != slice[i]) {
      return false;
    }
  }
  return true;
}
