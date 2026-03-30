#include "wasm_host_hooks_c_api.h"

#include <cstring>

namespace {

QmwWASMHostCallbacks g_callbacks{};
bool g_have_callbacks = false;

}  // namespace

extern "C" {

int qmw_wasm_hooks_abi_version(void) { return 1; }

void qmw_wasm_hooks_set(const QmwWASMHostCallbacks* callbacks) {
  if (callbacks == nullptr) {
    qmw_wasm_hooks_clear();
    return;
  }
  g_callbacks = *callbacks;
  g_have_callbacks = true;
}

void qmw_wasm_hooks_clear(void) {
  std::memset(&g_callbacks, 0, sizeof(g_callbacks));
  g_have_callbacks = false;
}

void qmw_wasm_hooks_notify_before_execute(const std::uint8_t* wasm_bytes, std::size_t wasm_len,
                                          const char* func_name, const std::int32_t* args,
                                          std::size_t arg_count) {
  if (!g_have_callbacks || g_callbacks.on_before_execute == nullptr) {
    return;
  }
  g_callbacks.on_before_execute(g_callbacks.user_data, wasm_bytes, wasm_len, func_name, args, arg_count);
}

void qmw_wasm_hooks_notify_after_execute(const std::uint8_t* wasm_bytes, std::size_t wasm_len,
                                         const char* func_name, const std::int32_t* args,
                                         std::size_t arg_count, int execute_rc, std::int32_t result) {
  if (!g_have_callbacks || g_callbacks.on_after_execute == nullptr) {
    return;
  }
  g_callbacks.on_after_execute(g_callbacks.user_data, wasm_bytes, wasm_len, func_name, args, arg_count,
                               execute_rc, result);
}

void qmw_wasm_hooks_notify_linear_memory_snapshot(std::uint64_t instance_id, const std::uint8_t* mem,
                                                  std::size_t len, const char* path_or_null) {
  if (!g_have_callbacks || g_callbacks.on_linear_memory_snapshot == nullptr) {
    return;
  }
  g_callbacks.on_linear_memory_snapshot(g_callbacks.user_data, instance_id, mem, len, path_or_null);
}

void qmw_wasm_hooks_notify_route_hint(std::uint64_t instance_id, const double* features, std::size_t dim) {
  if (!g_have_callbacks || g_callbacks.on_route_hint == nullptr) {
    return;
  }
  g_callbacks.on_route_hint(g_callbacks.user_data, instance_id, features, dim);
}

}
