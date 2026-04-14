#pragma once
#include <cstdint>
#include <array>
#include <vector>

namespace q {
namespace ternary {

// Dual trit packing schemes from Q-MINI research
// TritPack5: 5 trits per byte (95% capacity) - optimal for ≤20 trits
// Trit20: 20 trits per 32-bit word (500% vs uncompressed) - optimal for >20 trits

enum class PackingScheme {
    AUTO = 0,       // Auto-select based on trit count
    TRITPACK5 = 1,  // 5 trits per byte
    TRIT20 = 2,     // 20 trits per uint32
    LEGACY4 = 3     // Original 4-trit packing (backward compat)
};

// Lookup table for 3^i (i = 0..20)
constexpr std::array<uint32_t, 21> POW3_TABLE = {{
    1U,           // 3^0
    3U,           // 3^1
    9U,           // 3^2
    27U,          // 3^3
    81U,          // 3^4
    243U,         // 3^5
    729U,         // 3^6
    2187U,        // 3^7
    6561U,        // 3^8
    19683U,       // 3^9
    59049U,       // 3^10
    177147U,      // 3^11
    531441U,      // 3^12
    1594323U,     // 3^13
    4782969U,     // 3^14
    14348907U,    // 3^15
    43046721U,    // 3^16
    129140163U,   // 3^17
    387420489U,   // 3^18
    1162261467U,  // 3^19
    3486784401U   // 3^20 (fits in uint32)
}};

// Constants
constexpr uint32_t TRITS_PER_BYTE_T5 = 5;
constexpr uint32_t TRITS_PER_WORD_T20 = 20;
constexpr uint32_t TRIT20_MAX_VALUE = 3486784401U; // 3^20 - 1

// Balanced trit {-1, 0, +1} ↔ Unbalanced {0, 1, 2} conversion
inline uint8_t trit_to_unbalanced(int8_t trit) {
    return static_cast<uint8_t>(trit + 1); // -1→0, 0→1, +1→2
}

inline int8_t unbalanced_to_trit(uint8_t u) {
    return static_cast<int8_t>(u) - 1; // 0→-1, 1→0, 2→+1
}

// TritPack5: Pack 5 trits into 1 byte
// Each trit uses ~1.58 bits, achieving 95% byte capacity
// trits must point to at least 5 int8_t values
inline uint8_t pack_5trits(const int8_t* trits) {
    uint8_t packed = 0;
    for (int i = 0; i < 5; ++i) {
        uint8_t u = trit_to_unbalanced(trits[i]);
        packed |= (u << (i * 2)); // 2 bits per trit
    }
    return packed;
}

// TritPack5: Unpack 1 byte to 5 trits
// trits must point to at least 5 int8_t values
inline void unpack_5trits(uint8_t packed, int8_t* trits) {
    for (int i = 0; i < 5; ++i) {
        uint8_t u = (packed >> (i * 2)) & 0x3;
        trits[i] = unbalanced_to_trit(u);
    }
}

// Trit20: Pack 20 trits into 1 uint32 (i32)
// Polynomial encoding: Σ(trit_unbalanced[i] × 3^i)
// Maximum value: 3^20 - 1 = 3,486,784,401 (fits in uint32)
inline uint32_t pack_20trits(const int8_t* trits) {
    uint32_t packed = 0;
    for (int i = 0; i < 20; ++i) {
        uint8_t u = trit_to_unbalanced(trits[i]);
        packed += u * POW3_TABLE[i];
    }
    return packed;
}

// Trit20: Unpack 1 uint32 to 20 trits
// Uses successive division/modulo by 3
inline void unpack_20trits(uint32_t packed, int8_t* trits) {
    for (int i = 0; i < 20; ++i) {
        uint8_t u = packed % 3;
        trits[i] = unbalanced_to_trit(u);
        packed /= 3;
    }
}

// Auto-select optimal packing scheme based on trit count
inline PackingScheme select_packing(uint32_t trit_count) {
    if (trit_count <= 20) {
        return PackingScheme::TRITPACK5;  // Byte-aligned, dense
    }
    return PackingScheme::TRIT20;  // Max compression for bulk
}

// Calculate packed size in bytes for given scheme
inline size_t packed_size_bytes(uint32_t trit_count, PackingScheme scheme) {
    switch (scheme) {
        case PackingScheme::TRITPACK5:
            return (trit_count + TRITS_PER_BYTE_T5 - 1) / TRITS_PER_BYTE_T5;
        case PackingScheme::TRIT20:
            return ((trit_count + TRITS_PER_WORD_T20 - 1) / TRITS_PER_WORD_T20) * sizeof(uint32_t);
        case PackingScheme::LEGACY4:
            return (trit_count + 3) / 4;
        default:
            return trit_count; // UNPACKED
    }
}

// Batch pack multiple samples
void pack_batch_t5(const std::vector<int8_t>& trits, std::vector<uint8_t>& packed);
void pack_batch_t20(const std::vector<int8_t>& trits, std::vector<uint32_t>& packed);

// Batch unpack
void unpack_batch_t5(const std::vector<uint8_t>& packed, std::vector<int8_t>& trits, uint32_t trit_count);
void unpack_batch_t20(const std::vector<uint32_t>& packed, std::vector<int8_t>& trits, uint32_t trit_count);

} // namespace ternary
} // namespace q
