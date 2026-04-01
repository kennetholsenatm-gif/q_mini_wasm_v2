#include "qminiwasm/training/libtorch_ternary_trainer.hpp"

#include "qminiwasm/training/bloch_attention.hpp"
#include "qminiwasm/training/safetensors_f32.hpp"
#include "qminiwasm/training/tpem_constants.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <torch/torch.h>

namespace qminiwasm::training {
namespace {

void set_err(std::string* error_message, std::string_view text) {
  if (error_message != nullptr) {
    *error_message = std::string(text);
  }
}

/// Appended to interchange/geometry errors so Mission Control and logs point operators at one doc.
constexpr std::string_view kNativeInterchangeHint =
    " Hint: cpp/training/README.md (Checkpoints / interchange v2). Align TOML [model] "
    "attention_backend, native_bloch_seq_len, native_bloch_num_heads with the file envelope "
    "(native_bloch_*) and safetensors keys (bloch.*, ternary_*, input_stem.*, output_head.*).";

void set_err_interchange(std::string* error_message, std::string_view text) {
  if (error_message == nullptr) {
    return;
  }
  error_message->assign(std::string(text));
  error_message->append(kNativeInterchangeHint);
}

std::uint64_t read_u64_le(const unsigned char* p) {
  std::uint64_t v = 0;
  for (int i = 0; i < 8; ++i) {
    v |= static_cast<std::uint64_t>(p[i]) << (8 * i);
  }
  return v;
}

void write_u64_le(unsigned char* p, std::uint64_t v) {
  for (int i = 0; i < 8; ++i) {
    p[i] = static_cast<unsigned char>((v >> (8 * i)) & 0xFF);
  }
}

torch::Tensor ternary_ste(const torch::Tensor& weight) {
  auto abs_mean = weight.abs().mean();
  auto ternary = torch::round(weight / abs_mean).clamp(-1.0, 1.0);
  return (ternary - weight).detach() + weight;
}

struct TernaryExpertImpl : torch::nn::Module {
  TernaryExpertImpl(std::int64_t in_features, std::int64_t out_features) {
    weight_ = register_parameter(
        "weight", torch::empty({out_features, in_features}, torch::dtype(torch::kFloat32).device(torch::kCPU)));
    const double bound = std::sqrt(2.0 / (0.75 * static_cast<double>(in_features)));
    torch::nn::init::uniform_(weight_, -bound, bound);
  }

  torch::Tensor forward(torch::Tensor x) {
    auto w_ste = ternary_ste(weight_);
    return torch::nn::functional::linear(x, w_ste);
  }

  torch::Tensor weight_;
};

TORCH_MODULE(TernaryExpert);

struct CoreModuleImpl : torch::nn::Module {
  std::int64_t d_model_{};
  std::int64_t io_d_model_{};
  int num_blocks_{};
  bool stem_head_{false};
  int bloch_seq_len_{0};
  int bloch_num_heads_{0};
  torch::nn::Linear stem_{nullptr};
  torch::nn::Linear head_{nullptr};
  std::vector<torch::nn::LayerNorm> norms_;
  std::vector<TernaryExpert> experts_;
  torch::Tensor pos_embed_;
  BlochSphereAttention bloch_{nullptr};

  CoreModuleImpl(std::int64_t d_model, std::int64_t io_d_model, int num_blocks, int bloch_seq_len = 0,
                 int bloch_num_heads = 0)
      : d_model_(d_model),
        io_d_model_(io_d_model),
        num_blocks_(num_blocks),
        bloch_seq_len_(bloch_seq_len),
        bloch_num_heads_(bloch_num_heads) {
    TORCH_CHECK(num_blocks >= 1 && num_blocks <= 1024, "num_ternary_blocks must be in [1,1024]");
    stem_head_ = (io_d_model_ != d_model_);
    if (stem_head_) {
      stem_ = register_module("stem",
                              torch::nn::Linear(torch::nn::LinearOptions(io_d_model_, d_model_).bias(true)));
      head_ = register_module("head",
                              torch::nn::Linear(torch::nn::LinearOptions(d_model_, io_d_model_).bias(true)));
      torch::nn::init::kaiming_uniform_(stem_->weight, std::sqrt(5.0));
      torch::nn::init::zeros_(stem_->bias);
      torch::nn::init::kaiming_uniform_(head_->weight, std::sqrt(5.0));
      torch::nn::init::zeros_(head_->bias);
    }
    if (bloch_seq_len_ > 0) {
      TORCH_CHECK(bloch_num_heads_ > 0, "native Bloch: num_heads must be positive when seq_len > 0");
      TORCH_CHECK(d_model_ % bloch_num_heads_ == 0,
                  "native Bloch: d_model must be divisible by num_heads (d_model=", d_model_,
                  ", num_heads=", bloch_num_heads_, ")");
      pos_embed_ = register_parameter(
          "bloch_pos_embed",
          torch::empty({bloch_seq_len_, d_model_}, torch::dtype(torch::kFloat32).device(torch::kCPU)));
      torch::nn::init::normal_(pos_embed_, 0.0, 0.02);
      bloch_ = register_module("bloch", BlochSphereAttention(d_model_, bloch_num_heads_));
    }
    for (int i = 0; i < num_blocks_; ++i) {
      norms_.push_back(register_module(
          "ln" + std::to_string(i),
          torch::nn::LayerNorm(
              torch::nn::LayerNormOptions({d_model_}).elementwise_affine(true).eps(1e-5))));
      experts_.push_back(register_module("tb" + std::to_string(i), TernaryExpert(d_model_, d_model_)));
    }
  }

  std::int64_t d_model() const { return d_model_; }
  std::int64_t io_d_model() const { return io_d_model_; }
  int num_blocks() const { return num_blocks_; }
  bool stem_head() const { return stem_head_; }
  int native_bloch_seq_len() const { return bloch_seq_len_; }
  int native_bloch_num_heads() const { return bloch_num_heads_; }

  torch::Tensor forward(torch::Tensor x) {
    torch::Tensor y = stem_head_ ? stem_->forward(x) : x;
    if (bloch_seq_len_ > 0 && bloch_) {
      const auto b = y.size(0);
      auto pos = pos_embed_.unsqueeze(0).expand({b, bloch_seq_len_, d_model_});
      auto yb = y.unsqueeze(1).expand_as(pos);
      auto seq_in = yb + pos;
      y = bloch_->forward(seq_in).mean(1);
    }
    for (int i = 0; i < num_blocks_; ++i) {
      y = y + experts_[i]->forward(norms_[i]->forward(y));
    }
    return stem_head_ ? head_->forward(y) : y;
  }
};

TORCH_MODULE(CoreModule);

struct CascadeToyPolicyImpl : torch::nn::Module {
  CascadeToyPolicyImpl() {
    l1_ = register_module("l1", torch::nn::Linear(8, 32));
    l2_ = register_module("l2", torch::nn::Linear(32, 4));
    torch::nn::init::kaiming_uniform_(l1_->weight, std::sqrt(5.0));
    torch::nn::init::zeros_(l1_->bias);
    torch::nn::init::kaiming_uniform_(l2_->weight, std::sqrt(5.0));
    torch::nn::init::zeros_(l2_->bias);
  }

  torch::Tensor forward(torch::Tensor x) { return l2_->forward(torch::tanh(l1_->forward(x))); }

