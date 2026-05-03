#include "crash_breadcrumb.hpp"

#include <atomic>
#include <cstdio>
#include <cstring>

namespace q_mini_wasm_v2::core::training {
namespace {

alignas(64) char g_buf[2][256]{};
std::atomic<unsigned> g_slot{0};

} // namespace

void qmini_training_breadcrumb(const char* phase) noexcept {
    const char* p = phase ? phase : "";
    const unsigned w = 1u - g_slot.load(std::memory_order_relaxed);
#if defined(_WIN32)
    strncpy_s(g_buf[w], sizeof(g_buf[w]), p, _TRUNCATE);
#else
    std::strncpy(g_buf[w], p, sizeof(g_buf[w]) - 1);
    g_buf[w][sizeof(g_buf[w]) - 1] = '\0';
#endif
    g_slot.store(w, std::memory_order_release);
}

void qmini_training_breadcrumb_tail(char* line, size_t line_cap) noexcept {
    if (!line || line_cap < 32) {
        return;
    }
    const unsigned r = g_slot.load(std::memory_order_acquire);
    if (g_buf[r][0] == '\0') {
        return;
    }
    const size_t used = std::strlen(line);
    if (used + 16 >= line_cap) {
        return;
    }
    char* out = line + used;
    const size_t rem = line_cap - used;
#if defined(_WIN32)
    sprintf_s(out, rem, " bc=[%s]", g_buf[r]);
#else
    std::snprintf(out, rem, " bc=[%s]", g_buf[r]);
#endif
}

} // namespace q_mini_wasm_v2::core::training
