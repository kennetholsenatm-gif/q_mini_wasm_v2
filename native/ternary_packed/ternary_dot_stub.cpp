// Stub SYCL host file for packed ternary dot products (oneAPI / DPC++).
// Replace body with real joint_matrix / VNNI lowering when toolchain is available.

#include <cstdint>

#if defined(__INTEL_LLVM_COMPILER) || defined(SYCL_LANGUAGE_VERSION)
#include <sycl/sycl.hpp>

namespace qminiwasm {

inline int dot_digits_scalar_stub(const std::uint8_t* w, const std::int8_t* a,
                                  std::int32_t n, std::int32_t offset_per_lane) {
  int acc = 0;
  for (int i = 0; i < n; ++i) {
    acc += static_cast<int>(w[i]) * static_cast<int>(a[i]) - offset_per_lane;
  }
  return acc;
}

}  // namespace qminiwasm
#else
// Non-SYCL translation unit: keep empty TU for editors without oneAPI.
#endif
