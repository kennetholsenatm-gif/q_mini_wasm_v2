#pragma once

#include <torch/torch.h>

namespace qminiwasm::training {

/// Multi-head Bloch-sphere fidelity attention (Mode A), matching ``qminiwasm.layers.bloch_attention``.
struct BlochSphereAttentionImpl : torch::nn::Module {
  std::int64_t d_model_{};
  int num_heads_{};
  std::int64_t d_value_{};
  torch::nn::Linear q_proj_{nullptr};
  torch::nn::Linear k_proj_{nullptr};
  torch::nn::Linear v_proj_{nullptr};
  torch::nn::Linear out_proj_{nullptr};

  BlochSphereAttentionImpl(std::int64_t d_model, int num_heads, std::int64_t d_value = -1, bool bias = true);

  /// ``x``: ``[B, T, D]`` → ``[B, T, D]``.
  torch::Tensor forward(torch::Tensor x);
};

TORCH_MODULE(BlochSphereAttention);

}  // namespace qminiwasm::training
