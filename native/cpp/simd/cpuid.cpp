#include "cpuid.hpp"

#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#include <intrin.h>
#endif

namespace qminiwasm::simd {

#if (defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86)))

static void cpuid(int regs[4], int leaf, int subleaf = 0) {
  __cpuidex(regs, leaf, subleaf);
}

CpuFeatures detect_cpu_features() {
  CpuFeatures f{};
  int r[4];
  cpuid(r, 0);
  const int nmax = r[0];
  if (nmax < 7) {
    return f;
  }
  cpuid(r, 1);
  const bool osxsave = (r[2] & (1 << 27)) != 0;
  const bool avx = (r[2] & (1 << 28)) != 0;
  if (osxsave && avx) {
    unsigned long long xcr = _xgetbv(0);
    if ((xcr & 6u) == 6u) {
      f.avx2 = (r[2] & (1 << 5)) != 0;
    }
  }
  cpuid(r, 7, 0);
  if (f.avx2) {
    f.avx512f = (r[1] & (1 << 16)) != 0;
    f.avx512vnni = (r[2] & (1 << 11)) != 0;
  }
  return f;
}

#elif (defined(__GNUC__) || defined(__clang__)) && (defined(__x86_64__) || defined(__i386))

#include <cpuid.h>

CpuFeatures detect_cpu_features() {
  CpuFeatures f{};
  unsigned a = 0, b = 0, c = 0, d = 0;
  if (__get_cpuid_max(0, &a) < 7) {
    return f;
  }
  __cpuid_count(1, 0, a, b, c, d);
  const bool osxsave = (c & (1u << 27)) != 0;
  const bool avx = (c & (1u << 28)) != 0;
  if (osxsave && avx) {
    unsigned eax = 0;
    unsigned edx = 0;
    __asm__ volatile("xgetbv" : "=a"(eax), "=d"(edx) : "c"(0) : "memory");
    const unsigned long long xcr = (static_cast<unsigned long long>(edx) << 32) | eax;
    if ((xcr & 6u) == 6u) {
      f.avx2 = (c & (1u << 5)) != 0;
    }
  }
  __cpuid_count(7, 0, a, b, c, d);
  if (f.avx2) {
    f.avx512f = (b & (1u << 16)) != 0;
    f.avx512vnni = (c & (1u << 11)) != 0;
  }
  return f;
}

#else

CpuFeatures detect_cpu_features() {
  return CpuFeatures{};
}

#endif

}  // namespace qminiwasm::simd
