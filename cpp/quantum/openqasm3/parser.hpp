#pragma once

#include "ir.hpp"

#include <expected>
#include <optional>
#include <string>
#include <string_view>

namespace qminiwasm::quantum::openqasm3 {

std::expected<CircuitIR, ParseError> parse_openqasm_program(std::string_view source);

}  // namespace qminiwasm::quantum::openqasm3
