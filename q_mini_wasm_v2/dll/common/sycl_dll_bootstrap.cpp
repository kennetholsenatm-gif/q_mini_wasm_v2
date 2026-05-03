// Shared by every GF(3) / training SHARED DLL: SYCL runtime + default device kernel on load.
#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#endif

#include "sycl_dll_bootstrap.hpp"

#include <mutex>

#if defined(USE_SYCL) && USE_SYCL
#include <sycl/sycl.hpp>
#include "../../core/moe/gf3_sycl_probe.hpp"
#endif

namespace q_mini_wasm_v2::dll::common {

void qmini_dll_touch_sycl_device_once() {
#if defined(USE_SYCL) && USE_SYCL
    static std::once_flag once;
    std::call_once(once, [] {
        try {
            // Keep probe lightweight: resolve default queue/device once and log it.
            q_mini_wasm_v2::core::moe::gf3_sycl_probe_log_device();
        } catch (...) {
        }
    });
#endif
}

} // namespace q_mini_wasm_v2::dll::common

#if defined(_WIN32)
// q_training.dll only: install crash/terminate hooks as early as possible (before Training_InitSession).
// Other targets that compile this file do not define QMINI_Q_TRAINING_DLL.
#if defined(QMINI_Q_TRAINING_DLL)
extern "C" void qmini_q_training_install_hooks_on_attach(void);
#endif

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
#if defined(QMINI_Q_TRAINING_DLL)
        qmini_q_training_install_hooks_on_attach();
#endif
    }
    return TRUE;
}
#endif

#if !defined(_WIN32)
__attribute__((constructor)) static void qmini_sycl_dll_ctor() {
    q_mini_wasm_v2::dll::common::qmini_dll_touch_sycl_device_once();
}
#endif