  torch::nn::Linear l1_{nullptr};
  torch::nn::Linear l2_{nullptr};
};

TORCH_MODULE(CascadeToyPolicy);

torch::Tensor reconstruct_ptqtp_weight(const torch::Tensor& W, int num_planes) {
  torch::Tensor r = W.detach().clone();
  torch::Tensor acc = torch::zeros_like(r);
  const double eps = 1e-8;
  for (int p = 0; p < num_planes; ++p) {
    auto s = r.abs().mean(1, true).clamp_min(eps);
    auto tern = (r / s).round().clamp(-1.0, 1.0);
    acc = acc + s * tern;
    r = r - s * tern;
  }
  return acc;
}

bool read_file_all(const std::string& path, std::string* out, std::string* error_message) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    set_err(error_message, "failed to open file for read: " + path);
    return false;
  }
  in.seekg(0, std::ios::end);
  const auto sz = in.tellg();
  if (sz < 0) {
    set_err(error_message, "failed to size file: " + path);
    return false;
  }
  in.seekg(0, std::ios::beg);
  out->resize(static_cast<std::size_t>(sz));
  if (!out->empty()) {
    in.read(out->data(), static_cast<std::streamsize>(out->size()));
  }
  if (!in) {
    set_err(error_message, "failed to read file: " + path);
    return false;
  }
  return true;
}

bool write_file_all(const std::string& path, std::string_view data, std::string* error_message) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    set_err(error_message, "failed to open file for write: " + path);
    return false;
  }
  out.write(data.data(), static_cast<std::streamsize>(data.size()));
  out.flush();
  if (!out) {
    set_err(error_message, "failed to write file: " + path);
    return false;
  }
  return true;
}

bool copy_linear_weight_bias(torch::nn::Linear& lin, const torch::Tensor& w,
                             const torch::Tensor* bias_opt, std::string* error_message) {
  auto wt = w.detach().cpu().to(torch::kFloat32).contiguous();
  if (!wt.sizes().equals(lin->weight.sizes())) {
    set_err_interchange(error_message, "linear weight: shape mismatch vs checkpoint");
    return false;
  }
  torch::NoGradGuard guard;
  lin->weight.copy_(wt);
  if (bias_opt != nullptr && lin->bias.defined()) {
    auto bt = bias_opt->detach().cpu().to(torch::kFloat32).contiguous();
    if (bt.sizes().equals(lin->bias.sizes())) {
      lin->bias.copy_(bt);
    }
  }
  return true;
}

}  // namespace

struct LibTorchTpemTrainer::Impl {
  CoreModule core_{nullptr};
  CoreModule teacher_{nullptr};  // Cloned teacher from core (synthetic)
  CoreModule external_teacher_{nullptr};  // External FP32 teacher from checkpoint (real)
  bool has_teacher_ = false;
  bool has_external_teacher_ = false;
  std::unique_ptr<torch::optim::Adam> optim_;
  CascadeToyPolicy cascade_policy_{nullptr};
  std::unique_ptr<torch::optim::Adam> cascade_optim_;
  std::map<std::string, torch::Tensor> frozen_router_;
  double last_lr_ = 1e-3;
  std::uint64_t seed_ = 42;
  double sa_current_temperature_ = 0.0;  /* 0 = not in SA phase */

  Impl(double learning_rate, std::uint64_t seed)
      : last_lr_(learning_rate), seed_(seed) {
    torch::manual_seed(seed_);
    core_ = CoreModule(kTpemDModel, kTpemDModel, 1, 0, 0);
    rebuild_optim();
  }

  void rebuild_optim() {
    optim_ = std::make_unique<torch::optim::Adam>(core_->parameters(),
                                                  torch::optim::AdamOptions(last_lr_));
  }

  void rebuild_core(std::int64_t d_model, std::int64_t io_d_model, int num_blocks, int bloch_seq_len,
                    int bloch_num_heads) {
    torch::manual_seed(seed_);
    core_ = CoreModule(d_model, io_d_model, num_blocks, bloch_seq_len, bloch_num_heads);
    teacher_ = nullptr;
    has_teacher_ = false;
    rebuild_optim();
    frozen_router_.clear();
  }

  void ensure_cascade_policy() {
    if (!cascade_policy_) {
      cascade_policy_ = CascadeToyPolicy();
      rebuild_cascade_optim();
    }
  }

  void rebuild_cascade_optim() {
    if (!cascade_policy_) {
      cascade_optim_.reset();
      return;
    }
    cascade_optim_ = std::make_unique<torch::optim::Adam>(cascade_policy_->parameters(),
                                                          torch::optim::AdamOptions(last_lr_));
  }

  void set_learning_rate(double lr) {
    last_lr_ = lr;
    for (auto& group : optim_->param_groups()) {
      static_cast<torch::optim::AdamOptions&>(group.options()).lr(lr);
    }
    if (cascade_optim_) {
      for (auto& group : cascade_optim_->param_groups()) {
        static_cast<torch::optim::AdamOptions&>(group.options()).lr(lr);
      }
    }
  }

  std::map<std::string, torch::Tensor> tensors_for_save() const {
    std::map<std::string, torch::Tensor> m;
    if (core_->native_bloch_seq_len() > 0) {
      m.emplace("bloch.pos_embed", core_->pos_embed_.detach().cpu().contiguous().to(torch::kFloat32));
      m.emplace("bloch.q_proj.weight", core_->bloch_->q_proj_->weight.detach().cpu().contiguous().to(torch::kFloat32));
      m.emplace("bloch.q_proj.bias", core_->bloch_->q_proj_->bias.detach().cpu().contiguous().to(torch::kFloat32));
      m.emplace("bloch.k_proj.weight", core_->bloch_->k_proj_->weight.detach().cpu().contiguous().to(torch::kFloat32));
      m.emplace("bloch.k_proj.bias", core_->bloch_->k_proj_->bias.detach().cpu().contiguous().to(torch::kFloat32));
      m.emplace("bloch.v_proj.weight", core_->bloch_->v_proj_->weight.detach().cpu().contiguous().to(torch::kFloat32));
      m.emplace("bloch.v_proj.bias", core_->bloch_->v_proj_->bias.detach().cpu().contiguous().to(torch::kFloat32));
      m.emplace("bloch.out_proj.weight",
                core_->bloch_->out_proj_->weight.detach().cpu().contiguous().to(torch::kFloat32));
      m.emplace("bloch.out_proj.bias", core_->bloch_->out_proj_->bias.detach().cpu().contiguous().to(torch::kFloat32));
    }
    if (core_->stem_head()) {
      m.emplace("input_stem.weight", core_->stem_->weight.detach().cpu().contiguous().to(torch::kFloat32));
      m.emplace("input_stem.bias", core_->stem_->bias.detach().cpu().contiguous().to(torch::kFloat32));
      m.emplace("output_head.weight", core_->head_->weight.detach().cpu().contiguous().to(torch::kFloat32));
      m.emplace("output_head.bias", core_->head_->bias.detach().cpu().contiguous().to(torch::kFloat32));
    }
    const int nb = core_->num_blocks();
    for (int i = 0; i < nb; ++i) {
      const std::string key =
          (nb == 1) ? std::string("ternary_expert.weight")
                    : ("ternary_blocks." + std::to_string(i) + ".weight");
      m.emplace(key, core_->experts_[static_cast<std::size_t>(i)]->weight_.detach().cpu().contiguous().to(
                          torch::kFloat32));
    }
    for (const auto& [k, v] : frozen_router_) {
      m.emplace("quantum_router." + k, v.detach().cpu().contiguous().to(torch::kFloat32));
    }
    return m;
  }

