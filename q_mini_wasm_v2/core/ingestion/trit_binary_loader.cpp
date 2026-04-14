#include "trit_binary_loader.hpp"
#include <iostream>
#include <cstring>

namespace q {
namespace ingestion {

void T3BLoader::unpack_trits_legacy(uint8_t packed, int8_t* trits) {
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
    
    // Try to read as v2 first (smaller header)
    char magic[4];
    file_.read(magic, 4);
    file_.seekg(0, std::ios::beg); // Reset
    
    if (std::strncmp(magic, "T3B2", 4) == 0) {
        // v2 format
        T3BHeaderV2 header;
        file_.read(reinterpret_cast<char*>(&header), sizeof(header));
        
        version_ = header.version;
        sample_count_ = header.sample_count;
        trit_dimension_ = header.trit_dimension;
        header_size_ = sizeof(T3BHeaderV2);
        
        // Auto-select packing if AUTO, otherwise use specified
        switch (header.packing_scheme) {
            case 1: packing_scheme_ = ternary::PackingScheme::TRITPACK5; break;
            case 2: packing_scheme_ = ternary::PackingScheme::TRIT20; break;
            default: // AUTO
                packing_scheme_ = ternary::select_packing(trit_dimension_);
                break;
        }
        
        std::cout << "[T3B v2] Loaded: " << filepath 
                  << " (" << sample_count_ << " samples, "
                  << trit_dimension_ << " trits/sample, "
                  << (packing_scheme_ == ternary::PackingScheme::TRITPACK5 ? "T5" : "T20")
                  << " packing)" << std::endl;
    } else {
        // v1 format (legacy)
        T3BHeaderV1 header;
        file_.read(reinterpret_cast<char*>(&header), sizeof(header));
        
        if (std::strncmp(header.magic, "T3B", 3) != 0) {
            std::cerr << "[T3B] Invalid magic header" << std::endl;
            file_.close();
            return false;
        }
        
        if (header.version != 1) {
            std::cerr << "[T3B] Unsupported version: " << header.version << std::endl;
            file_.close();
            return false;
        }
        
        version_ = 1;
        sample_count_ = header.sample_count;
        trit_dimension_ = header.trit_dimension;
        packing_scheme_ = ternary::PackingScheme::LEGACY4;
        header_size_ = sizeof(T3BHeaderV1);
        
        std::cout << "[T3B v1] Loaded: " << filepath 
                  << " (" << sample_count_ << " samples, "
                  << trit_dimension_ << " trits/sample, legacy4)" << std::endl;
    }
    
    // Build offset index for random access
    build_offset_index();
    
    return true;
}

void T3BLoader::build_offset_index() {
    sample_offsets_.resize(sample_count_);
    uint64_t offset = header_size_;
    
    for (uint64_t i = 0; i < sample_count_; ++i) {
        sample_offsets_[i] = offset;
        
        uint32_t length = trit_dimension_;
        
        // v1 has per-sample length prefix
        if (version_ == 1) {
            file_.seekg(offset, std::ios::beg);
            file_.read(reinterpret_cast<char*>(&length), sizeof(length));
            offset += sizeof(length);
        }
        
        // Calculate packed size based on scheme
        size_t packed_size = 0;
        switch (packing_scheme_) {
            case ternary::PackingScheme::TRITPACK5:
                packed_size = (length + ternary::TRITS_PER_BYTE_T5 - 1) / ternary::TRITS_PER_BYTE_T5;
                break;
            case ternary::PackingScheme::TRIT20:
                packed_size = ((length + ternary::TRITS_PER_WORD_T20 - 1) / ternary::TRITS_PER_WORD_T20) * sizeof(uint32_t);
                break;
            default: // LEGACY4
                packed_size = (length + 3) / 4;
                break;
        }
        offset += packed_size;
    }
    
    // Reset to beginning
    file_.seekg(header_size_, std::ios::beg);
}

void T3BLoader::close() {
    if (file_.is_open()) {
        file_.close();
    }
    sample_offsets_.clear();
    version_ = 0;
    sample_count_ = 0;
    trit_dimension_ = 0;
}

bool T3BLoader::read_sample(uint64_t index, std::vector<int8_t>& trits) {
    if (!file_.is_open() || index >= sample_count_) {
        return false;
    }
    
    // Seek to sample
    file_.seekg(sample_offsets_[index], std::ios::beg);
    
    uint32_t length = trit_dimension_;
    
    // v1 has per-sample length prefix
    if (version_ == 1) {
        file_.read(reinterpret_cast<char*>(&length), sizeof(length));
    }
    
    // Read packed data based on scheme
    switch (packing_scheme_) {
        case ternary::PackingScheme::TRITPACK5: {
            size_t packed_size = (length + ternary::TRITS_PER_BYTE_T5 - 1) / ternary::TRITS_PER_BYTE_T5;
            std::vector<uint8_t> packed(packed_size);
            file_.read(reinterpret_cast<char*>(packed.data()), packed_size);
            ternary::unpack_batch_t5(packed, trits, length);
            break;
        }
        case ternary::PackingScheme::TRIT20: {
            size_t packed_words = (length + ternary::TRITS_PER_WORD_T20 - 1) / ternary::TRITS_PER_WORD_T20;
            std::vector<uint32_t> packed(packed_words);
            file_.read(reinterpret_cast<char*>(packed.data()), packed_words * sizeof(uint32_t));
            ternary::unpack_batch_t20(packed, trits, length);
            break;
        }
        default: { // LEGACY4
            size_t packed_size = (length + 3) / 4;
            std::vector<uint8_t> packed(packed_size);
            file_.read(reinterpret_cast<char*>(packed.data()), packed_size);
            trits.resize(length);
            for (size_t j = 0; j < packed_size; ++j) {
                int8_t t[4];
                unpack_trits_legacy(packed[j], t);
                for (int k = 0; k < 4 && (j * 4 + k) < length; ++k) {
                    trits[j * 4 + k] = t[k];
                }
            }
            break;
        }
    }
    
    return true;
}

void T3BLoader::unpack_sample(const uint8_t* packed_data, size_t packed_size, 
                              std::vector<int8_t>& trits, uint32_t length) {
    trits.resize(length);
    
    switch (packing_scheme_) {
        case ternary::PackingScheme::TRITPACK5: {
            std::vector<uint8_t> packed(packed_data, packed_data + packed_size);
            ternary::unpack_batch_t5(packed, trits, length);
            break;
        }
        case ternary::PackingScheme::TRIT20: {
            size_t num_words = packed_size / sizeof(uint32_t);
            std::vector<uint32_t> packed(num_words);
            std::memcpy(packed.data(), packed_data, packed_size);
            ternary::unpack_batch_t20(packed, trits, length);
            break;
        }
        default: { // LEGACY4
            for (size_t j = 0; j < packed_size; ++j) {
                int8_t t[4];
                unpack_trits_legacy(packed_data[j], t);
                for (int k = 0; k < 4 && (j * 4 + k) < length; ++k) {
                    trits[j * 4 + k] = t[k];
                }
            }
            break;
        }
    }
}

} // namespace ingestion
} // namespace q
