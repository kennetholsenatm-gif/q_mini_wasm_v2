#include "maxplus_api.hpp"
#include <vector>
#include <memory>
#include <algorithm>
#include <cstring>

namespace q_mini_wasm_v2::dll::maxplus {

struct TopologyHandle {
    size_t dimension;
    std::vector<int32_t> adjacency_matrix;
};

} // namespace q_mini_wasm_v2::dll::maxplus

extern "C" {

Q_GF3_MAXPLUS_API void* MaxPlus_LoadTopology(
    const int32_t* adjacency_matrix,
    size_t dimension
) {
    using namespace q_mini_wasm_v2::dll::maxplus;
    
    if (!adjacency_matrix || dimension == 0) return nullptr;
    
    auto handle = std::make_unique<TopologyHandle>();
    handle->dimension = dimension;
    handle->adjacency_matrix.resize(dimension * dimension);
    
    std::memcpy(handle->adjacency_matrix.data(), adjacency_matrix, dimension * dimension * sizeof(int32_t));
    
    return handle.release();
}

Q_GF3_MAXPLUS_API uint32_t MaxPlus_ExecuteGEMM(
    void* topology_handle,
    const int32_t* input_vector,
    int32_t* output_vector,
    size_t vector_length
) {
    using namespace q_mini_wasm_v2::dll::maxplus;
    
    if (!topology_handle || !input_vector || !output_vector) {
        return 0xFFFFFFFF;
    }
    
    auto handle = static_cast<TopologyHandle*>(topology_handle);
    
    for (size_t i = 0; i < handle->dimension && i < vector_length; ++i) {
        int32_t max_val = -0x7FFFFFFF;
        
        for (size_t j = 0; j < handle->dimension; ++j) {
            int32_t sum = handle->adjacency_matrix[i * handle->dimension + j] + input_vector[j];
            max_val = std::max(max_val, sum);
        }
        
        output_vector[i] = max_val;
    }
    
    return 0;
}

Q_GF3_MAXPLUS_API uint32_t MaxPlus_ExtractSteadyState(
    void* topology_handle,
    int32_t* result_buffer
) {
    return 0;
}

Q_GF3_MAXPLUS_API uint32_t MaxPlus_UpdateTopology(
    void* topology_handle,
    const int32_t* updated_weights
) {
    using namespace q_mini_wasm_v2::dll::maxplus;
    
    if (!topology_handle || !updated_weights) {
        return 0xFFFFFFFF;
    }
    
    auto handle = static_cast<TopologyHandle*>(topology_handle);
    std::memcpy(handle->adjacency_matrix.data(), updated_weights, handle->dimension * handle->dimension * sizeof(int32_t));
    
    return 0;
}

Q_GF3_MAXPLUS_API void MaxPlus_ReleaseTopology(void* topology_handle) {
    using namespace q_mini_wasm_v2::dll::maxplus;
    
    if (topology_handle) {
        delete static_cast<TopologyHandle*>(topology_handle);
    }
}

} // extern "C"