  bool load_flat_tensors(const std::unordered_map<std::string, torch::Tensor>& flat,
                         std::string* error_message) {
    constexpr const char* qr_prefix = "quantum_router.";
    frozen_router_.clear();
    for (const auto& [full, tensor] : flat) {
      if (full.rfind(qr_prefix, 0) == 0) {
        const std::string sub = full.substr(std::char_traits<char>::length(qr_prefix));
        frozen_router_[sub] = tensor.detach().cpu().to(torch::kFloat32).contiguous();
      }
    }

    if (core_->stem_head()) {
      auto it_w = flat.find("input_stem.weight");
      if (it_w == flat.end()) {
        set_err_interchange(error_message, "missing input_stem.weight (io_d_model != d_model in envelope)");
        return false;
      }
      auto it_b = flat.find("input_stem.bias");
      const torch::Tensor* pb = (it_b != flat.end()) ? &it_b->second : nullptr;
      if (!copy_linear_weight_bias(core_->stem_, it_w->second, pb, error_message)) {
        return false;
      }
      auto ow = flat.find("output_head.weight");
      if (ow == flat.end()) {
        set_err_interchange(error_message, "missing output_head.weight");
        return false;
      }
      auto ob = flat.find("output_head.bias");
      const torch::Tensor* pob = (ob != flat.end()) ? &ob->second : nullptr;
      if (!copy_linear_weight_bias(core_->head_, ow->second, pob, error_message)) {
        return false;
      }
    }

    const int nb = core_->num_blocks();
    for (int i = 0; i < nb; ++i) {
      std::string wkey =
          (nb == 1) ? std::string("ternary_expert.weight")
                    : ("ternary_blocks." + std::to_string(i) + ".weight");
      auto it = flat.find(wkey);
      if (it == flat.end() && nb == 1) {
        it = flat.find("ternary_blocks.0.weight");
      }
      if (it == flat.end()) {
        set_err_interchange(error_message, "missing " + wkey + " in interchange payload");
        return false;
      }
      auto t = it->second.detach().cpu().to(torch::kFloat32).contiguous();
      auto& ex = core_->experts_[static_cast<std::size_t>(i)];
      if (t.dim() != 2 || t.size(0) != core_->d_model() || t.size(1) != core_->d_model()) {
        set_err_interchange(error_message, "ternary block weight: bad shape vs d_model");
        return false;
      }
      torch::NoGradGuard guard;
      ex->weight_.copy_(t);
    }

    if (core_->native_bloch_seq_len() > 0 && core_->bloch_) {
      auto itp = flat.find("bloch.pos_embed");
      if (itp != flat.end()) {
        auto te = itp->second.detach().cpu().to(torch::kFloat32).contiguous();
        if (!te.sizes().equals(core_->pos_embed_.sizes())) {
          set_err_interchange(error_message, "bloch.pos_embed: shape mismatch vs native core");
          return false;
        }
        torch::NoGradGuard g;
        core_->pos_embed_.copy_(te);
      }
      auto load_lin = [&](std::string_view base, torch::nn::Linear& lin) -> bool {
        const std::string wkey = std::string(base) + ".weight";
        auto iw = flat.find(wkey);
        if (iw == flat.end()) {
          return true;
        }
        const torch::Tensor* pb = nullptr;
        const std::string bkey = std::string(base) + ".bias";
        auto ib = flat.find(bkey);
        if (ib != flat.end()) {
          pb = &ib->second;
        }
        return copy_linear_weight_bias(lin, iw->second, pb, error_message);
      };
      if (!load_lin("bloch.q_proj", core_->bloch_->q_proj_)) {
        return false;
      }
      if (!load_lin("bloch.k_proj", core_->bloch_->k_proj_)) {
        return false;
      }
      if (!load_lin("bloch.v_proj", core_->bloch_->v_proj_)) {
        return false;
      }
      if (!load_lin("bloch.out_proj", core_->bloch_->out_proj_)) {
        return false;
      }
    }

    rebuild_optim();
    return true;
  }

  double train_step(std::size_t batch_size, std::uint64_t step_mix) {
    const auto b = static_cast<std::int64_t>(std::max<std::size_t>(1, batch_size));
    torch::manual_seed(step_mix);
    const std::int64_t io = core_->io_d_model();
    auto x = torch::randn({b, io}, torch::dtype(torch::kFloat32));
    auto target = torch::randn({b, io}, torch::dtype(torch::kFloat32));
    core_->train();
    optim_->zero_grad();
    auto out = core_->forward(x);
    auto loss = torch::mse_loss(out, target);
    loss.backward();
    optim_->step();
    return loss.item<double>();
  }

  double eval_step(std::size_t batch_size, std::uint64_t step_mix) {
    const auto b = static_cast<std::int64_t>(std::max<std::size_t>(1, batch_size));
    torch::manual_seed(step_mix + 0x9E3779B97F4A7C15ULL);
    const std::int64_t io = core_->io_d_model();
    auto x = torch::randn({b, io}, torch::dtype(torch::kFloat32));
    auto target = torch::randn({b, io}, torch::dtype(torch::kFloat32));
    core_->eval();
    torch::NoGradGuard guard;
    auto out = core_->forward(x);
    return torch::mse_loss(out, target).item<double>();
  }

  double train_step_supervised(std::size_t batch_size, std::int64_t io_dim, const float* x_rm,
                               const float* t_rm, std::uint64_t step_mix) {
    (void)step_mix;
    const auto b = static_cast<std::int64_t>(std::max<std::size_t>(1, batch_size));
    auto x = torch::from_blob(const_cast<float*>(x_rm), {b, io_dim}, torch::TensorOptions().dtype(torch::kFloat32))
                 .clone();
    auto tgt = torch::from_blob(const_cast<float*>(t_rm), {b, io_dim}, torch::TensorOptions().dtype(torch::kFloat32))
                   .clone();
    core_->train();
    optim_->zero_grad();
    auto out = core_->forward(x);
    auto loss = torch::mse_loss(out, tgt);
    loss.backward();
    optim_->step();
    return loss.item<double>();
  }

  double eval_step_supervised(std::size_t batch_size, std::int64_t io_dim, const float* x_rm, const float* t_rm,
                              std::uint64_t step_mix) {
    (void)step_mix;
    const auto b = static_cast<std::int64_t>(std::max<std::size_t>(1, batch_size));
    auto x = torch::from_blob(const_cast<float*>(x_rm), {b, io_dim}, torch::TensorOptions().dtype(torch::kFloat32))
                 .clone();
    auto tgt = torch::from_blob(const_cast<float*>(t_rm), {b, io_dim}, torch::TensorOptions().dtype(torch::kFloat32))
                   .clone();
    core_->eval();
    torch::NoGradGuard guard;
    auto out = core_->forward(x);
    return torch::mse_loss(out, tgt).item<double>();
  }

  bool clone_teacher_from_core(std::string* error_message) {
    (void)error_message;
    const std::int64_t d = core_->d_model();
    const std::int64_t io = core_->io_d_model();
    const int nb = core_->num_blocks();
    teacher_ = CoreModule(d, io, nb, core_->native_bloch_seq_len(), core_->native_bloch_num_heads());
    torch::NoGradGuard g;
    const auto ps = core_->parameters();
    const auto pt = teacher_->parameters();
    if (ps.size() != pt.size()) {
      set_err(error_message, "teacher clone: parameter count mismatch");
      return false;
    }
    for (std::size_t i = 0; i < ps.size(); ++i) {
      pt[i].copy_(ps[i]);
      pt[i].set_requires_grad(false);
    }
    teacher_->eval();
    has_teacher_ = true;
    return true;
  }

  void reset_teacher() {
    teacher_ = nullptr;
    has_teacher_ = false;
  }

