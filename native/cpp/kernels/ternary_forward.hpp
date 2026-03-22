#pragma once

#include <cstddef>
#include <cstdint>

namespace qminiwasm::kernels {

using MatvecFn = void (*)(const std::uint8_t* packed_weights, std::size_t num_rows, int in_features,
                          const std::int8_t* activations, std::int32_t* out);

/** One row: packed MSB trits × int8 activations, length ``in_features``. */
std::int32_t packed_row_dot_scalar(const std::uint8_t* packed_row, int in_features,
                                   const std::int8_t* activations);

/** Expand one packed row to int8 {-1,0,1} (length ``in_features``). */
void unpack_packed_row_i8(const std::uint8_t* packed_row, int in_features, std::int8_t* out_w);

void matvec_scalar(const std::uint8_t* packed_weights, std::size_t num_rows, int in_features,
                   const std::int8_t* activations, std::int32_t* out);

void matvec_avx2(const std::uint8_t* packed_weights, std::size_t num_rows, int in_features,
                 const std::int8_t* activations, std::int32_t* out);

void matvec_avx512(const std::uint8_t* packed_weights, std::size_t num_rows, int in_features,
                   const std::int8_t* activations, std::int32_t* out);

/** CPUID-dispatched best implementation. */
MatvecFn matvec_dispatch_ptr();

void matvec_best(const std::uint8_t* packed_weights, std::size_t num_rows, int in_features,
                 const std::int8_t* activations, std::int32_t* out);

}  // namespace qminiwasm::kernels
