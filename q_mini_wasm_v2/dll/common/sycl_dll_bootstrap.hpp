#pragma once

namespace q_mini_wasm_v2::dll::common {

/** SYCL builds: queue default device + tiny kernel + probe log (once per process). No-op without USE_SYCL. */
void qmini_dll_touch_sycl_device_once();

} // namespace q_mini_wasm_v2::dll::common
