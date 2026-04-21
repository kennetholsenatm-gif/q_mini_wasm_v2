#include "gf3_polynomial_hash.hpp"
#include <cstring>
#include <unordered_set>

namespace q {
namespace ingestion {

// GF(3) arithmetic lookup tables
// For balanced ternary: states are -1, 0, +1
// Packed representation: 0, 1, 2 (offset by +1)

static const uint8_t GF3_ADD_TABLE[3][3] = {
    {0, 1, 2},  // -1 + {-1, 0, +1} = {-2->+1, -1, 0} = {2, 0, 1} wait...
    {1, 2, 0},  // Actually use: (a + b) % 3
    {2, 0, 1}
};

static const uint8_t GF3_MUL_TABLE[3][3] = {
    {0, 0, 0},  // -1 * anything
    {0, 1, 2},  // 0 * anything
    {0, 2, 1}   // +1 * anything (note: +1 * +1 = +1, +1 * -1 = -1)
};

GF3PolynomialHash::GF3PolynomialHash() : state_(0) {
    reset();
    
    // Initialize Toeplitz matrix rows
    // Each row represents polynomial P(x) shifted by position
    for (int i = 0; i < 64; ++i) {
        for (int j = 0; j < 64; ++j) {
            // P(x) = x^20 + 2x^3 + 1
            // Toeplitz entry T[i][j] = coeff of x^(i+j) in P(x)
            int exp = (i + j) % 21;
            if (exp == 20) {
                toeplitz_rows_[i][j] = 1;  // coeff of x^20
            } else if (exp == 3) {
                toeplitz_rows_[i][j] = 2;  // coeff of 2x^3
            } else if (exp == 0) {
                toeplitz_rows_[i][j] = 1;  // constant term
            } else {
                toeplitz_rows_[i][j] = 0;
            }
        }
    }
    
    recent_hashes_.reserve(BLOOM_SIZE);
    std::memset(&stats_, 0, sizeof(stats_));
}

void GF3PolynomialHash::reset() {
    state_ = 0;
}

uint32_t GF3PolynomialHash::gf3_add(uint32_t a, uint32_t b) {
    // Balanced ternary addition: (a + b) mod 3
    // Input: packed 0,1,2 representing -1,0,+1
    // Output: packed result
    return (a + b) % 3;
}

uint32_t GF3PolynomialHash::gf3_mul(uint32_t a, uint32_t b) {
    // Balanced ternary multiplication
    // Use lookup table for efficiency
    return GF3_MUL_TABLE[a][b];
}

uint32_t GF3PolynomialHash::gf3_mod(uint32_t value) {
    // Branchless modulo 3 using reciprocal multiplication
    // Magic: 341 = ceil(65536 / 243) for 16-bit values
    // But we need mod 3, not mod 243
    
    // For mod 3: use the property that 2^2 ≡ 1 (mod 3)
    // So we can use bit tricks
    uint32_t q = (value >> 2) + (value >> 4) + (value >> 6);
    uint32_t r = value - (q * 3);
    return r > 2 ? r - 3 : r;
}

uint32_t GF3PolynomialHash::reduce_to_20_trits(uint64_t accumulator) {
    // Reduce 64-bit accumulator to 20-trit polynomial
    // Each trit is 2 bits, so 20 trits = 40 bits max
    
    uint32_t result = 0;
    uint64_t temp = accumulator;
    
    // Process in chunks, applying modulo 3 to each trit position
    for (int i = 0; i < 20; ++i) {
        uint32_t trit = temp & 0x3;  // Extract 2 bits
        if (trit > 2) trit = 2;      // Clamp to valid trit (shouldn't happen)
        result |= (trit << (i * 2));
        temp >>= 2;
    }
    
    // Ensure saturation trit (bit 39) is clear in normal operation
    result &= 0x0007FFFF;  // Clear bit 19 (saturation flag)
    
    return result;
}

GF3PolynomialHash::HashState GF3PolynomialHash::update(uint8_t tritpack_byte) {
    // Decode 5 trits from TritPack5 byte
    // Each trit is 2 bits, packed as: t0 | (t1 << 2) | (t2 << 4) | (t3 << 6) | (t4 << 8)
    // But we only have 8 bits, so actually it's base-243 encoding
    
    uint32_t value = tritpack_byte;
    
    // Convert to balanced ternary offset representation
    // TritPack5 maps to values 0-242, offset by +121 for balanced
    int8_t balanced = static_cast<int8_t>(value) - 121;
    
    // Pack balanced trit: -1->2, 0->1, +1->0 (reverse of decoding)
    uint32_t trit_packed = (balanced == 0) ? 1 : (balanced > 0 ? 2 : 0);
    
    // Rolling hash: H' = (H * 3 + trit) mod P(x)
    // In GF(3): multiply by x (shift) and add new coefficient
    
    // Shift current state (multiply by x)
    uint64_t shifted = static_cast<uint64_t>(state_) * 3;
    
    // Add new trit
    shifted += trit_packed;
    
    // Reduce modulo irreducible polynomial P(x) = x^20 + 2x^3 + 1
    // For degree >= 20, reduce using: x^20 ≡ 2x^3 + 1 (mod P)
    while (shifted >= (1ULL << 40)) {  // While degree >= 20
        uint64_t high_bits = shifted >> 40;
        shifted &= ((1ULL << 40) - 1);  // Keep low 40 bits
        
        // Add high_bits * (2x^3 + 1) back
        // 2x^3 term: shift by 3 and multiply by 2
        shifted += (high_bits << 3) * 2;
        // +1 term: add directly
        shifted += high_bits;
    }
    
    state_ = reduce_to_20_trits(shifted);
    
    stats_.bytes_processed++;
    stats_.hashes_computed++;
    
    return state_;
}

GF3PolynomialHash::HashState GF3PolynomialHash::update_block(const uint8_t* data, size_t len) {
    // Process block with vectorized approach
    // Align to 64-byte boundaries for cache efficiency
    
    size_t i = 0;
    
    // Process 64-byte chunks
    for (; i + 64 <= len; i += 64) {
        // SIMD-friendly: process 64 bytes = 320 trits
        for (int j = 0; j < 64; ++j) {
            update(data[i + j]);
        }
    }
    
    // Process remainder
    for (; i < len; ++i) {
        update(data[i]);
    }
    
    return state_;
}

GF3PolynomialHash::HashState GF3PolynomialHash::finalize() const {
    return state_;
}

bool GF3PolynomialHash::is_duplicate(HashState hash) {
    // Simple bloom filter check
    for (const auto& h : recent_hashes_) {
        if (h == hash) {
            stats_.duplicates_detected++;
            return true;
        }
    }
    return false;
}

void GF3PolynomialHash::record_hash(HashState hash) {
    // Ring buffer: evict oldest if full
    if (recent_hashes_.size() >= BLOOM_SIZE) {
        recent_hashes_.erase(recent_hashes_.begin());
    }
    recent_hashes_.push_back(hash);
}

GF3PolynomialHash::Stats GF3PolynomialHash::get_stats() const {
    return stats_;
}

// DatasetDeduplicator implementation

DatasetDeduplicator::DatasetDeduplicator() 
    : total_samples_(0), unique_samples_(0) {
    seen_hashes_.reserve(100000);
}

bool DatasetDeduplicator::process_sample(const std::string& text) {
    total_samples_++;
    
    // Compute hash of text
    hasher_.reset();
    hasher_.update_block(reinterpret_cast<const uint8_t*>(text.data()), text.size());
    auto hash = hasher_.finalize();
    
    // Check for duplicate
    for (const auto& seen : seen_hashes_) {
        if (seen == hash) {
            return true;  // Duplicate
        }
    }
    
    // New unique sample
    seen_hashes_.push_back(hash);
    unique_samples_++;
    return false;
}

size_t DatasetDeduplicator::get_unique_count() const {
    return unique_samples_;
}

double DatasetDeduplicator::get_dedup_ratio() const {
    if (total_samples_ == 0) return 0.0;
    return 1.0 - (static_cast<double>(unique_samples_) / total_samples_);
}

void DatasetDeduplicator::clear() {
    hasher_.reset();
    seen_hashes_.clear();
    total_samples_ = 0;
    unique_samples_ = 0;
}

} // namespace ingestion
} // namespace q
