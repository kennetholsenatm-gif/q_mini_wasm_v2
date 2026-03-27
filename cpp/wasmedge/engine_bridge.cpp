#include "engine_bridge_c_api.h"

#include <cstddef>
#include <cstdint>

extern "C" int qmw_native_abi_version() { return 1; }

extern "C" int qmw_wasmedge_execute(const std::uint8_t*, std::size_t, const char*, const std::int32_t*,
                                    std::size_t, std::int32_t* out_result) {
  if (out_result == nullptr) {
    return -1;
  }
  // Correctness-first behavior: until real WasmEdge semantics are wired, fail explicitly.
  *out_result = 0;
  return -38;  // ENOSYS-style: native wasm execute not implemented.
}
