#include "engine_bridge_c_api.h"

#include "../wasm/wasm_host_hooks_c_api.h"

#include <cstddef>
#include <cstdint>

extern "C" int qmw_native_abi_version() { return 1; }

extern "C" int qmw_wasmedge_execute(const std::uint8_t* wasm_bytes, std::size_t wasm_len, const char* func_name,
                                    const std::int32_t* args, std::size_t arg_count, std::int32_t* out_result) {
  qmw_wasm_hooks_notify_before_execute(wasm_bytes, wasm_len, func_name, args, arg_count);
  if (out_result == nullptr) {
    qmw_wasm_hooks_notify_after_execute(wasm_bytes, wasm_len, func_name, args, arg_count, -1, 0);
    return -1;
  }
  // Correctness-first behavior: until real WasmEdge semantics are wired, fail explicitly.
  *out_result = 0;
  const int rc = -38;  // ENOSYS-style: native wasm execute not implemented.
  qmw_wasm_hooks_notify_after_execute(wasm_bytes, wasm_len, func_name, args, arg_count, rc, *out_result);
  return rc;
}
