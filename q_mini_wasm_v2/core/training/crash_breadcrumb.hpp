#pragma once

#include <cstddef>

namespace q_mini_wasm_v2::core::training {

/** Last pipeline phase for native crash logs (keep short ASCII; no quotes). */
void qmini_training_breadcrumb(const char* phase) noexcept;

/** Append ` bc=[...]` to @p line if a breadcrumb is set (truncates to fit @p line_cap). */
void qmini_training_breadcrumb_tail(char* line, size_t line_cap) noexcept;

} // namespace q_mini_wasm_v2::core::training
