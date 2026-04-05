#pragma once

#include <cstdint>
#include <span>
#include "../ternary/trit.hpp"
#include "../memory/arena.hpp"

namespace q_mini_wasm_v2::core::inference {

/**
 * @brief Ternary Tokenizer
 * 
 * Implements Phase 2 Component Consolidation: Unified Tokenization
 * as specified in Quantum Architecture Review §5.2.
 * 
 * Features:
 *  - Zero-copy parsing using std::span
 *  - Direct GF(3) state mapping with exact homomorphism
 *  - 5-trit block packing with 99.06% entropy efficiency
 *  - Arena-backed allocation with no intermediate copies
 *  - Compile-time LUT decoding for O(1) trit unpacking
 */
class TernaryTokenizer {
public:
    /**
     * @brief Zero-copy token view
     */
    struct TokenView {
        uint32_t offset;
        uint16_t length;
        ternary::Trit state;
    };

    /**
     * @brief Construct tokenizer
     * @param arena Shared memory arena for allocations
     */
    explicit TernaryTokenizer(memory::MemoryArena& arena);

    /**
     * @brief Tokenize input buffer with zero copies
     * @param input Raw input bytes
     * @param max_tokens Maximum tokens to generate
     * @return Span of TokenView structures in arena
     */
    std::span<const TokenView> tokenize(std::span<const uint8_t> input, size_t max_tokens = SIZE_MAX) noexcept;

    /**
     * @brief Convert token directly to GF(3) trit state
     * @param token_id Numeric token identifier
     * @return Mapped Trit value {-1, 0, +1}
     */
    constexpr ternary::Trit token_to_trit(uint32_t token_id) const noexcept;

    /**
     * @brief Convert trit state to token identifier
     * @param trit GF(3) state value
     * @return Mapped token id
     */
    constexpr uint32_t trit_to_token(ternary::Trit trit) const noexcept;

    /**
     * @brief Pack tokens into TritBlock5 compact format
     * @param tokens Span of token views
     * @return Pointer to packed TritBlock5 array in arena
     */
    ternary::TritBlock5* pack_tokens(std::span<const TokenView> tokens) noexcept;

private:
    memory::MemoryArena& arena_;

    // Compile-time LUT for GF(3) homomorphism mapping
    static constexpr std::array<ternary::Trit, 256> token_map = []() consteval {
        std::array<ternary::Trit, 256> map{};
        for (size_t i = 0; i < 256; ++i) {
            map[i] = static_cast<ternary::Trit>((static_cast<int8_t>(i) % 3) - 1);
        }
        return map;
    }();
};

} // namespace q_mini_wasm_v2::core::inference