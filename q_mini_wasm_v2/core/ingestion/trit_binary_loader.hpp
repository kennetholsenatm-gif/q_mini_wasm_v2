#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <fstream>
#include <memory>

namespace q {
namespace ingestion {

// Trit Binary Format (.t3b) Loader
// Efficient storage: 4 trits per byte (2 bits each)
// -1 -> 00, 0 -> 01, +1 -> 10

struct T3BHeader {
    char magic[4];           // "T3B\0"
    uint32_t version;        // 1
    uint64_t sample_count;   // Number of samples
    uint32_t trit_dimension; // Trits per sample (usually 256)
};

struct T3BSample {
    uint32_t length;         // Number of trits
    std::vector<int8_t> trits; // -1, 0, +1 values
};

class T3BLoader {
public:
    T3BLoader() = default;
    ~T3BLoader() { close(); }
    
    // Open and parse header
    bool open(const std::string& filepath);
    
    // Close file
    void close();
    
    // Get header info
    uint64_t get_sample_count() const { return header_.sample_count; }
    uint32_t get_trit_dimension() const { return header_.trit_dimension; }
    
    // Read sample by index
    bool read_sample(uint64_t index, std::vector<int8_t>& trits);
    
    // Stream all samples (callback-based for memory efficiency)
    template<typename Callback>
    void stream_samples(Callback&& callback);
    
private:
    std::ifstream file_;
    T3BHeader header_;
    std::vector<uint64_t> sample_offsets_; // For random access
    
    // Unpack 4 trits from a byte
    static void unpack_trits(uint8_t packed, int8_t* trits);
};

template<typename Callback>
void T3BLoader::stream_samples(Callback&& callback) {
    if (!file_.is_open()) return;
    
    // Seek to first sample (after header)
    file_.seekg(sizeof(T3BHeader), std::ios::beg);
    
    std::vector<int8_t> trits;
    for (uint64_t i = 0; i < header_.sample_count; ++i) {
        uint32_t length;
        file_.read(reinterpret_cast<char*>(&length), sizeof(length));
        
        // Read packed data
        size_t packed_size = (length + 3) / 4;
        std::vector<uint8_t> packed(packed_size);
        file_.read(reinterpret_cast<char*>(packed.data()), packed_size);
        
        // Unpack
        trits.resize(length);
        for (size_t j = 0; j < packed_size; ++j) {
            int8_t t[4];
            unpack_trits(packed[j], t);
            for (int k = 0; k < 4 && (j * 4 + k) < length; ++k) {
                trits[j * 4 + k] = t[k];
            }
        }
        
        // Call user callback
        if (!callback(i, trits)) break;
    }
}

} // namespace ingestion
} // namespace q
