#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace qminiwasm::training {

/// Fetch up to ``max_rows`` rows from Hugging Face datasets-server API (HTTPS via ``curl`` in PATH).
/// Encodes each row into ``io_dim`` floats in ``[-1,1]`` (deterministic hash of concatenated string fields).
bool hf_fetch_encoded_rows(const std::string& dataset_id, const std::string& config_name,
                           const std::string& split, const std::string& revision,
                           std::uint32_t max_rows, std::int64_t io_dim, std::uint64_t seed,
                           std::vector<float>* out_row_major, std::string* error_message);

/// Append ``mesh_blend_fraction * base_rows`` synthetic rows (deterministic from seed).
void hf_append_mesh_blend(std::vector<float>* row_major, std::int64_t io_dim, std::uint32_t base_rows,
                          double mesh_blend_fraction, std::uint64_t seed);

}  // namespace qminiwasm::training
