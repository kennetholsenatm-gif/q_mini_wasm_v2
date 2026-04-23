// GF(3) - Galois Field 3 Type System
// Pure ternary arithmetic with no floating points
// 
// This file provides:
// - Trit: Ternary values {-1, 0, +1}
// - TropicalInt: Max-plus algebra integers
// - GF(3) arithmetic operations
// - Vector types for ternary data

#pragma once

#include <cstdint>
#include <vector>
#include <algorithm>
#include <numeric>

namespace q_mini_wasm_v2 {
namespace core {
namespace gf3 {

// ============================================================================
// Ternary Value (Trit)
// ============================================================================

// Ternary enum: Negative=-1, Zero=0, Positive=+1
enum class Trit : int8_t {
    NEGATIVE = -1,
    ZERO = 0,
    POSITIVE = 1
};

// Constants for convenience
constexpr Trit TRIT_NEG = Trit::NEGATIVE;
constexpr Trit TRIT_ZERO = Trit::ZERO;
constexpr Trit TRIT_POS = Trit::POSITIVE;

// Convert integer to Trit (mod 3)
inline constexpr Trit int_to_trit(int value) {
    int mod = ((value % 3) + 3) % 3;  // Ensure positive mod
    return static_cast<Trit>(mod == 2 ? -1 : mod);  // Map 2 -> -1
}

// Convert Trit to integer
inline constexpr int trit_to_int(Trit t) {
    return static_cast<int>(t);
}

// ============================================================================
// GF(3) Arithmetic
// ============================================================================

// Addition in GF(3): (a + b) mod 3 with {-1, 0, 1} representation
inline constexpr Trit add(Trit a, Trit b) {
    int sum = trit_to_int(a) + trit_to_int(b);
    return int_to_trit(sum);
}

// Multiplication in GF(3): {-1, 0, 1} * {-1, 0, 1}
inline constexpr Trit multiply(Trit a, Trit b) {
    return static_cast<Trit>(trit_to_int(a) * trit_to_int(b));
}

// Negation in GF(3): -a (additive inverse)
inline constexpr Trit negate(Trit a) {
    return static_cast<Trit>(-trit_to_int(a));
}

// ============================================================================
// Tropical (Max-Plus) Algebra
// Used for goodness scores and routing decisions
// ============================================================================

using TropicalInt = int32_t;

namespace tropical {
    // Tropical addition: a ⊕ b = max(a, b)
    inline constexpr TropicalInt add(TropicalInt a, TropicalInt b) {
        return std::max(a, b);
    }
    
    // Tropical multiplication: a ⊗ b = a + b
    inline constexpr TropicalInt multiply(TropicalInt a, TropicalInt b) {
        return a + b;
    }
    
    // Tropical "division": a ⊘ b = a - b (for averages)
    inline constexpr TropicalInt divide(TropicalInt a, TropicalInt b) {
        return a - b;
    }
    
    // Tropical zero: -∞ (represented as minimum int for practical purposes)
    constexpr TropicalInt ZERO = -2147483648;  // INT32_MIN
    
    // Tropical one: 0
    constexpr TropicalInt ONE = 0;
    
    // Tropical infinity: +∞ (renamed to avoid conflict with C macro)
    constexpr TropicalInt TROPICAL_INFINITY = 2147483647;  // INT32_MAX
}

// ============================================================================
// Tropical Arithmetic Helpers
// ============================================================================

// Compute tropical sum of a vector (max of all elements)
inline TropicalInt tropical_sum(const std::vector<TropicalInt>& values) {
    if (values.empty()) return tropical::ZERO;
    return std::accumulate(values.begin(), values.end(), tropical::ZERO,
        [](TropicalInt a, TropicalInt b) { return tropical::add(a, b); });
}

// Compute tropical average (tropical division of sum by count)
inline TropicalInt tropical_average(const std::vector<TropicalInt>& values) {
    if (values.empty()) return tropical::ZERO;
    TropicalInt sum = tropical_sum(values);
    return tropical::divide(sum, static_cast<TropicalInt>(values.size()));
}

// ============================================================================
// Ternary Vector Types
// ============================================================================

using TernaryVector = std::vector<Trit>;

// Dot product in GF(3): sum of element-wise products
inline Trit dot_product(const TernaryVector& a, const TernaryVector& b) {
    if (a.size() != b.size()) return TRIT_ZERO;
    
    Trit result = TRIT_ZERO;
    for (size_t i = 0; i < a.size(); ++i) {
        result = add(result, multiply(a[i], b[i]));
    }
    return result;
}

// Tropical dot product (max-plus): max of (a[i] + b[i])
inline TropicalInt tropical_dot_product(const std::vector<TropicalInt>& a, 
                                         const std::vector<TropicalInt>& b) {
    if (a.size() != b.size()) return tropical::ZERO;
    
    TropicalInt result = tropical::ZERO;
    for (size_t i = 0; i < a.size(); ++i) {
        result = tropical::add(result, tropical::multiply(a[i], b[i]));
    }
    return result;
}

// ============================================================================
// Conversion Utilities
// ============================================================================

// Quantize float to Trit (for external data ingestion only)
// This is a ONE-WAY conversion - once in GF(3), stay in GF(3)
inline Trit quantize_to_trit(float value, float threshold = 0.33f) {
    if (value > threshold) return TRIT_POS;
    if (value < -threshold) return TRIT_NEG;
    return TRIT_ZERO;
}

// Convert Trit to activation value (for routing decisions)
inline TropicalInt trit_to_tropical(Trit t) {
    return static_cast<TropicalInt>(trit_to_int(t));
}

// ============================================================================
// Training-Specific Types
// ============================================================================

// Goodness score (tropical integer)
using GoodnessScore = TropicalInt;

// Layer activation (ternary vector)
using LayerActivation = TernaryVector;

// Weight matrix (ternary values only)
using TernaryMatrix = std::vector<TernaryVector>;

// Expert routing score (tropical)
using RoutingScore = TropicalInt;

} // namespace gf3
} // namespace core
} // namespace q_mini_wasm_v2
