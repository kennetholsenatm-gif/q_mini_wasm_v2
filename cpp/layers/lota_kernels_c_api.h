#pragma once

#include <cstddef>

extern "C" {

void qmw_lota_forward_f32(const float* x, const float* a, const float* b, std::size_t batch, std::size_t in_features,
                          std::size_t rank, std::size_t out_features, float* out);

void qmw_lota_merge_f32(float* base_weight, const float* a, const float* b, std::size_t in_features, std::size_t rank,
                        std::size_t out_features);

void qmw_tsign_update_f32(float* param, const float* grad, std::size_t n, float lr);

}
