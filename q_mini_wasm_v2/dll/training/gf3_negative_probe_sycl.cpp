// SYCL contrastive-negative probe for Training_InitSession. This TU is compiled into q_training.dll (not only

// q_mini_wasm_v2_core.lib) so Level Zero always finds the SPIR-V kernel on Windows.



#include "gf3_negative_probe_sycl.hpp"



#if defined(USE_SYCL) && USE_SYCL



#include "sycl/gf3_layers_sycl.hpp"



namespace q_mini_wasm_v2::training_dll {



bool gf3_training_negative_probe_sycl(

    const std::vector<uint8_t>& positive_packed,

    size_t trit_count,

    uint32_t corruption_seed,

    std::vector<uint8_t>& negative_packed) {

    return q_mini_wasm_v2::sycl_kernels::gf3_generate_negative_sycl(

        positive_packed, trit_count, corruption_seed, negative_packed);

}



} // namespace q_mini_wasm_v2::training_dll



#else



#include "gf3_negative_probe_sycl.hpp"



namespace q_mini_wasm_v2::training_dll {



bool gf3_training_negative_probe_sycl(

    const std::vector<uint8_t>&,

    size_t,

    uint32_t,

    std::vector<uint8_t>&) {

    return false;

}



} // namespace q_mini_wasm_v2::training_dll



#endif // USE_SYCL

