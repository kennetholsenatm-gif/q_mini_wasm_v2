#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace qminiwasm::quantum {

/** Parse OpenQASM 3 subset, run on trinary LibTorch simulator; return Pauli-Z expectation on wire 0. */
std::expected<double, std::string> openqasm_expval_pauli_z0(std::string_view openqasm_source, std::uint64_t seed = 1);

}  // namespace qminiwasm::quantum
