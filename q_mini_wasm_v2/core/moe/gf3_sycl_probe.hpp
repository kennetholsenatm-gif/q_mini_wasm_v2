#pragma once

namespace q_mini_wasm_v2::core::moe {

/** Logs default SYCL device once when built with USE_SYCL; no-op otherwise. */
void gf3_sycl_probe_log_device();

/**
 * Returns true if a SYCL queue can be created on a GPU device (gpu_selector_v).
 * On failure, writes a short NUL-terminated message into err_buf (if non-null, cap>0).
 */
bool gf3_sycl_gpu_queue_available(char* err_buf, size_t err_cap) noexcept;

} // namespace q_mini_wasm_v2::core::moe
