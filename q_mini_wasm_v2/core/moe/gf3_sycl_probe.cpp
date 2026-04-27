#include "gf3_sycl_probe.hpp"
#include <iostream>

#if defined(USE_SYCL) && USE_SYCL
#include <sycl/sycl.hpp>
#endif

namespace q_mini_wasm_v2::core::moe {

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
        std::cerr << "[TrainingPipeline] SYCL probe failed: " << ex.what() << std::endl;
    } catch (...) {
        std::cerr << "[TrainingPipeline] SYCL probe failed (unknown exception)" << std::endl;
    }
#else
    // MoE GF(3) Forward–Forward still runs on CPU; enable USE_SYCL in CMake when oneAPI/AdaptiveCpp is installed.
#endif
}

} // namespace q_mini_wasm_v2::core::moe