  bool apply_ptqtp_reconstruct_experts(int num_planes, std::string* error_message) {
    if (num_planes < 1) {
      set_err(error_message, "ptqtp: num_planes must be >= 1");
      return false;
    }
    torch::NoGradGuard g;
    const int nb = core_->num_blocks();
    for (int i = 0; i < nb; ++i) {
      auto& w = core_->experts_[static_cast<std::size_t>(i)]->weight_;
      auto recon = reconstruct_ptqtp_weight(w, num_planes);
      if (!recon.sizes().equals(w.sizes())) {
        set_err(error_message, "ptqtp: bad expert weight shape");
        return false;
      }
      w.copy_(recon);
    }
    rebuild_optim();
    return true;
  }

  double ternary_quantize_element(double w_val) {
    // Greedy ternary projection: sign(w) * clamp(round(|w|/α), 0, 1)
    // where α = mean(|w|) for the weight tensor
    return w_val >= 0.0 ? 1.0 : (w_val <= 0.0 ? -1.0 : 0.0);
  }

  bool apply_simulated_annealing(double temperature, double cool_rate, double min_temperature,
                                  double target_acceptance, std::uint64_t step_mix,
                                  double* out_acceptance_rate, std::string* error_message) {
    if (temperature < min_temperature) {
      set_err(error_message, "sa: temperature below min_temperature");
      return false;
    }
    torch::NoGradGuard g;
    std::mt19937_64 rng(step_mix);
    std::uniform_real_distribution<double> uniform_dist(0.0, 1.0);
    std::normal_distribution<double> noise_dist(0.0, 1.0);

    const int nb = core_->num_blocks();
    double total_attempts = 0.0;
    double total_accepted = 0.0;

    for (int i = 0; i < nb; ++i) {
      auto& w = core_->experts_[static_cast<std::size_t>(i)]->weight_;
      const auto shape = w.sizes();
      const auto numel = w.numel();

      // Compute α for ternary quantization (mean absolute value)
      auto abs_val = w.abs();
      double alpha = abs_val.mean().item<double>() + 1e-8;

      // Current quantization error energy: E = Σ |ternary(w) * α - w|²
      auto ternary_target = torch::round(w / alpha).clamp(-1.0, 1.0);
      double energy_current = (ternary_target * alpha - w).pow(2).sum().item<double>();

      // Create candidate: add noise to weights
      auto w_flat = w.view({numel}).clone();
      std::vector<double> best_vals;
      best_vals.reserve(static_cast<size_t>(numel));

      // Metropolis-Hastings per weight element
      std::int64_t n_accepted = 0;
      std::int64_t n_trials = 0;

      // Iterate in chunks for memory efficiency
      for (std::int64_t idx = 0; idx < numel; ++idx) {
        double current_val = w_flat[idx].item<double>();
        double ternary_val = ternary_target.view({numel})[idx].item<double>();

        // Propose a perturbed value (exploration within continuous space)
        double perturbation = noise_dist(rng) * temperature * alpha;
        double candidate_val = current_val + perturbation;

        // Quantize candidate to ternary
        double candidate_ternary = candidate_val > 0.0 ? 1.0 : (candidate_val < 0.0 ? -1.0 : 0.0);
        double current_ternary = current_val > 0.0 ? 1.0 : (current_val < 0.0 ? -1.0 : 0.0);

        // Energy: distance between candidate weight and its quantized version
        double energy_candidate = std::pow(candidate_val - candidate_ternary * alpha, 2);
        double energy_current_elem = std::pow(current_val - current_ternary * alpha, 2);

        // Also consider the direct distance to target ternary
        double delta_energy = energy_candidate - energy_current_elem;

        // Metropolis-Hastings acceptance criterion
        bool accept = false;
        if (delta_energy <= 0) {
          accept = true;  // Lower energy always accepted
        } else {
          double acceptance_prob = std::exp(-delta_energy / std::max(temperature, 1e-10));
          if (uniform_dist(rng) < acceptance_prob) {
            accept = true;
          }
        }

        if (accept) {
          w_flat[idx] = candidate_val;
          ++n_accepted;
        }
        ++n_trials;
      }

      // After exploring, finalize by quantizing to ternary lattice
      if (temperature <= min_temperature) {
        // Fully quantize when frozen
        for (std::int64_t idx = 0; idx < numel; ++idx) {
          double val = w_flat[idx].item<double>();
          double quantized = val > 0.0 ? 1.0 : (val < 0.0 ? -1.0 : 0.0);
          w_flat[idx] = quantized * alpha;
        }
      }

      w.copy_(w_flat.view(shape));
      total_accepted += static_cast<double>(n_accepted);
      total_attempts += static_cast<double>(n_trials);
    }

    double acc_rate = total_attempts > 0.0 ? total_accepted / total_attempts : 0.0;
    if (out_acceptance_rate != nullptr) {
      *out_acceptance_rate = acc_rate;
    }
    sa_current_temperature_ = temperature * cool_rate;  // Update for next call

    rebuild_optim();
    return true;
  }

  double train_step_distill(std::size_t batch_size, std::int64_t io_dim, const float* x_rm, const float* t_rm,
                            double lambda_teacher, std::uint64_t step_mix) {
    (void)step_mix;
    const bool use_cloned = has_teacher_ && teacher_;
    const bool use_external = has_external_teacher_ && external_teacher_;
    if (!use_cloned && !use_external) {
      return train_step_supervised(batch_size, io_dim, x_rm, t_rm, step_mix);
    }
    const auto b = static_cast<std::int64_t>(std::max<std::size_t>(1, batch_size));
    auto x = torch::from_blob(const_cast<float*>(x_rm), {b, io_dim}, torch::TensorOptions().dtype(torch::kFloat32))
                 .clone();
    auto tgt = torch::from_blob(const_cast<float*>(t_rm), {b, io_dim}, torch::TensorOptions().dtype(torch::kFloat32))
                   .clone();
    core_->train();
    optim_->zero_grad();
    torch::Tensor teach_out;
    {
      torch::NoGradGuard ng;
      // Prefer external teacher if loaded, otherwise use cloned teacher
      if (use_external) {
        teach_out = external_teacher_->forward(x);
      } else {
        teach_out = teacher_->forward(x);
      }
    }
    auto stu_out = core_->forward(x);
    // MSE distillation loss (forward-KL style)
    auto loss = torch::mse_loss(stu_out, teach_out) + static_cast<float>(lambda_teacher) * torch::mse_loss(stu_out, tgt);
    loss.backward();
    optim_->step();
    return loss.item<double>();
  }

  double train_step_distill_reverse_kl(std::size_t batch_size, std::int64_t io_dim, const float* x_rm,
                                         const float* t_rm, double lambda_kl, double lambda_target,
                                         std::uint64_t step_mix) {
    (void)step_mix;
    const bool use_cloned = has_teacher_ && teacher_;
    const bool use_external = has_external_teacher_ && external_teacher_;
    if (!use_cloned && !use_external) {
      return train_step_supervised(batch_size, io_dim, x_rm, t_rm, step_mix);
    }
    const auto b = static_cast<std::int64_t>(std::max<std::size_t>(1, batch_size));
    auto x = torch::from_blob(const_cast<float*>(x_rm), {b, io_dim}, torch::TensorOptions().dtype(torch::kFloat32))
                 .clone();
    auto tgt = torch::from_blob(const_cast<float*>(t_rm), {b, io_dim}, torch::TensorOptions().dtype(torch::kFloat32))
                   .clone();
    core_->train();
    optim_->zero_grad();
    torch::Tensor teach_out;
    torch::Tensor teach_logits;
    {
      torch::NoGradGuard ng;
      if (use_external) {
        teach_out = external_teacher_->forward(x);
      } else {
        teach_out = teacher_->forward(x);
      }
      // Convert teacher output to distribution via soft normalization
      teach_logits = teach_out / teach_out.abs().mean().clamp_min(1e-8);
    }
    auto stu_out = core_->forward(x);
    // Student logits distribution
    auto stu_logits = stu_out / stu_out.abs().mean().clamp_min(1e-8);
    // Reverse-KL: D_KL(q_student || p_teacher)
    // = E_{x ~ q} [log q(x) - log p(x)]
    // Using Monte Carlo estimation with student's distribution
    auto log_q = torch::log_softmax(stu_logits, /*dim=*/-1);
    auto log_p = torch::log_softmax(teach_logits, /*dim=*/-1);
    // Reverse KL on softmax-normalized distributions (per-element for regression-like outputs)
    auto kl_reverse = (torch::exp(log_q) * (log_q - log_p)).sum(-1).mean();
    // Target loss (MSE on task targets)
    auto target_loss = torch::mse_loss(stu_out, tgt);
    auto loss = kl_reverse + static_cast<float>(lambda_kl) * kl_reverse + static_cast<float>(lambda_target) * target_loss;
    loss.backward();
    optim_->step();
    return loss.item<double>();
  }

