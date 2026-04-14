#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <span>
#include "../../core/ternary/trit.hpp"

namespace q_mini_incus::runtime::namespace_bridge {

/**
 * @brief Ternary-Binary conversion utilities
 * 
 * This is the ONLY component in the system that handles conversion
 * between ternary (GF(3)) and binary representations. All namespace
 * boundary crossings go through here.
 * 
 * Design principles:
 * - Conversion is explicit and logged
 * - Minimize conversion surface area
 * - Preserve ternary semantics where possible
 */

/**
 * @brief Convert ternary data to binary bytes (8-bit)
 * 
 * Uses 5-trit packing (99.06% entropy efficient) per ADR-001
 * Each byte holds 5 trits: byte = t0 + 3*t1 + 9*t2 + 27*t3 + 81*t4
 * 
 * @param trits Input ternary data
 * @return Packed binary representation
 */
std::vector<uint8_t> TernaryToBinary(const std::vector<q_mini_wasm_v2::core::ternary::Trit>& trits);

/**
 * @brief Convert binary bytes to ternary data
 * 
 * Unpacks 5-trit encoding back to ternary values
 * 
 * @param bytes Packed binary data
 * @return Unpacked ternary data (truncated to original trit count if needed)
 */
std::vector<q_mini_wasm_v2::core::ternary::Trit> BinaryToTernary(const std::vector<uint8_t>& bytes,
                                                                  size_t original_trit_count = 0);

/**
 * @brief Pack trits into bytes using 5-trit encoding
 */
std::vector<uint8_t> PackTrits(const std::span<q_mini_wasm_v2::core::ternary::Trit>& trits);

/**
 * @brief Unpack bytes into trits
 */
std::vector<q_mini_wasm_v2::core::ternary::Trit> UnpackBytes(const std::span<uint8_t>& bytes,
                                                               size_t trit_count);

/**
 * @brief Convert network protocol data (binary wire format) to ternary message
 * 
 * Handles protocol framing, checksum validation, etc.
 */
struct NetworkProtocolFrame {
    uint32_t magic;           // Protocol identifier
    uint32_t version;         // Protocol version
    uint32_t payload_length;  // Length in trits (not bytes!)
    uint32_t checksum;        // Simple parity checksum
    std::vector<uint8_t> packed_payload;
};

NetworkProtocolFrame CreateNetworkFrame(
    const std::vector<q_mini_wasm_v2::core::ternary::Trit>& payload,
    uint32_t protocol_version = 1
);

std::vector<q_mini_wasm_v2::core::ternary::Trit> ParseNetworkFrame(const NetworkProtocolFrame& frame);

/**
 * @brief Convert filesystem data to ternary representation
 * 
 * Used for reading/writing configuration files, logs, etc.
 * Preserves text semantics by encoding character-by-character.
 */
std::vector<q_mini_wrt::core::ternary::Trit> TextToTernary(const std::string& text);

std::string TernaryToText(const std::vector<q_mini_wasm_v2::core::ternary::Trit>& trits);

/**
 * @brief Configuration file format for ternary-native container
 * 
 * Simple key-value pairs encoded in ternary:
 * - Keys are strings (ternary encoded)
 * - Values are typed: Trit, TritVector, Number (ternary), String
 */
class TernaryConfigFile {
public:
    bool LoadFromFilesystem(const std::string& path);
    bool SaveToFilesystem(const std::string& path) const;
    
    // Getters return ternary-native values
    q_mini_wasm_v2::core::ternary::Trit GetTrit(const std::string& key,
                                                  q_mini_wasm_v2::core::ternary::Trit default_val) const;
    std::vector<q_mini_wasm_v2::core::ternary::Trit> GetTritVector(const std::string& key) const;
    std::string GetString(const std::string& key, const std::string& default_val = "") const;
    
    // Setters
    void SetTrit(const std::string& key, q_mini_wasm_v2::core::ternary::Trit value);
    void SetTritVector(const std::string& key, const std::vector<q_mini_wasm_v2::core::ternary::Trit>& value);
    void SetString(const std::string& key, const std::string& value);
    
private:
    std::map<std::string, std::vector<q_mini_wasm_v2::core::ternary::Trit>> data_;
};

/**
 * @brief Conversion statistics for monitoring
 */
struct ConversionStats {
    uint64_t ternary_to_binary_ops = 0;
    uint64_t binary_to_ternary_ops = 0;
    uint64_t bytes_converted = 0;
    uint64_t trits_converted = 0;
    uint64_t errors = 0;
};

ConversionStats GetConversionStats();
void ResetConversionStats();

} // namespace q_mini_incus::runtime::namespace_bridge
