#pragma once

#include <cstdint>
#include <type_traits>

namespace q_mini_wasm_v2::core::ternary {

/**
 * @brief Trit type representing the 1.58-bit ternary state space
 * 
 * Based on research: "A Unified QMINIWASM Framework: Bridging Qutrit Stabilizer 
 * Formalisms and Extreme-Edge Ternary AI"
 * 
 * The classical ternary alphabet {-1, 0, +1} exhibits a perfect mathematical 
 * homomorphism with the quantum computational basis of a three-level qutrit system.
 */
enum class Trit : int8_t {
    NEGATIVE = -1,  // |1⟩ quantum state
    ZERO = 0,       // |0⟩ quantum state  
    POSITIVE = 1    // |2⟩ quantum state
};

/**
 * @brief Unbalanced Z3 mapping for arithmetic operations
 * Maps balanced ternary {-1, 0, +1} to GF(3) {0, 1, 2}
 */
constexpr int8_t to_gf3(Trit t) noexcept {
    return static_cast<int8_t>(t) + 1;
}

/**
 * @brief Convert GF(3) value back to Trit
 */
constexpr Trit from_gf3(int8_t val) noexcept {
    return static_cast<Trit>(val - 1);
}

/**
 * @brief Binary Coded Ternary (BCT) representation
 * Each trit is represented by two bits (High, Low)
 * Used for hardware execution via Flash-Cosmos Multi-Wordline Sensing
 */
struct BCT {
    uint8_t high;  // AH
    uint8_t low;   // AL
    
    static constexpr BCT from_trit(Trit t) noexcept {
        switch (t) {
            case Trit::POSITIVE: return {0, 1};
            case Trit::ZERO:     return {0, 0};
            case Trit::NEGATIVE: return {1, 1};
        }
        return {0, 0};
    }
    
    constexpr Trit to_trit() const noexcept {
        if (high == 0 && low == 1) return Trit::POSITIVE;
        if (high == 0 && low == 0) return Trit::ZERO;
        return Trit::NEGATIVE;  // high == 1, low == 1
    }
};

/**
 * @brief 5-trit block for optimal 8-bit packing
 * Achieves 99.06% Shannon entropy efficiency
 * 
 * 5 trits = 3^5 = 243 states
 * 8 bits = 2^8 = 256 states
 * Efficiency = log2(3) * 5 / 8 = 0.9906
 */
struct TritBlock5 {
    Trit trits[5];
    
    /**
     * @brief Pack 5 trits into single byte using base-3 polynomial
     * byte = trit[0] + 3*trit[1] + 9*trit[2] + 27*trit[3] + 81*trit[4]
     */
    uint8_t pack() const noexcept {
        int sum = 0;
        int power = 1;
        for (int i = 0; i < 5; ++i) {
            sum += to_gf3(trits[i]) * power;
            power *= 3;
        }
        return static_cast<uint8_t>(sum);
    }
    
    /**
     * @brief Precomputed lookup table for unpacking bytes into 5 trits
     * Eliminates modulo division operations for maximum WASM performance
     */
    static const TritBlock5 UNPACK_LUT[256];

    /**
     * @brief Unpack byte into 5 trits using precomputed LUT
     */
    static constexpr TritBlock5 unpack(uint8_t byte) noexcept {
        return UNPACK_LUT[byte];
    }
};

/**
 * @brief Trit arithmetic operations over GF(3)
 */
namespace trit_ops {

constexpr Trit add(Trit a, Trit b) noexcept {
    int sum = to_gf3(a) + to_gf3(b);
    return from_gf3(sum % 3);
}

constexpr Trit subtract(Trit a, Trit b) noexcept {
    int diff = to_gf3(a) - to_gf3(b);
    if (diff < 0) diff += 3;
    return from_gf3(diff % 3);
}

constexpr Trit multiply(Trit a, Trit b) noexcept {
    int prod = to_gf3(a) * to_gf3(b);
    return from_gf3(prod % 3);
}

constexpr Trit negate(Trit a) noexcept {
    // In GF(3), -x = (3 - x) % 3
    return from_gf3((3 - to_gf3(a)) % 3);
}

} // namespace trit_ops

} // namespace q_mini_wasm_v2::core::ternary