  bool load_teacher_from_interchange(const std::string& path, std::uint32_t native_bloch_seq_len,
                                      std::uint32_t native_bloch_num_heads, std::string* error_message) {
    // Create external teacher with same geometry as core
    const std::int64_t d = core_->d_model();
    const std::int64_t io = core_->io_d_model();
    const int nb = core_->num_blocks();
    const int bseq = core_->native_bloch_seq_len();
    const int bheads = core_->native_bloch_num_heads();

    external_teacher_ = CoreModule(d, io, nb, bseq, bheads);
    torch::NoGradGuard g;

    // Load checkpoint into external teacher
    std::string raw;
    if (!read_file_all(path, &raw, error_message)) {
      external_teacher_ = nullptr;
      has_external_teacher_ = false;
      return false;
    }
    if (raw.size() < 8 + 8) {
      set_err_interchange(error_message, "teacher interchange file too small");
      external_teacher_ = nullptr;
      has_external_teacher_ = false;
      return false;
    }
    if (std::memcmp(raw.data(), kTpemInterchangeMagic, 8) != 0) {
      set_err_interchange(error_message, "not a valid interchange file for teacher");
      external_teacher_ = nullptr;
      has_external_teacher_ = false;
      return false;
    }
    const auto* u = reinterpret_cast<const unsigned char*>(raw.data() + 8);
    const std::uint64_t json_len = read_u64_le(u);
    if (json_len > raw.size() - 16) {
      set_err_interchange(error_message, "teacher interchange: invalid json length");
      external_teacher_ = nullptr;
      has_external_teacher_ = false;
      return false;
    }
    std::string env_json(raw.substr(16, static_cast<std::size_t>(json_len)));
    nlohmann::json env;
    try {
      env = nlohmann::json::parse(env_json);
    } catch (const std::exception& e) {
      set_err_interchange(error_message, std::string("teacher interchange: JSON error: ") + e.what());
      external_teacher_ = nullptr;
      has_external_teacher_ = false;
      return false;
    }

    std::vector<torch::nn::Parameter> teacher_params = external_teacher_->parameters();
    const std::size_t st_begin = 16 + static_cast<std::size_t>(json_len);
    const std::string_view st_blob(raw.data() + st_begin, raw.size() - st_begin);
    std::unordered_map<std::string, torch::Tensor> tensors;
    if (!decode_safetensors_f32(st_blob, &tensors, error_message)) {
      external_teacher_ = nullptr;
      has_external_teacher_ = false;
      return false;
    }

    // Load weights into external teacher (reuse existing loading logic)
    // We need to adapt the flat tensor loading for the external teacher
    constexpr const char* qr_prefix = "quantum_router.";
    std::unordered_map<std::string, torch::Tensor> filtered;
    for (const auto& [key, val] : tensors) {
      if (key.rfind(qr_prefix, 0) != 0) {  // Skip router tensors
        filtered[key] = val;
      }
    }

    // Copy stem/head if present
    if (external_teacher_->stem_head()) {
      auto it_w = filtered.find("input_stem.weight");
      if (it_w == filtered.end()) {
        set_err_interchange(error_message, "teacher: missing input_stem.weight");
        external_teacher_ = nullptr;
        has_external_teacher_ = false;
        return false;
      }
      auto it_b = filtered.find("input_stem.bias");
      const torch::Tensor* pb = (it_b != filtered.end()) ? &it_b->second : nullptr;
      if (!copy_linear_weight_bias(external_teacher_->stem_, it_w->second, pb, error_message)) {
        external_teacher_ = nullptr;
        has_external_teacher_ = false;
        return false;
      }
      auto ow = filtered.find("output_head.weight");
      if (ow == filtered.end()) {
        set_err_interchange(error_message, "teacher: missing output_head.weight");
        external_teacher_ = nullptr;
        has_external_teacher_ = false;
        return false;
      }
      auto ob = filtered.find("output_head.bias");
      const torch::Tensor* pob = (ob != filtered.end()) ? &ob->second : nullptr;
      if (!copy_linear_weight_bias(external_teacher_->head_, ow->second, pob, error_message)) {
        external_teacher_ = nullptr;
        has_external_teacher_ = false;
        return false;
      }
    }

    // Copy ternary expert weights
    const int nb_local = external_teacher_->num_blocks();
    for (int i = 0; i < nb_local; ++i) {
      std::string wkey = (nb_local == 1) ? std::string("ternary_expert.weight")
                                          : ("ternary_blocks." + std::to_string(i) + ".weight");
      auto it = filtered.find(wkey);
      if (it == filtered.end() && nb_local == 1) {
        it = filtered.find("ternary_blocks.0.weight");
      }
      if (it == filtered.end()) {
        set_err_interchange(error_message, "teacher: missing " + wkey);
        external_teacher_ = nullptr;
        has_external_teacher_ = false;
        return false;
      }
      auto t = it->second.detach().cpu().to(torch::kFloat32).contiguous();
      auto& ex = external_teacher_->experts_[static_cast<std::size_t>(i)];
      if (t.dim() != 2 || t.size(0) != external_teacher_->d_model() || t.size(1) != external_teacher_->d_model()) {
        set_err_interchange(error_message, "teacher: bad expert weight shape");
        external_teacher_ = nullptr;
        has_external_teacher_ = false;
        return false;
      }
      ex->weight_.copy_(t);
    }

    // Copy Bloch attention params if present
    if (external_teacher_->native_bloch_seq_len() > 0 && external_teacher_->bloch_) {
      auto itp = filtered.find("bloch.pos_embed");
      if (itp != filtered.end()) {
        auto te = itp->second.detach().cpu().to(torch::kFloat32).contiguous();
        if (te.sizes().equals(external_teacher_->pos_embed_.sizes())) {
          external_teacher_->pos_embed_.copy_(te);
        }
      }
      auto load_lin = [&](std::string_view base, torch::nn::Linear& lin) {
        const std::string wkey = std::string(base) + ".weight";
        auto iw = filtered.find(wkey);
        if (iw == filtered.end()) return;
        const torch::Tensor* loc_pb = nullptr;
        const std::string bkey = std::string(base) + ".bias";
        auto ib = filtered.find(bkey);
        if (ib != filtered.end()) loc_pb = &ib->second;
        copy_linear_weight_bias(lin, iw->second, loc_pb, nullptr);
      };
      load_lin("bloch.q_proj", external_teacher_->bloch_->q_proj_);
      load_lin("bloch.k_proj", external_teacher_->bloch_->k_proj_);
      load_lin("bloch.v_proj", external_teacher_->bloch_->v_proj_);
      load_lin("bloch.out_proj", external_teacher_->bloch_->out_proj_);
    }

    external_teacher_->eval();
    has_external_teacher_ = true;
    return true;
  }

