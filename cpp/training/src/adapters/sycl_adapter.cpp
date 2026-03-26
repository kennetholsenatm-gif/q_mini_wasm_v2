#include "../internal/adapter_interfaces.hpp"

#include <algorithm>

namespace qminiwasm::training::adapters {

double run_sycl_compute(std::size_t samples, PrecisionMode precision) {
  const double scale = precision == PrecisionMode::kFp16 ? 1.0 : 1.2;
  return std::max(0.01, 1.0 / (static_cast<double>(samples) * 0.001 * scale + 1.0));
}

}  // namespace qminiwasm::training::adapters
