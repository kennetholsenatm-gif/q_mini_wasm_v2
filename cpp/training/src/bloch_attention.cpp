#include "qminiwasm/training/bloch_attention.hpp"

namespace qminiwasm::training {
namespace {

torch::Tensor stokes_from_logits(torch::Tensor logits) {
  auto t = torch::tanh(logits);
  auto n = t.norm(2, /*dim=*/-1, /*keepdim=*/true).clamp_min(1e-6f);
  return t / n;
}

}  // namespace

BlochSphereAttentionImpl::BlochSphereAttentionImpl(std::int64_t d_model, int num_heads, std::int64_t d_value,
                                                   bool bias)
    : d_model_(d_model), num_heads_(num_heads) {
  TORCH_CHECK(num_heads > 0, "num_heads must be positive");
  if (d_value < 0) {
    TORCH_CHECK(d_model % num_heads == 0, "d_model must be divisible by num_heads when d_value is implicit");
    d_value_ = d_model / num_heads;
  } else {
    d_value_ = d_value;
  }
  q_proj_ = register_module("q_proj", torch::nn::Linear(
                                           torch::nn::LinearOptions(d_model_, num_heads_ * 3).bias(bias)));
  k_proj_ = register_module("k_proj", torch::nn::Linear(
                                           torch::nn::LinearOptions(d_model_, num_heads_ * 3).bias(bias)));
  v_proj_ = register_module(
      "v_proj", torch::nn::Linear(torch::nn::LinearOptions(d_model_, num_heads_ * d_value_).bias(bias)));
  out_proj_ = register_module(
      "out_proj", torch::nn::Linear(torch::nn::LinearOptions(num_heads_ * d_value_, d_model_).bias(bias)));
}

torch::Tensor BlochSphereAttentionImpl::forward(torch::Tensor x) {
  TORCH_CHECK(x.dim() == 3, "expected [B,T,D], got dim ", x.dim());
  const auto b = x.size(0);
  const auto t = x.size(1);
  const auto h = static_cast<std::int64_t>(num_heads_);
  auto q_logits = q_proj_->forward(x);
  auto k_logits = k_proj_->forward(x);
  auto q = stokes_from_logits(q_logits.view({b, t, h, 3}));
  auto k = stokes_from_logits(k_logits.view({b, t, h, 3}));
  auto v = v_proj_->forward(x).view({b, t, h, d_value_});
  // fidelity [B, T, S, H] then permute to [B, H, T, S]
  auto qe = q.unsqueeze(2);
  auto ke = k.unsqueeze(1);
  auto fid_ts = (1.0 + (qe * ke).sum(-1)) * 0.5;
  auto fid = fid_ts.permute({0, 3, 1, 2}).clamp_min(1e-6);
  auto w = fid / fid.sum(-1, true);
  auto vp = v.permute({0, 2, 1, 3});
  const auto dv = d_value_;
  auto w_flat = w.reshape({-1, t, t});
  auto v_flat = vp.reshape({-1, t, dv});
  auto ctx = torch::bmm(w_flat, v_flat);
  auto ctx_bthd = ctx.reshape({b, h, t, dv}).permute({0, 2, 1, 3}).reshape({b, t, h * dv});
  return out_proj_->forward(ctx_bthd);
}

}  // namespace qminiwasm::training