  static constexpr int kCascadeStateDim = 8;
  static constexpr int kCascadeNumActions = 4;
  static constexpr int kCascadeMaxSteps = 16;
  static constexpr double kGrpoEps = 1e-8;

  torch::Tensor cascade_grpo_loss_tensor(std::size_t group_size, std::uint64_t step_mix) {
    const auto gsz = static_cast<std::int64_t>(std::max<std::size_t>(1, group_size));
    std::vector<torch::Tensor> logprob_sums;
    logprob_sums.reserve(static_cast<std::size_t>(gsz));
    std::vector<torch::Tensor> returns_list;
    returns_list.reserve(static_cast<std::size_t>(gsz));

    for (std::int64_t g = 0; g < gsz; ++g) {
      torch::manual_seed(static_cast<std::uint64_t>(step_mix ^ (static_cast<std::uint64_t>(g + 1) * 0x9E3779B9ULL)));
      auto s = torch::randn({kCascadeStateDim}, torch::dtype(torch::kFloat32));
      double total_r = 0.0;
      std::vector<torch::Tensor> traj_lps;
      traj_lps.reserve(static_cast<std::size_t>(kCascadeMaxSteps));
      for (int t = 0; t < kCascadeMaxSteps; ++t) {
        auto logits = cascade_policy_->forward(s);
        auto log_p = torch::log_softmax(logits, /*dim=*/0);
        auto probs = torch::softmax(logits, 0);
        auto a = torch::multinomial(probs, /*num_samples=*/1, /*replacement=*/true).squeeze();
        const int64_t ac = a.item<int64_t>();
        traj_lps.push_back(log_p[ac]);
        total_r += -0.01 * static_cast<double>(ac) +
                   0.1 * s.sum().item<double>() / static_cast<double>(kCascadeStateDim);
        s = (s + 0.05 * torch::randn_like(s)).detach();
      }
      logprob_sums.push_back(torch::stack(traj_lps).sum());
      returns_list.push_back(torch::tensor(total_r, torch::dtype(torch::kFloat32)));
    }

    auto logprob_tensor = torch::stack(logprob_sums);
    auto returns_tensor = torch::stack(returns_list).to(logprob_tensor.dtype()).detach();
    torch::Tensor adv = returns_tensor;
    if (gsz > 1) {
      auto mean = returns_tensor.mean();
      auto centered = returns_tensor - mean;
      auto st = centered.pow(2).mean().sqrt();
      adv = centered / (st + kGrpoEps);
    }
    return -(adv * logprob_tensor).mean();
  }

  torch::Tensor cascade_cispo_loss_tensor(std::size_t group_size, double epsilon, std::uint64_t step_mix) {
    const auto gsz = static_cast<std::int64_t>(std::max<std::size_t>(1, group_size));
    const double low = 1.0 - epsilon;
    const double high = 1.0 + epsilon;
    std::vector<torch::Tensor> new_sums;
    std::vector<torch::Tensor> old_sums;
    std::vector<torch::Tensor> returns_list;
    new_sums.reserve(static_cast<std::size_t>(gsz));
    old_sums.reserve(static_cast<std::size_t>(gsz));
    returns_list.reserve(static_cast<std::size_t>(gsz));

    for (std::int64_t g = 0; g < gsz; ++g) {
      torch::manual_seed(static_cast<std::uint64_t>(step_mix ^ (static_cast<std::uint64_t>(g + 101) * 0x85EBCA6BULL)));
      auto s = torch::randn({kCascadeStateDim}, torch::dtype(torch::kFloat32));
      double total_r = 0.0;
      std::vector<torch::Tensor> old_lps;
      std::vector<torch::Tensor> states;
      std::vector<int64_t> actions;
      old_lps.reserve(static_cast<std::size_t>(kCascadeMaxSteps));
      states.reserve(static_cast<std::size_t>(kCascadeMaxSteps));
      actions.reserve(static_cast<std::size_t>(kCascadeMaxSteps));

      for (int t = 0; t < kCascadeMaxSteps; ++t) {
        torch::Tensor logits;
        torch::Tensor log_p;
        int64_t ac = 0;
        {
          torch::NoGradGuard ng;
          logits = cascade_policy_->forward(s);
          log_p = torch::log_softmax(logits, 0);
          auto probs = torch::softmax(logits, 0);
          auto a = torch::multinomial(probs, 1, true).squeeze();
          ac = a.item<int64_t>();
          old_lps.push_back(log_p[ac].detach().clone());
        }
        states.push_back(s.clone());
        actions.push_back(ac);
        total_r += -0.01 * static_cast<double>(ac) +
                   0.1 * s.sum().item<double>() / static_cast<double>(kCascadeStateDim);
        s = (s + 0.05 * torch::randn_like(s)).detach();
      }

      std::vector<torch::Tensor> new_parts;
      new_parts.reserve(static_cast<std::size_t>(kCascadeMaxSteps));
      for (int t = 0; t < kCascadeMaxSteps; ++t) {
        auto logits_n = cascade_policy_->forward(states[static_cast<std::size_t>(t)]);
        auto log_pn = torch::log_softmax(logits_n, 0);
        new_parts.push_back(log_pn[actions[static_cast<std::size_t>(t)]]);
      }
      new_sums.push_back(torch::stack(new_parts).sum());
      old_sums.push_back(torch::stack(old_lps).sum());
      returns_list.push_back(torch::tensor(total_r, torch::dtype(torch::kFloat32)));
    }

    auto logprob_tensor = torch::stack(new_sums);
    auto old_tensor = torch::stack(old_sums).detach();
    auto returns_tensor = torch::stack(returns_list).to(logprob_tensor.dtype()).detach();
    auto diff = torch::clamp(logprob_tensor - old_tensor, -20.0, 20.0);
    auto ratio = torch::exp(diff);
    auto clipped = torch::clamp(ratio, low, high);
    torch::Tensor adv = returns_tensor;
    if (gsz > 1) {
      auto mean = returns_tensor.mean();
      auto centered = returns_tensor - mean;
      auto st = centered.pow(2).mean().sqrt();
      adv = centered / (st + kGrpoEps);
    }
    auto coeff = clipped.detach();
    return -(coeff * adv * logprob_tensor).mean();
  }

  double train_step_cascade_grpo(std::size_t group_size, std::uint64_t step_mix) {
    ensure_cascade_policy();
    cascade_policy_->train();
    auto loss = cascade_grpo_loss_tensor(group_size, step_mix);
    cascade_optim_->zero_grad();
    loss.backward();
    cascade_optim_->step();
    return loss.item<double>();
  }

  double train_step_cascade_cispo(std::size_t group_size, double epsilon, std::uint64_t step_mix) {
    ensure_cascade_policy();
    cascade_policy_->train();
    auto loss = cascade_cispo_loss_tensor(group_size, epsilon, step_mix);
    cascade_optim_->zero_grad();
    loss.backward();
    cascade_optim_->step();
    return loss.item<double>();
  }

