#include "engine_bridge_c_api.h"

#include <cstddef>
#include <cstdint>

extern "C" int qmw_wasmedge_execute_mock(const std::uint8_t* wasm_bytes, std::size_t wasm_len, const char*,
                                          const std::int32_t* args, std::size_t arg_count,
                                          std::int32_t* out_result) {
  if (out_result == nullptr) {
    return -1;
  }
  std::int32_t acc = static_cast<std::int32_t>(wasm_len & 0x7FFFFFFF);
  for (std::size_t i = 0; i < arg_count; ++i) {
    acc += args ? args[i] : 0;
  }
  acc += wasm_bytes != nullptr ? 1 : 0;
  *out_result = acc;
  return 0;
}
