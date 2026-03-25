#pragma once

#include <cstdint>

namespace qminiwasm::simd {

struct CpuFeatures {
  bool avx2 = false;
  bool avx512f = false;
  bool avx512vnni = false;
};

/** Query once; safe to call from multiple threads after first init. */
CpuFeatures detect_cpu_features();

}  // namespace qminiwasm::simd