  double train_step_joint_supervised_cascade(std::size_t batch_size, std::size_t group_size, double cascade_lambda,
                                             bool use_cispo, double cispo_epsilon, std::uint64_t step_mix) {
    ensure_cascade_policy();
    const auto b = static_cast<std::int64_t>(std::max<std::size_t>(1, batch_size));
    const std::int64_t io = core_->io_d_model();
    torch::manual_seed(step_mix);
    auto x = torch::randn({b, io}, torch::dtype(torch::kFloat32));
    auto target = torch::randn({b, io}, torch::dtype(torch::kFloat32));
    core_->train();
    cascade_policy_->train();
    optim_->zero_grad();
    cascade_optim_->zero_grad();
    auto out = core_->forward(x);
    auto mse = torch::mse_loss(out, target);
    const std::uint64_t pol_mix = step_mix ^ 0xC0DEC0DEC0DEC0DEULL;
    torch::Tensor pol =
        use_cispo ? cascade_cispo_loss_tensor(group_size, cispo_epsilon, pol_mix)
                  : cascade_grpo_loss_tensor(group_size, pol_mix);
    const float lam = static_cast<float>(cascade_lambda);
    auto total = mse + lam * pol;
    total.backward();
    optim_->step();
    cascade_optim_->step();
    return total.item<double>();
  }
};

std::unique_ptr<LibTorchTpemTrainer> LibTorchTpemTrainer::create(double learning_rate, std::uint64_t seed,
                                                                   std::string* error_message) {
  (void)error_message;
  auto t = std::unique_ptr<LibTorchTpemTrainer>(new LibTorchTpemTrainer());
  t->impl_ = std::make_unique<Impl>(learning_rate, seed);
  return t;
}

bool LibTorchTpemTrainer::init_geometry(std::int64_t d_model, std::int64_t io_d_model, int num_ternary_blocks,
                                        int native_bloch_seq_len, int native_bloch_num_heads,
                                        std::string* error_message) {
  if (d_model < 32 || d_model > 1048576) {
    set_err_interchange(error_message, "init_geometry: d_model out of range [32,1048576]");
    return false;
  }
  if (io_d_model < 8 || io_d_model > 1048576) {
    set_err_interchange(error_message, "init_geometry: io_d_model out of range [8,1048576]");
    return false;
  }
  if (num_ternary_blocks < 1 || num_ternary_blocks > 1024) {
    set_err_interchange(error_message, "init_geometry: num_ternary_blocks out of range [1,1024]");
    return false;
  }
  int bseq = native_bloch_seq_len;
  int bhead = native_bloch_num_heads;
  if (bseq > 0) {
    if (bhead <= 0) {
      set_err_interchange(error_message, "init_geometry: native Bloch requires positive num_heads");
      return false;
    }
    if (d_model % bhead != 0) {
      set_err_interchange(error_message, "init_geometry: native Bloch requires d_model divisible by num_heads");
      return false;
    }
  } else {
    bhead = 0;
  }
  impl_->rebuild_core(d_model, io_d_model, num_ternary_blocks, bseq, bhead);
  return true;
}

bool LibTorchTpemTrainer::load_interchange(const std::string& path, bool attention_backend_is_bloch,
                                           std::uint32_t native_bloch_seq_len, std::uint32_t native_bloch_num_heads,
                                           std::string* error_message) {
  std::string raw;
  if (!read_file_all(path, &raw, error_message)) {
    return false;
  }
  if (raw.size() < 8 + 8) {
    set_err_interchange(error_message, "interchange file too small");
    return false;
  }
  if (std::memcmp(raw.data(), kTpemInterchangeMagic, 8) != 0) {
    set_err_interchange(
        error_message,
        "not a native TPEM interchange file (expected magic QMWTPEM2); use Python export "
        "save_trainable_tpem_interchange_v2 or a C++ checkpoint");
    return false;
  }
  const auto* u = reinterpret_cast<const unsigned char*>(raw.data() + 8);
  const std::uint64_t json_len = read_u64_le(u);
  if (json_len > raw.size() - 16) {
    set_err_interchange(error_message, "interchange: invalid json length");
    return false;
  }
  std::string env_json(raw.substr(16, static_cast<std::size_t>(json_len)));
  nlohmann::json env;
  try {
    env = nlohmann::json::parse(env_json);
  } catch (const std::exception& e) {
    set_err_interchange(error_message, std::string("interchange: envelope JSON error: ") + e.what());
    return false;
  }
  const int ver = env.value("format_version", 0);
  if (ver != kTrainableTpemFormatVersionV2) {
    set_err_interchange(error_message, "interchange: unsupported format_version (expected 2)");
    return false;
  }
  int nblk = env.value("num_ternary_blocks", 1);
  if (nblk < 1) {
    nblk = 1;
  }
  if (nblk > 1024) {
    set_err_interchange(error_message, "interchange: num_ternary_blocks exceeds 1024");
    return false;
  }
  std::int64_t dm = kTpemDModel;
  if (env.contains("d_model") && env["d_model"].is_number_integer()) {
    dm = static_cast<std::int64_t>(env["d_model"].get<int>());
  } else if (env.contains("d_model") && env["d_model"].is_number_unsigned()) {
    dm = static_cast<std::int64_t>(env["d_model"].get<std::uint64_t>());
  }
  if (dm < 32 || dm > 1048576) {
    set_err_interchange(error_message, "interchange: d_model out of supported range");
    return false;
  }
  std::int64_t iodm = dm;
  if (env.contains("io_d_model") && env["io_d_model"].is_number_integer()) {
    iodm = static_cast<std::int64_t>(env["io_d_model"].get<int>());
  } else if (env.contains("io_d_model") && env["io_d_model"].is_number_unsigned()) {
    iodm = static_cast<std::int64_t>(env["io_d_model"].get<std::uint64_t>());
  }
  if (iodm < 8 || iodm > 1048576) {
    set_err_interchange(error_message, "interchange: io_d_model out of supported range");
    return false;
  }

  bool use_bloch = attention_backend_is_bloch;
  std::uint32_t seq_c = native_bloch_seq_len;
  std::uint32_t heads_c = native_bloch_num_heads;
  if (!use_bloch) {
    int es = 0;
    if (env.contains("native_bloch_seq_len") && env["native_bloch_seq_len"].is_number_integer()) {
      es = env["native_bloch_seq_len"].get<int>();
    } else if (env.contains("native_bloch_seq_len") && env["native_bloch_seq_len"].is_number_unsigned()) {
      es = static_cast<int>(env["native_bloch_seq_len"].get<std::uint64_t>());
    }
    if (es > 0) {
      use_bloch = true;
      if (seq_c == 0) {
        seq_c = static_cast<std::uint32_t>(es);
      }
      if (heads_c == 0 && env.contains("native_bloch_num_heads")) {
        if (env["native_bloch_num_heads"].is_number_integer()) {
          heads_c = static_cast<std::uint32_t>(std::max(1, env["native_bloch_num_heads"].get<int>()));
        } else if (env["native_bloch_num_heads"].is_number_unsigned()) {
          heads_c = static_cast<std::uint32_t>(env["native_bloch_num_heads"].get<std::uint64_t>());
        }
      }
      if (heads_c == 0) {
        heads_c = 4;
      }
    }
  }

  int bloch_seq = 0;
  int bloch_heads = 0;
  if (use_bloch) {
    constexpr int kDefaultSeq = 8;
    constexpr int kDefaultHeads = 4;
    bloch_seq = seq_c > 0 ? static_cast<int>(seq_c) : kDefaultSeq;
    bloch_heads = heads_c > 0 ? static_cast<int>(heads_c) : kDefaultHeads;
    while (bloch_heads > 1 && (dm % bloch_heads) != 0) {
      --bloch_heads;
    }
    if (bloch_heads < 1) {
      bloch_heads = 1;
    }
  }

  impl_->rebuild_core(dm, iodm, nblk, bloch_seq, bloch_heads);
  const std::size_t st_begin = 16 + static_cast<std::size_t>(json_len);
  const std::string_view st_blob(raw.data() + st_begin, raw.size() - st_begin);
  std::unordered_map<std::string, torch::Tensor> tensors;
  if (!decode_safetensors_f32(st_blob, &tensors, error_message)) {
    if (error_message != nullptr && error_message->find("Hint:") == std::string::npos) {
      error_message->append(kNativeInterchangeHint);
    }
    return false;
  }
  return impl_->load_flat_tensors(tensors, error_message);
}

