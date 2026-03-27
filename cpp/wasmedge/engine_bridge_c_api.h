#pragma once

#include <cstddef>
#include <cstdint>

extern "C" {

/** ABI version for native bridge capability checks. */
int qmw_native_abi_version();

/** WasmEdge execution bridge; returns non-zero when backend is unavailable or execution fails. */
int qmw_wasmedge_execute(const std::uint8_t* wasm_bytes, std::size_t wasm_len, const char* func_name,
                         const std::int32_t* args, std::size_t arg_count, std::int32_t* out_result);

}
