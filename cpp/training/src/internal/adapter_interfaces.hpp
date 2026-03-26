#pragma once

#include <cstddef>

#include "qminiwasm/training/taxonomy_tier.hpp"

namespace qminiwasm::training::adapters {

double run_sycl_compute(std::size_t samples, PrecisionMode precision);
double run_avx512_compute(std::size_t samples, PrecisionMode precision);
double run_wasmedge_step(std::size_t samples, PrecisionMode precision);

}  // namespace qminiwasm::training::adapters
