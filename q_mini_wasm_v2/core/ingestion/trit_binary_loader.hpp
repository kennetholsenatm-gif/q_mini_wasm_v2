#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <fstream>
#include <memory>
#include "../ternary/packing.hpp"

namespace q {
namespace ingestion {

// Trit Binary Format (.t3b) Loader
// v1: 4 trits per byte (legacy)
// v2: Auto-select TritPack5 (5 trits/byte) or Trit20 (20 trits/i32)

// Legacy v1 header
struct T3BHeaderV1 {
    char magic[4];           // "T3B\0"
    uint32_t version;        // 1
    uint64_t sample_count;   // Number of samples
    uint32_t trit_dimension; // Trits per sample
};

// Extended v2 header
struct T3BHeaderV2 {
    char magic[4];           // "T3B2"
    uint16_t version;        // 2
    uint8_t packing_scheme;    // 1=T5, 2=T20
    uint8_t reserved;        // 0
    uint32_t sample_count;   // Number of samples
    uint32_t trit_dimension; // Trits per sample
    uint32_t max_trits;      // Max trits per sample
};

struct T3BSample {
    uint32_t length;         // Number of trits
    std::vector<int8_t> trits; // -1, 0, +1 values
};

class T3BLoader {
public:
    T3BLoader() = default;
    ~T3BLoader() { close(); }
    
    // Open and parse header (auto-detects v1 or v2)
    bool open(const std::string& filepath);
    
    // Close file
    void close();
    
    // Get header info
    uint64_t get_sample_count() const { return sample_count_; }
    uint32_t get_trit_dimension() const { return trit_dimension_; }
    ternary::PackingScheme get_packing_scheme() const { return packing_scheme_; }
    int get_version() const { return version_; }
    
    // Read sample by index
    bool read_sample(uint64_t index, std::vector<int8_t>& trits);
    
    // Stream all samples (callback-based for memory efficiency)
    template<typename Callback>
    void stream_samples(Callback&& callback);
    
private:
    std::ifstream file_;
    int version_ = 0;
    uint64_t sample_count_ = 0;
    uint32_t trit_dimension_ = 0;
    ternary::PackingScheme packing_scheme_ = ternary::PackingScheme::LEGACY4;
    std::vector<uint64_t> sample_offsets_; // For random access
    size_t header_size_ = 0;
    
    // Legacy v1 unpack: 4 trits per byte
    static void unpack_trits_legacy(uint8_t packed, int8_t* trits);
    
    // Unpack sample using current scheme
    void unpack_sample(const uint8_t* packed_data, size_t packed_size, 
                       std::vector<int8_t>& trits, uint32_t length);
    
    // Build offset index
    void build_offset_index();
};

template<typename Callback>
void T3BLoader::stream_samples(Callback&& callback) {
    if (!file_.is_open()) return;
    
    // Seek to first sample (after header)
    file_.seekg(header_size_, std::ios::beg);
    
    std::vector<int8_t> trits;
    std::vector<uint8_t> packed_bytes;
    std::vector<uint32_t> packed_words;
    
    for (uint64_t i = 0; i < sample_count_; ++i) {
        uint32_t length = trit_dimension_;
        
        // v1 has per-sample length prefix, v2 uses fixed dimension
        if (version_ == 1) {
            file_.read(reinterpret_cast<char*>(&length), sizeof(length));
        }
        
        // Read packed data
        size_t packed_size = 0;
        switch (packing_scheme_) {
            case ternary::PackingScheme::TRITPACK5:
                packed_size = (length + ternary::TRITS_PER_BYTE_T5 - 1) / ternary::TRITS_PER_BYTE_T5;
                packed_bytes.resize(packed_size);
                file_.read(reinterpret_cast<char*>(packed_bytes.data()), packed_size);
                ternary::unpack_batch_t5(packed_bytes, trits, length);
                break;
                
            case ternary::PackingScheme::TRIT20:
                packed_size = ((length + ternary::TRITS_PER_WORD_T20 - 1) / ternary::TRITS_PER_WORD_T20) * sizeof(uint32_t);
                packed_words.resize(packed_size / sizeof(uint32_t));
                file_.read(reinterpret_cast<char*>(packed_words.data()), packed_size);
                ternary::unpack_batch_t20(packed_words, trits, length);
                break;
                
            case ternary::PackingScheme::LEGACY4:
            default:
                packed_size = (length + 3) / 4;
                packed_bytes.resize(packed_size);
                file_.read(reinterpret_cast<char*>(packed_bytes.data()), packed_size);
                trits.resize(length);
                for (size_t j = 0; j < packed_size; ++j) {
                    int8_t t[4];
                    unpack_trits_legacy(packed_bytes[j], t);
                    for (int k = 0; k < 4 && (j * 4 + k) < length; ++k) {
                        trits[j * 4 + k] = t[k];
                    }
                }
                break;
        }
        
        // Call user callback
        if (!callback(i, trits)) break;
    }
}

} // namespace ingestion
} // namespace q
