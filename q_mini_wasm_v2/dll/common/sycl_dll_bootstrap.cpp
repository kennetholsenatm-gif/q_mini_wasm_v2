// Shared by every GF(3) / training SHARED DLL: SYCL runtime + default device kernel on load.
#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#endif

#include "sycl_dll_bootstrap.hpp"

#include <mutex>

#if defined(USE_SYCL) && USE_SYCL
#include <CL/sycl.hpp>
#include "../../core/moe/gf3_sycl_probe.hpp"
#endif

namespace q_mini_wasm_v2::dll::common {

void qmini_dll_touch_sycl_device_once() {
#if defined(USE_SYCL) && USE_SYCL
    static std::once_flag once;
    std::call_once(once, [] {
        try {
            q_mini_wasm_v2::core::moe::gf3_sycl_probe_log_device();

            sycl::queue q{sycl::default_selector_v};
            sycl::buffer<int32_t, 1> buf{sycl::range<1>(4096)};
            q.submit([&](sycl::handler& h) {
                auto acc = buf.get_access<sycl::access::mode::write>(h);
                h.parallel_for(sycl::range<1>(4096), [=](sycl::id<1> id) {
                    const size_t i = id[0];
                    acc[i] = static_cast<int32_t>(i % 3);
                });
            }).wait();
        } catch (...) {
        }
    });
#endif
}

} // namespace q_mini_wasm_v2::dll::common

#if defined(_WIN32)
BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        q_mini_wasm_v2::dll::common::qmini_dll_touch_sycl_device_once();
    }
    return TRUE;
}
#endif

#if !defined(_WIN32)
__attribute__((constructor)) static void qmini_sycl_dll_ctor() {
    q_mini_wasm_v2::dll::common::qmini_dll_touch_sycl_device_once();
}
#endif