InterchangeTensorSnapshot LibTorchTpemTrainer::capture_interchange_tensors() const {
  InterchangeTensorSnapshot s;
  s.d_model = impl_->core_->d_model();
  s.io_d_model = impl_->core_->io_d_model();
  s.num_ternary_blocks = impl_->core_->num_blocks();
  s.native_bloch_seq_len = impl_->core_->native_bloch_seq_len();
  s.native_bloch_num_heads = impl_->core_->native_bloch_num_heads();
  s.tensors = impl_->tensors_for_save();
  return s;
}

bool LibTorchTpemTrainer::write_interchange_to_path(const std::string& path, const std::string& run_id,
                                                    std::size_t epoch_1based, double train_loss, double val_loss,
                                                    double learning_rate, InterchangeTensorSnapshot snap,
                                                    std::string* error_message) {
  try {
    const std::filesystem::path p(path);
    if (p.has_parent_path()) {
      std::filesystem::create_directories(p.parent_path());
    }
  } catch (const std::exception& e) {
    set_err(error_message, std::string("interchange: mkdir: ") + e.what());
    return false;
  }

  nlohmann::json env;
  env["format_version"] = kTrainableTpemFormatVersionV2;
  env["d_model"] = snap.d_model;
  env["num_ternary_blocks"] = snap.num_ternary_blocks;
  env["io_d_model"] = snap.io_d_model;
  if (snap.native_bloch_seq_len > 0) {
    env["native_bloch_seq_len"] = snap.native_bloch_seq_len;
    env["native_bloch_num_heads"] = snap.native_bloch_num_heads;
  }
  env["tensor_layout"] = "safetensors_f32";
  env["meta"] = nlohmann::json::object();
  env["training_meta"] = {
      {"run_id", run_id},
      {"epoch", epoch_1based},
      {"train_loss", train_loss},
      {"val_loss", val_loss},
      {"learning_rate", learning_rate},
  };

  const std::string env_str = env.dump();
  auto st = encode_safetensors_f32(snap.tensors, error_message);
  if (!st.has_value()) {
    return false;
  }

  std::string out;
  out.resize(8 + 8 + env_str.size() + st->size());
  std::memcpy(out.data(), kTpemInterchangeMagic, 8);
  unsigned char jl[8];
  write_u64_le(jl, static_cast<std::uint64_t>(env_str.size()));
  std::memcpy(out.data() + 8, jl, 8);
  std::memcpy(out.data() + 16, env_str.data(), env_str.size());
  std::memcpy(out.data() + 16 + env_str.size(), st->data(), st->size());

  if (!write_file_all(path, out, error_message)) {
    return false;
  }
  return true;
}

bool LibTorchTpemTrainer::save_interchange(const std::string& path, const std::string& run_id,
                                           std::size_t epoch_1based, double train_loss, double val_loss,
                                           double learning_rate, std::string* error_message) {
  InterchangeTensorSnapshot snap = capture_interchange_tensors();
  return write_interchange_to_path(path, run_id, epoch_1based, train_loss, val_loss, learning_rate, std::move(snap),
                                   error_message);
}

double LibTorchTpemTrainer::train_step(std::size_t batch_size, std::uint64_t step_mix) {
  return impl_->train_step(batch_size, step_mix);
}

double LibTorchTpemTrainer::eval_step(std::size_t batch_size, std::uint64_t step_mix) {
  return impl_->eval_step(batch_size, step_mix);
}

void LibTorchTpemTrainer::set_learning_rate(double lr) { impl_->set_learning_rate(lr); }

double LibTorchTpemTrainer::train_step_supervised(std::size_t batch_size, std::int64_t io_dim,
                                                  const float* x_row_major, const float* target_row_major,
                                                  std::uint64_t step_mix) {
  return impl_->train_step_supervised(batch_size, io_dim, x_row_major, target_row_major, step_mix);
}

double LibTorchTpemTrainer::eval_step_supervised(std::size_t batch_size, std::int64_t io_dim,
                                                 const float* x_row_major, const float* target_row_major,
                                                 std::uint64_t step_mix) {
  return impl_->eval_step_supervised(batch_size, io_dim, x_row_major, target_row_major, step_mix);
}

bool LibTorchTpemTrainer::clone_teacher_from_core(std::string* error_message) {
  return impl_->clone_teacher_from_core(error_message);
}

bool LibTorchTpemTrainer::load_teacher_from_interchange(const std::string& path,
                                                         std::uint32_t native_bloch_seq_len,
                                                         std::uint32_t native_bloch_num_heads,
                                                         std::string* error_message) {
  return impl_->load_teacher_from_interchange(path, native_bloch_seq_len, native_bloch_num_heads, error_message);
}

void LibTorchTpemTrainer::reset_teacher() {
  impl_->reset_teacher();
  impl_->external_teacher_ = nullptr;
  impl_->has_external_teacher_ = false;
}

bool LibTorchTpemTrainer::apply_ptqtp_reconstruct_experts(int num_planes, std::string* error_message) {
  return impl_->apply_ptqtp_reconstruct_experts(num_planes, error_message);
}

double LibTorchTpemTrainer::train_step_distill(std::size_t batch_size, std::int64_t io_dim,
                                               const float* x_row_major, const float* target_row_major,
                                               double lambda_teacher, std::uint64_t step_mix) {
  return impl_->train_step_distill(batch_size, io_dim, x_row_major, target_row_major, lambda_teacher, step_mix);
}

double LibTorchTpemTrainer::train_step_distill_reverse_kl(std::size_t batch_size, std::int64_t io_dim,
                                                            const float* x_row_major, const float* target_row_major,
                                                            double lambda_kl, double lambda_target,
                                                            std::uint64_t step_mix) {
  return impl_->train_step_distill_reverse_kl(batch_size, io_dim, x_row_major, target_row_major, lambda_kl,
                                                lambda_target, step_mix);
}

double LibTorchTpemTrainer::train_step_cascade_grpo(std::size_t group_size, std::uint64_t step_mix) {
  return impl_->train_step_cascade_grpo(group_size, step_mix);
}

double LibTorchTpemTrainer::train_step_cascade_cispo(std::size_t group_size, double epsilon,
                                                     std::uint64_t step_mix) {
  return impl_->train_step_cascade_cispo(group_size, epsilon, step_mix);
}

double LibTorchTpemTrainer::train_step_joint_supervised_cascade(std::size_t batch_size, std::size_t group_size,
                                                                double cascade_lambda, bool use_cispo,
                                                                double cispo_epsilon, std::uint64_t step_mix) {
  return impl_->train_step_joint_supervised_cascade(batch_size, group_size, cascade_lambda, use_cispo, cispo_epsilon,
                                                      step_mix);
}

bool LibTorchTpemTrainer::apply_simulated_annealing(double temperature, double cool_rate, double min_temperature,
                                                     double target_acceptance, std::uint64_t step_mix,
                                                     double* out_acceptance_rate, std::string* error_message) {
  return impl_->apply_simulated_annealing(temperature, cool_rate, min_temperature, target_acceptance, step_mix,
                                            out_acceptance_rate, error_message);
}

LibTorchTpemTrainer::~LibTorchTpemTrainer() = default;

}  // namespace qminiwasm::training
