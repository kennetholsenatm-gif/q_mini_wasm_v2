#include "../internal/adapter_interfaces.hpp"

#include <algorithm>

namespace qminiwasm::training::adapters {

double run_avx512_compute(std::size_t samples, PrecisionMode precision) {
  const double scale = precision == PrecisionMode::kInt4 ? 1.1 : 1.4;
  return std::max(0.02, 1.0 / (static_cast<double>(samples) * 0.0008 * scale + 1.0));
}

}  // namespace qminiwasm::training::adapters
