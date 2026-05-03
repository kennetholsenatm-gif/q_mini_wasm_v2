#include "gf3_sycl_probe.hpp"
#include <cstdio>
#include <iostream>
#include <string_view>

#if defined(USE_SYCL) && USE_SYCL
#include <sycl/sycl.hpp>
#endif

namespace q_mini_wasm_v2::core::moe {

bool gf3_sycl_gpu_queue_available(char* err_buf, size_t err_cap) noexcept {
#if !defined(USE_SYCL) || !USE_SYCL
    if (err_buf && err_cap > 0) {
        err_buf[0] = '\0';
    }
    return false;
#else
    if (err_buf && err_cap > 0) {
        err_buf[0] = '\0';
    }
    try {
        sycl::queue q{sycl::gpu_selector_v};
        const auto dev = q.get_device();
        if (!dev.is_gpu()) {
            if (err_buf && err_cap > 0) {
                std::snprintf(err_buf, err_cap, "%s", "SYCL default queue device is not a GPU");
            }
            return false;
        }
        return true;
    } catch (const std::exception& ex) {
        if (err_buf && err_cap > 0 && ex.what()) {
            std::snprintf(err_buf, err_cap, "%s", ex.what());
        }
        return false;
    } catch (...) {
        if (err_buf && err_cap > 0) {
            std::snprintf(err_buf, err_cap, "%s", "unknown exception creating SYCL GPU queue");
        }
        return false;
    }
#endif
}

void gf3_sycl_probe_log_device() {
#if defined(USE_SYCL) && USE_SYCL
    static bool done = false;
    if (done) {
        return;
    }
    done = true;
    try {
        sycl::queue q{sycl::default_selector_v};
        const auto dev = q.get_device();
        std::cout << "[TrainingPipeline] SYCL default device: "
                  << dev.get_info<sycl::info::device::name>() << std::endl;
    } catch (const std::exception& ex) {
        const std::string_view msg = ex.what() ? std::string_view(ex.what()) : std::string_view{};
        if (msg.find("bad array new length") != std::string_view::npos) {
            std::cerr << "[TrainingPipeline] SYCL probe: skipping device-name query due to runtime quirk; "
                         "SYCL kernels remain eligible."
                      << std::endl;
            return;
        }
        std::cerr << "[TrainingPipeline] SYCL probe failed: " << ex.what() << std::endl;
    } catch (...) {
        std::cerr << "[TrainingPipeline] SYCL probe failed (unknown exception)" << std::endl;
    }
#else
    // MoE GF(3) Forward–Forward uses SYCL when built with USE_SYCL (see gf3_layers_sycl / TryTrainForwardForwardMultiSlot).
#endif
}

} // namespace q_mini_wasm_v2::core::moe
