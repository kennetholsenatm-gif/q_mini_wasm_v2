#pragma once

#include <cstddef>
#include <limits>

namespace q_mini_wasm_v2::core::moe {

/// Returns true if `a * b` would overflow `size_t`.
inline bool gf3_ff_mul_overflow_size(size_t a, size_t b, size_t* out_product) noexcept {
    if (out_product == nullptr) {
        return true;
    }
    if (a == 0 || b == 0) {
        *out_product = 0;
        return false;
    }
    if (b > std::numeric_limits<size_t>::max() / a) {
        return true;
    }
    *out_product = a * b;
    return false;
}

/// `rows * cols` as element count; overflow detection for matrix-ish products.
inline bool gf3_ff_mat_cells_overflow_size(size_t rows, size_t cols, size_t* out_cells) noexcept {
    return gf3_ff_mul_overflow_size(rows, cols, out_cells);
}

} // namespace q_mini_wasm_v2::core::moe
