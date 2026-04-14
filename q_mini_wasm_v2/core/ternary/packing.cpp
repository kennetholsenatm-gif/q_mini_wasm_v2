#include "packing.hpp"
#include <cstring>

namespace q {
namespace ternary {

void pack_batch_t5(const std::vector<int8_t>& trits, std::vector<uint8_t>& packed) {
    const size_t trit_count = trits.size();
    const size_t num_bytes = (trit_count + TRITS_PER_BYTE_T5 - 1) / TRITS_PER_BYTE_T5;
    packed.resize(num_bytes);
    
    size_t trit_idx = 0;
    for (size_t byte_idx = 0; byte_idx < num_bytes; ++byte_idx) {
        int8_t t5[5] = {0, 0, 0, 0, 0};
        for (int i = 0; i < 5 && trit_idx < trit_count; ++i, ++trit_idx) {
            t5[i] = trits[trit_idx];
        }
        packed[byte_idx] = pack_5trits(t5);
    }
}

void pack_batch_t20(const std::vector<int8_t>& trits, std::vector<uint32_t>& packed) {
    const size_t trit_count = trits.size();
    const size_t num_words = (trit_count + TRITS_PER_WORD_T20 - 1) / TRITS_PER_WORD_T20;
    packed.resize(num_words);
    
    size_t trit_idx = 0;
    for (size_t word_idx = 0; word_idx < num_words; ++word_idx) {
        int8_t t20[20];
        for (int i = 0; i < 20; ++i) {
            t20[i] = (trit_idx < trit_count) ? trits[trit_idx] : 0;
            ++trit_idx;
        }
        packed[word_idx] = pack_20trits(t20);
    }
}

void unpack_batch_t5(const std::vector<uint8_t>& packed, std::vector<int8_t>& trits, uint32_t trit_count) {
    trits.resize(trit_count);
    
    size_t trit_idx = 0;
    for (uint8_t byte_val : packed) {
        int8_t t5[5];
        unpack_5trits(byte_val, t5);
        for (int i = 0; i < 5 && trit_idx < trit_count; ++i, ++trit_idx) {
            trits[trit_idx] = t5[i];
        }
    }
}

void unpack_batch_t20(const std::vector<uint32_t>& packed, std::vector<int8_t>& trits, uint32_t trit_count) {
    trits.resize(trit_count);
    
    size_t trit_idx = 0;
    for (uint32_t word_val : packed) {
        int8_t t20[20];
        unpack_20trits(word_val, t20);
        for (int i = 0; i < 20 && trit_idx < trit_count; ++i, ++trit_idx) {
            trits[trit_idx] = t20[i];
        }
    }
}

} // namespace ternary
} // namespace q
