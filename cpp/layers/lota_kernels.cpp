#include "lota_kernels_c_api.h"

#include <cstddef>

extern "C" void qmw_lota_forward_f32(const float* x, const float* a, const float* b, std::size_t batch,
                                      std::size_t in_features, std::size_t rank, std::size_t out_features,
                                      float* out) {
  if (x == nullptr || a == nullptr || b == nullptr || out == nullptr) return;
  for (std::size_t bi = 0; bi < batch; ++bi) {
    for (std::size_t o = 0; o < out_features; ++o) {
      float acc = 0.0F;
      for (std::size_t r = 0; r < rank; ++r) {
        float xr = 0.0F;
        for (std::size_t i = 0; i < in_features; ++i) {
          xr += x[bi * in_features + i] * a[r * in_features + i];
        }
        acc += xr * b[o * rank + r];
      }
      out[bi * out_features + o] = acc;
    }
  }
}

extern "C" void qmw_lota_merge_f32(float* base_weight, const float* a, const float* b, std::size_t in_features,
                                    std::size_t rank, std::size_t out_features) {
  if (base_weight == nullptr || a == nullptr || b == nullptr) return;
  for (std::size_t o = 0; o < out_features; ++o) {
    for (std::size_t i = 0; i < in_features; ++i) {
      float delta = 0.0F;
      for (std::size_t r = 0; r < rank; ++r) {
        delta += b[o * rank + r] * a[r * in_features + i];
      }
      base_weight[o * in_features + i] += delta;
    }
  }
}

extern "C" void qmw_tsign_update_f32(float* param, const float* grad, std::size_t n, float lr) {
  if (param == nullptr || grad == nullptr) return;
  for (std::size_t i = 0; i < n; ++i) {
    const float g = grad[i];
    const float s = g > 0.0F ? 1.0F : (g < 0.0F ? -1.0F : 0.0F);
    param[i] += -lr * s;
  }
}
