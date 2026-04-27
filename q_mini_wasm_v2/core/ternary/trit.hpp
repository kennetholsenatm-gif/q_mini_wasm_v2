#pragma once

#include <cstdint>
#include <type_traits>

#include "packing.hpp"

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
 * @brief Ternary energy tracking in GF(3) space
 * 
 * Maps energy consumption to discrete ternary levels for Gottesman-Knill
 * simulability. Energy is quantized to avoid floating-point contamination.
 * 
 * Energy levels: {-1, 0, +1} representing {low, medium, high} energy states
 * Each unit represents ~0.1 pJ for sub-picojoule precision
 */
enum class EnergyTrit : int8_t {
    LOW = -1,      // < 0.3 pJ/op
    MEDIUM = 0,    // 0.3 - 0.7 pJ/op  
    HIGH = 1       // > 0.7 pJ/op
};

/**
 * @brief Convert energy in fixed-point picojoules to ternary energy level
 * @param energy_pj_fixed Energy in fixed-point pJ (scale 1000)
 * @return Ternary energy level
 */
constexpr EnergyTrit energy_to_trit_fixed(int32_t energy_pj_fixed) noexcept {
    if (energy_pj_fixed < 300) return EnergyTrit::LOW;
    if (energy_pj_fixed > 700) return EnergyTrit::HIGH;
    return EnergyTrit::MEDIUM;
}

/**
 * @brief Convert ternary energy level to fixed-point picojoules
 * @param energy_trit Ternary energy level
 * @return Approximate energy in fixed-point pJ (scale 1000)
 */
constexpr int32_t trit_to_energy_fixed(EnergyTrit energy_trit) noexcept {
    switch (energy_trit) {
        case EnergyTrit::LOW: return 200;    // 0.2 pJ/op
        case EnergyTrit::MEDIUM: return 500; // 0.5 pJ/op
        case EnergyTrit::HIGH: return 1000;  // 1.0 pJ/op
    }
    return 500; // Default medium
}

// Backward-compatible aliases for in-flight migration
constexpr EnergyTrit energy_to_trit(int32_t energy_pj_fixed) noexcept {
    return energy_to_trit_fixed(energy_pj_fixed);
}

constexpr int32_t trit_to_energy(EnergyTrit energy_trit) noexcept {
    return trit_to_energy_fixed(energy_trit);
}

/**
 * @brief Ternary probability distribution for ML operations
 * 
 * Replaces floating-point probabilities with GF(3) discrete values
 * Each trit represents probability ranges: {-1: 0-33%, 0: 34-66%, +1: 67-100%}
 */
enum class ProbTrit : int8_t {
    LOW_PROB = -1,   // 0-33%
    MED_PROB = 0,    // 34-66%
    HIGH_PROB = 1    // 67-100%
};

/**
 * @brief Convert probability (0-100) to ternary probability
 * @param prob Percentage probability (0-100)
 * @return Ternary probability
 */
constexpr ProbTrit prob_to_trit(uint32_t prob) noexcept {
    if (prob < 34) return ProbTrit::LOW_PROB;
    if (prob > 66) return ProbTrit::HIGH_PROB;
    return ProbTrit::MED_PROB;
}

/**
 * @brief Convert ternary probability to percentage
 * @param prob_trit Ternary probability
 * @return Representative percentage
 */
constexpr uint32_t trit_to_prob(ProbTrit prob_trit) noexcept {
    switch (prob_trit) {
        case ProbTrit::LOW_PROB: return 17;   // 17% ~ center of 0-33%
        case ProbTrit::MED_PROB: return 50;   // 50% ~ center of 34-66%
        case ProbTrit::HIGH_PROB: return 83;  // 83% ~ center of 67-100%
    }
    return 50;
}

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

    /** Same encoding as @c q::ternary::pack_5trits / @c pack_batch_t5 (base-3 polynomial, 0..242). */
    uint8_t pack() const noexcept {
        int8_t lanes[5];
        for (int i = 0; i < 5; ++i) {
            lanes[i] = static_cast<int8_t>(trits[i]);
        }
        return ::q::ternary::pack_5trits(lanes);
    }

    static TritBlock5 unpack(uint8_t byte) noexcept {
        int8_t lanes[5];
        ::q::ternary::unpack_5trits(byte, lanes);
        TritBlock5 block{};
        for (int i = 0; i < 5; ++i) {
            block.trits[i] = static_cast<Trit>(lanes[i]);
        }
        return block;
    }
};

/**
 * @brief 5-Trit packed structure for memory-efficient storage
 *
 * Same wire format as @c q::ternary::pack_batch_t5 / @c unpack_batch_t5:
 * one byte encodes 5 balanced trits via base-3 polynomial (values 0..242).
 *
 * Used for Betti / QGNN edge storage and anywhere a single-byte 5-trit block is needed.
 */
struct TritPack5 {
    uint8_t packed{};

    void set(size_t idx, Trit value) noexcept {
        if (idx >= 5) {
            return;
        }
        int8_t lanes[5];
        ::q::ternary::unpack_5trits(packed, lanes);
        lanes[idx] = static_cast<int8_t>(value);
        packed = ::q::ternary::pack_5trits(lanes);
    }

    Trit get(size_t idx) const noexcept {
        if (idx >= 5) {
            return Trit::ZERO;
        }
        int8_t lanes[5];
        ::q::ternary::unpack_5trits(packed, lanes);
        return static_cast<Trit>(lanes[idx]);
    }
};

/**
 * @brief Trit arithmetic operations over GF(3)
 */
namespace trit_ops {

template <typename T>
constexpr T multiply(T a) noexcept {
    return a;
}

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

/**
 * @brief Energy trit arithmetic operations
 */
namespace energy_ops {

constexpr EnergyTrit add(EnergyTrit a, EnergyTrit b) noexcept {
    // Energy addition in GF(3) space
    int sum = static_cast<int>(a) + static_cast<int>(b);
    if (sum > 1) return static_cast<EnergyTrit>(sum - 3);
    if (sum < -1) return static_cast<EnergyTrit>(sum + 3);
    return static_cast<EnergyTrit>(sum);
}

constexpr EnergyTrit multiply(EnergyTrit a, EnergyTrit b) noexcept {
    // Energy multiplication in GF(3)
    int prod = static_cast<int>(a) * static_cast<int>(b);
    if (prod > 1) return static_cast<EnergyTrit>(prod - 3);
    if (prod < -1) return static_cast<EnergyTrit>(prod + 3);
    return static_cast<EnergyTrit>(prod);
}

} // namespace energy_ops

/**
 * @brief Probability trit arithmetic operations
 */
namespace prob_ops {

constexpr ProbTrit add(ProbTrit a, ProbTrit b) noexcept {
    // Probability addition in GF(3) space
    int sum = static_cast<int>(a) + static_cast<int>(b);
    if (sum > 1) return static_cast<ProbTrit>(sum - 3);
    if (sum < -1) return static_cast<ProbTrit>(sum + 3);
    return static_cast<ProbTrit>(sum);
}

constexpr ProbTrit multiply(ProbTrit a, ProbTrit b) noexcept {
    // Probability multiplication in GF(3)
    int prod = static_cast<int>(a) * static_cast<int>(b);
    if (prod > 1) return static_cast<ProbTrit>(prod - 3);
    if (prod < -1) return static_cast<ProbTrit>(prod + 3);
    return static_cast<ProbTrit>(prod);
}

} // namespace prob_ops

} // namespace q_mini_wasm_v2::core::ternary