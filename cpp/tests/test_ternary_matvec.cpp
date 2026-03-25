#include "../kernels/ternary_forward.hpp"
#include "../pack/w158_pack.hpp"

#include <cstdint>
#include <vector>

bool test_ternary_matvec() {
  const int in_f = 13;
  const int rows = 3;
  std::vector<int> w(static_cast<std::size_t>(in_f * rows), 1);
  std::vector<std::uint8_t> packed;
  for (int r = 0; r < rows; ++r) {
    std::vector<std::uint8_t> row_pack;
    qminiwasm::pack::pack_ternary_msb(w.data() + static_cast<std::size_t>(r * in_f),
                                      static_cast<std::size_t>(in_f), row_pack);
    packed.insert(packed.end(), row_pack.begin(), row_pack.end());
  }
  std::vector<std::int8_t> a(static_cast<std::size_t>(in_f), 1);
  std::vector<std::int32_t> y_scalar(static_cast<std::size_t>(rows));
  std::vector<std::int32_t> y_best(static_cast<std::size_t>(rows));
  qminiwasm::kernels::matvec_scalar(packed.data(), static_cast<std::size_t>(rows), in_f, a.data(),
                                    y_scalar.data());
  qminiwasm::kernels::matvec_best(packed.data(), static_cast<std::size_t>(rows), in_f, a.data(), y_best.data());
  for (std::size_t r = 0; r < static_cast<std::size_t>(rows); ++r) {
    if (y_scalar[r] != y_best[r]) {
      return false;
    }
  }
  return true;
}
