#pragma once

namespace q_mini_wasm_v2::core::moe {

/** Logs default SYCL device once when built with USE_SYCL; no-op otherwise. */
void gf3_sycl_probe_log_device();

} // namespace q_mini_wasm_v2::core::moe
