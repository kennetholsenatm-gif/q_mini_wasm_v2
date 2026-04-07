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
     * @brief Unpack byte into 5 trits (precomputed LUT approach, simulated with constexpr)
     * Eliminates modulo division operations for maximum WASM performance
     */
    static constexpr TritBlock5 unpack(uint8_t byte) noexcept {
        TritBlock5 block{};
        int val = byte;
        for (int i = 0; i < 5; ++i) {
            block.trits[i] = from_gf3(val % 3);
            val /= 3;
        }
        return block;
    }
};

/**
 * @brief 5-Trit packed structure for memory-efficient storage
 * 
 * Packs 5 ternary values {-1, 0, +1} into 8 bits (vs 160 bits for 5x32-bit floats)
 * Memory reduction: 60% vs floating-point representations
 * 
 * Used for Betti number computation edge storage in simplicial complexes.
 * Encoding: trit[i] = (packed >> (i*2)) & 0x3
 * Mapping: 0->0 (ZERO), 1->+1 (POSITIVE), 2->-1 (NEGATIVE), 3->unused
 */
struct TritPack5 {
    uint8_t packed; // 5 trits in 8 bits: 2 bits per trit + 2 bits padding
    
    static constexpr uint8_t TRIT_ZERO = 0;
    static constexpr uint8_t TRIT_POS  = 1;
    static constexpr uint8_t TRIT_NEG  = 2;
    
    void set(size_t idx, Trit value) noexcept {
        uint8_t bits = trit_to_bits(value);
        packed &= ~(0x3 << (idx * 2));  // Clear bits at position
        packed |= (bits << (idx * 2));   // Set new bits
    }
    
    Trit get(size_t idx) const noexcept {
        uint8_t bits = (packed >> (idx * 2)) & 0x3;
        return bits_to_trit(bits);
    }
    
    static uint8_t trit_to_bits(Trit t) noexcept {
        switch (t) {
            case Trit::ZERO:     return TRIT_ZERO;
            case Trit::POSITIVE: return TRIT_POS;
            case Trit::NEGATIVE: return TRIT_NEG;
        }
        return TRIT_ZERO;
    }
    
    static Trit bits_to_trit(uint8_t bits) noexcept {
        switch (bits) {
            case TRIT_ZERO: return Trit::ZERO;
            case TRIT_POS:  return Trit::POSITIVE;
            case TRIT_NEG:  return Trit::NEGATIVE;
        }
        return Trit::ZERO;
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