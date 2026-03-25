#include "ternary_forward.hpp"

#include "../simd/cpuid.hpp"

#include <atomic>

namespace qminiwasm::kernels {

static std::atomic<MatvecFn> g_matvec{nullptr};

static MatvecFn resolve_matvec() {
  const simd::CpuFeatures f = simd::detect_cpu_features();
#if QMINIWASM_HAS_AVX512_TU
  if (f.avx512f) {
    return matvec_avx512;
  }
#endif
  if (f.avx2) {
    return matvec_avx2;
  }
  return matvec_scalar;
}

MatvecFn matvec_dispatch_ptr() {
  MatvecFn p = g_matvec.load(std::memory_order_relaxed);
  if (p == nullptr) {
    p = resolve_matvec();
    g_matvec.store(p, std::memory_order_relaxed);
  }
  return p;
}

void matvec_best(const std::uint8_t* packed_weights, std::size_t num_rows, int in_features,
                 const std::int8_t* activations, std::int32_t* out) {
  matvec_dispatch_ptr()(packed_weights, num_rows, in_features, activations, out);
}

}  // namespace qminiwasm::kernels
