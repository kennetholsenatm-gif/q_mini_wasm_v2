#pragma once

#include <cstddef>
#include <cstdint>

extern "C" {

/** Foundation stub for wasmedge-native execution bridge. */
int qmw_wasmedge_execute_mock(const std::uint8_t* wasm_bytes, std::size_t wasm_len, const char* func_name,
                              const std::int32_t* args, std::size_t arg_count, std::int32_t* out_result);

}
