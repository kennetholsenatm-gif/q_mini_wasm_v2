#pragma once



#include <cstddef>

#include <cstdint>

#include <vector>



namespace q_mini_wasm_v2::training_dll {



/**

 * InitSession-only SYCL probe (TritPack5 I/O); implemented in gf3_negative_probe_sycl.cpp

 * (compiled into q_training.dll). Delegates to @c q_mini_wasm_v2::sycl_kernels::gf3_generate_negative_sycl.

 */

bool gf3_training_negative_probe_sycl(

    const std::vector<uint8_t>& positive_packed,

    size_t trit_count,

    uint32_t corruption_seed,

    std::vector<uint8_t>& negative_packed);



} // namespace q_mini_wasm_v2::training_dll

