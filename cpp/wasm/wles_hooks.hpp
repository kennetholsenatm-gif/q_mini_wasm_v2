#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace qminiwasm::wles {

/** Write raw linear memory (TPEM image or full guest RAM) for WASM Linear Execution Snapshots. */
bool save_linear_memory(const char* path, const std::uint8_t* data, std::size_t len);

/** Load a prior WLES linear-memory blob from disk. */
bool load_linear_memory(const char* path, std::vector<std::uint8_t>& out);

}  // namespace qminiwasm::wles
