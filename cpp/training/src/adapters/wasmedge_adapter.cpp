#include "../internal/adapter_interfaces.hpp"

#include <algorithm>

namespace qminiwasm::training::adapters {

double run_wasmedge_step(std::size_t samples, PrecisionMode precision) {
  const double penalty = precision == PrecisionMode::kFp16 ? 1.2 : 1.0;
  return std::max(0.02, 1.0 / (static_cast<double>(samples) * 0.0012 * penalty + 1.0));
}

}  // namespace qminiwasm::training::adapters
