#include "ternary_converter.hpp"
#include <iostream>
#include <cstring>

namespace q_mini_incus::runtime::namespace_bridge {

// 5-trit packing: each byte holds 5 trits
// byte = t0 + 3*t1 + 9*t2 + 27*t3 + 81*t4
// where ti is in {0, 1, 2} (GF(3) representation)

using namespace q_mini_wasm_v2::core::ternary;

std::vector<uint8_t> PackTrits(const std::span<Trit>& trits) {
    std::vector<uint8_t> bytes;
    bytes.reserve((trits.size() + 4) / 5);  // Ceiling division
    
    for (size_t i = 0; i < trits.size(); i += 5) {
        uint8_t byte = 0;
        int power = 1;
        for (size_t j = 0; j < 5 && (i + j) < trits.size(); ++j) {
            byte += static_cast<uint8_t>(to_gf3(trits[i + j]) * power);
            power *= 3;
        }
        bytes.push_back(byte);
    }
    
    return bytes;
}

std::vector<Trit> UnpackBytes(const std::span<uint8_t>& bytes, size_t trit_count) {
    std::vector<Trit> trits;
    trits.reserve(trit_count);
    
    for (size_t i = 0; i < bytes.size() && trits.size() < trit_count; ++i) {
        uint8_t val = bytes[i];
        for (int j = 0; j < 5 && trits.size() < trit_count; ++j) {
            trits.push_back(from_gf3(val % 3));
            val /= 3;
        }
    }
    
    return trits;
}

std::vector<uint8_t> TernaryToBinary(const std::vector<Trit>& trits) {
    // Store trit count as header (4 bytes), then packed data
    std::vector<uint8_t> result;
    uint32_t count = static_cast<uint32_t>(trits.size());
    
    // Append count as 4 bytes (little endian)
    result.push_back(count & 0xFF);
    result.push_back((count >> 8) & 0xFF);
    result.push_back((count >> 16) & 0xFF);
    result.push_back((count >> 24) & 0xFF);
    
    // Pack trits
    auto packed = PackTrits(std::span(trits));
    result.insert(result.end(), packed.begin(), packed.end());
    
    return result;
}

std::vector<Trit> BinaryToTernary(const std::vector<uint8_t>& bytes, size_t original_trit_count) {
    if (bytes.size() < 4) {
        return {};
    }
    
    // Read count from header
    uint32_t count = bytes[0] | (bytes[1] << 8) | (bytes[2] << 16) | (bytes[3] << 24);
    if (original_trit_count > 0) {
        count = static_cast<uint32_t>(original_trit_count);
    }
    
    // Unpack remaining bytes
    std::span<uint8_t> packed(bytes.data() + 4, bytes.size() - 4);
    return UnpackBytes(packed, count);
}

// Network protocol frame
NetworkProtocolFrame CreateNetworkFrame(const std::vector<Trit>& payload, uint32_t protocol_version) {
    NetworkProtocolFrame frame;
    frame.magic = 0x514D494E;  // 'QMIN' in ASCII
    frame.version = protocol_version;
    frame.payload_length = static_cast<uint32_t>(payload.size());
    frame.packed_payload = PackTrits(std::span(payload));
    
    // Simple checksum: XOR of all payload bytes
    frame.checksum = 0;
    for (auto b : frame.packed_payload) {
        frame.checksum ^= b;
    }
    
    return frame;
}

std::vector<Trit> ParseNetworkFrame(const NetworkProtocolFrame& frame) {
    // Verify magic
    if (frame.magic != 0x514D494E) {
        return {};
    }
    
    // Verify checksum
    uint32_t calc_checksum = 0;
    for (auto b : frame.packed_payload) {
        calc_checksum ^= b;
    }
    if (calc_checksum != frame.checksum) {
        return {};
    }
    
    // Unpack payload
    std::span<const uint8_t> packed(frame.packed_payload.data(), frame.packed_payload.size());
    return UnpackBytes(packed, frame.payload_length);
}

// Text conversion (character-by-character encoding)
std::vector<Trit> TextToTernary(const std::string& text) {
    std::vector<Trit> trits;
    trits.reserve(text.size() * 5);  // ~5 trits per char with 5-trit packing
    
    // Simple encoding: each char is encoded as its byte value in base-3
    for (char c : text) {
        uint8_t val = static_cast<uint8_t>(c);
        // Convert byte to ~5 trits
        for (int i = 0; i < 5; ++i) {
            trits.push_back(from_gf3(val % 3));
            val /= 3;
        }
    }
    
    return trits;
}

std::string TernaryToText(const std::vector<Trit>& trits) {
    std::string text;
    text.reserve(trits.size() / 5);
    
    for (size_t i = 0; i + 4 < trits.size(); i += 5) {
        uint8_t val = 0;
        int power = 1;
        for (int j = 0; j < 5; ++j) {
            val += static_cast<uint8_t>(to_gf3(trits[i + j]) * power);
            power *= 3;
        }
        text.push_back(static_cast<char>(val));
    }
    
    return text;
}

// Stats tracking
struct ConversionStatsInternal {
    uint64_t ternary_to_binary_ops = 0;
    uint64_t binary_to_ternary_ops = 0;
    uint64_t bytes_converted = 0;
    uint64_t trits_converted = 0;
    uint64_t errors = 0;
};

static ConversionStatsInternal g_stats;

ConversionStats GetConversionStats() {
    ConversionStats stats;
    stats.ternary_to_binary_ops = g_stats.ternary_to_binary_ops;
    stats.binary_to_ternary_ops = g_stats.binary_to_ternary_ops;
    stats.bytes_converted = g_stats.bytes_converted;
    stats.trits_converted = g_stats.trits_converted;
    stats.errors = g_stats.errors;
    return stats;
}

void ResetConversionStats() {
    g_stats = ConversionStatsInternal{};
}

} // namespace q_mini_incus::runtime::namespace_bridge
