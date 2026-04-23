#pragma once

#include <cstdint>
#include <vector>
#include <algorithm>
#include <numeric>
#include <cmath>

namespace q_mini_wasm_v2::core::gf3 {

/**
 * @brief Tropical (Max-Plus) Algebra Operations
 * 
 * Tropical algebra uses:
 * - Addition: max(a, b)  (tropical "addition")
 * - Multiplication: a + b  (tropical "multiplication")
 * - Division: a - b  (tropical "division")
 * 
 * These operations form a semiring over the real numbers extended with -∞.
 * In GF(3) context, we use fixed-point int32_t with scale factor 1000.
 */

// Scale factor for fixed-point arithmetic
constexpr int32_t TROPICAL_SCALE = 1000;

/**
 * @brief Tropical addition: max(a, b)
 */
inline int32_t tropical_add(int32_t a, int32_t b) {
    return (a > b) ? a : b;
}

/**
 * @brief Tropical multiplication: a + b (with overflow protection)
 */
inline int32_t tropical_multiply(int32_t a, int32_t b) {
    int64_t result = static_cast<int64_t>(a) + static_cast<int64_t>(b);
    // Clamp to prevent overflow while preserving tropical structure
    if (result > INT32_MAX) return INT32_MAX;
    if (result < INT32_MIN) return INT32_MIN;
    return static_cast<int32_t>(result);
}

/**
 * @brief Tropical division (inverse of multiplication): a - b
 */
inline int32_t tropical_divide(int32_t a, int32_t b) {
    return a - b;
}

/**
 * @brief Tropical minimum (for min-plus algebra variant)
 */
inline int32_t tropical_min(int32_t a, int32_t b) {
    return (a < b) ? a : b;
}

/**
 * @brief Tropical inner product: max_i(a_i + b_i)
 * 
 * This is the tropical analog of dot product.
 * For routing: measures alignment between input and expert weights.
 */
inline int32_t tropical_inner_product(
    const std::vector<int32_t>& a,
    const std::vector<int32_t>& b
) {
    if (a.size() != b.size() || a.empty()) return INT32_MIN;  // Tropical "zero"
    
    int32_t max_sum = INT32_MIN;
    for (size_t i = 0; i < a.size(); ++i) {
        int32_t sum = tropical_multiply(a[i], b[i]);
        max_sum = tropical_add(max_sum, sum);
    }
    return max_sum;
}

/**
 * @brief Tropical Manhattan distance: sum of absolute differences
 * 
 * This is the natural metric for tropical geometry (L1 metric).
 * Used for measuring distance between expert centroids.
 */
inline int64_t tropical_manhattan_distance(
    const std::vector<int32_t>& a,
    const std::vector<int32_t>& b
) {
    if (a.size() != b.size()) return INT64_MAX;
    
    int64_t sum = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        int64_t diff = static_cast<int64_t>(a[i]) - static_cast<int64_t>(b[i]);
        sum += (diff < 0) ? -diff : diff;
    }
    return sum;
}

/**
 * @brief Tropical norm (L∞): max absolute value
 * 
 * Used for normalization in fixed-point arithmetic.
 */
inline int32_t tropical_norm(const std::vector<int32_t>& v) {
    if (v.empty()) return 0;
    
    int32_t max_abs = 0;
    for (int32_t x : v) {
        int32_t abs_x = (x < 0) ? -x : x;
        if (abs_x > max_abs) max_abs = abs_x;
    }
    return max_abs;
}

/**
 * @brief Normalize vector using tropical norm (scale to TROPICAL_SCALE)
 * 
 * Each element: x * TROPICAL_SCALE / norm
 */
inline std::vector<int32_t> tropical_normalize(
    const std::vector<int32_t>& v
) {
    int32_t norm = tropical_norm(v);
    if (norm == 0) return v;  // Cannot normalize zero vector
    
    std::vector<int32_t> result;
    result.reserve(v.size());
    for (int32_t x : v) {
        // Scale to fixed-point representation
        result.push_back((x * TROPICAL_SCALE) / norm);
    }
    return result;
}

/**
 * @brief Tropical matrix multiplication (max-plus product)
 * 
 * C[i][j] = max_k(A[i][k] + B[k][j])
 * 
 * Used for tropical linear transformations in routing.
 */
inline std::vector<std::vector<int32_t>> tropical_matrix_multiply(
    const std::vector<std::vector<int32_t>>& A,
    const std::vector<std::vector<int32_t>>& B
) {
    size_t m = A.size();
    size_t n = B.empty() ? 0 : B[0].size();
    size_t p = B.size();
    
    if (A.empty() || A[0].size() != p) return {};
    
    std::vector<std::vector<int32_t>> C(m, std::vector<int32_t>(n, INT32_MIN));
    
    for (size_t i = 0; i < m; ++i) {
        for (size_t j = 0; j < n; ++j) {
            int32_t max_val = INT32_MIN;
            for (size_t k = 0; k < p; ++k) {
                int32_t sum = tropical_multiply(A[i][k], B[k][j]);
                max_val = tropical_add(max_val, sum);
            }
            C[i][j] = max_val;
        }
    }
    
    return C;
}

/**
 * @brief Tropical variance (spread measure)
 * 
 * Uses max deviation from tropical mean instead of squared differences.
 * More appropriate for tropical geometry than standard variance.
 */
inline int32_t tropical_variance(
    const std::vector<int32_t>& values
) {
    if (values.empty()) return 0;
    
    // Tropical mean: max value (dominant element)
    int32_t tropical_mean = *std::max_element(values.begin(), values.end());
    
    // Max deviation from tropical mean
    int32_t max_deviation = 0;
    for (int32_t v : values) {
        int32_t deviation = std::abs(v - tropical_mean);
        if (deviation > max_deviation) max_deviation = deviation;
    }
    
    return max_deviation;
}

/**
 * @brief Convert float/double to tropical fixed-point
 */
inline int32_t to_tropical_fixed(double value) {
    return static_cast<int32_t>(value * TROPICAL_SCALE);
}

/**
 * @brief Convert tropical fixed-point to float for display
 */
inline double from_tropical_fixed(int32_t value) {
    return static_cast<double>(value) / TROPICAL_SCALE;
}

} // namespace q_mini_wasm_v2::core::gf3
