#pragma once

#include <cstddef>
#include <cstdint>

namespace qminiwasm::wasm {

/** Fixed-width float encoding of WASM linear memory for training (parity with Python ``memory_encode``). */
void encode_linear_memory_u8(const std::uint8_t* mem, std::size_t len, std::int32_t result_i32,
                             std::int32_t first_arg, int d_model, int meta_slots, float* out);

}  // namespace qminiwasm::wasm
