#include "trit_binary_loader.hpp"
#include <iostream>
#include <cstring>

namespace q {
namespace ingestion {

void T3BLoader::unpack_trits(uint8_t packed, int8_t* trits) {
    // 00 -> -1, 01 -> 0, 10 -> +1, 11 -> 0 (unused)
    static const int8_t decode[4] = {-1, 0, 1, 0};
    trits[0] = decode[(packed >> 0) & 0b11];
    trits[1] = decode[(packed >> 2) & 0b11];
    trits[2] = decode[(packed >> 4) & 0b11];
    trits[3] = decode[(packed >> 6) & 0b11];
}

bool T3BLoader::open(const std::string& filepath) {
    file_.open(filepath, std::ios::binary);
    if (!file_.is_open()) {
        std::cerr << "[T3B] Failed to open: " << filepath << std::endl;
        return false;
    }
    
    // Read header
    file_.read(reinterpret_cast<char*>(&header_), sizeof(header_));
    
    // Verify magic
    if (std::strncmp(header_.magic, "T3B\0", 4) != 0) {
        std::cerr << "[T3B] Invalid magic header" << std::endl;
        file_.close();
        return false;
    }
    
    // Verify version
    if (header_.version != 1) {
        std::cerr << "[T3B] Unsupported version: " << header_.version << std::endl;
        file_.close();
        return false;
    }
    
    // Build offset index for random access
    sample_offsets_.resize(header_.sample_count);
    uint64_t offset = sizeof(T3BHeader);
    for (uint64_t i = 0; i < header_.sample_count; ++i) {
        sample_offsets_[i] = offset;
        
        // Read length to skip
        uint32_t length;
        file_.seekg(offset, std::ios::beg);
        file_.read(reinterpret_cast<char*>(&length), sizeof(length));
        
        size_t packed_size = (length + 3) / 4;
        offset += sizeof(length) + packed_size;
    }
    
    std::cout << "[T3B] Loaded: " << filepath 
              << " (" << header_.sample_count << " samples, "
              << header_.trit_dimension << " trits/sample)" << std::endl;
    
    return true;
}

void T3BLoader::close() {
    if (file_.is_open()) {
        file_.close();
    }
    sample_offsets_.clear();
}

bool T3BLoader::read_sample(uint64_t index, std::vector<int8_t>& trits) {
    if (!file_.is_open() || index >= header_.sample_count) {
        return false;
    }
    
    // Seek to sample
    file_.seekg(sample_offsets_[index], std::ios::beg);
    
    // Read length
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
    
    return true;
}

} // namespace ingestion
} // namespace q
