#include "qminiwasm/training/libtorch_ternary_trainer.hpp"

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
  torch::nn::Linear stem_{nullptr};
  torch::nn::Linear head_{nullptr};
  std::vector<torch::nn::LayerNorm> norms_;
  std::vector<TernaryExpert> experts_;

  CoreModuleImpl(std::int64_t d_model, std::int64_t io_d_model, int num_blocks)
      : d_model_(d_model), io_d_model_(io_d_model), num_blocks_(num_blocks) {
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

  torch::Tensor forward(torch::Tensor x) {
    torch::Tensor y = stem_head_ ? stem_->forward(x) : x;
    for (int i = 0; i < num_blocks_; ++i) {
      y = y + experts_[i]->forward(norms_[i]->forward(y));
    }
    return stem_head_ ? head_->forward(y) : y;
  }
};

TORCH_MODULE(CoreModule);

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
    set_err(error_message, "linear weight: shape mismatch vs checkpoint");
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
  std::unique_ptr<torch::optim::Adam> optim_;
  std::map<std::string, torch::Tensor> frozen_router_;
  double last_lr_ = 1e-3;
  std::uint64_t seed_ = 42;

  Impl(double learning_rate, std::uint64_t seed)
      : last_lr_(learning_rate), seed_(seed) {
    torch::manual_seed(seed_);
    core_ = CoreModule(kTpemDModel, kTpemDModel, 1);
    rebuild_optim();
  }

  void rebuild_optim() {
    optim_ = std::make_unique<torch::optim::Adam>(core_->parameters(),
                                                  torch::optim::AdamOptions(last_lr_));
  }

  void rebuild_core(std::int64_t d_model, std::int64_t io_d_model, int num_blocks) {
    torch::manual_seed(seed_);
    core_ = CoreModule(d_model, io_d_model, num_blocks);
    rebuild_optim();
    frozen_router_.clear();
  }

  void set_learning_rate(double lr) {
    last_lr_ = lr;
    for (auto& group : optim_->param_groups()) {
      static_cast<torch::optim::AdamOptions&>(group.options()).lr(lr);
    }
  }

  std::map<std::string, torch::Tensor> tensors_for_save() const {
    std::map<std::string, torch::Tensor> m;
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
        set_err(error_message, "missing input_stem.weight (io_d_model != d_model in envelope)");
        return false;
      }
      auto it_b = flat.find("input_stem.bias");
      const torch::Tensor* pb = (it_b != flat.end()) ? &it_b->second : nullptr;
      if (!copy_linear_weight_bias(core_->stem_, it_w->second, pb, error_message)) {
        return false;
      }
      auto ow = flat.find("output_head.weight");
      if (ow == flat.end()) {
        set_err(error_message, "missing output_head.weight");
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
        set_err(error_message, "missing " + wkey + " in interchange payload");
        return false;
      }
      auto t = it->second.detach().cpu().to(torch::kFloat32).contiguous();
      auto& ex = core_->experts_[static_cast<std::size_t>(i)];
      if (t.dim() != 2 || t.size(0) != core_->d_model() || t.size(1) != core_->d_model()) {
        set_err(error_message, "ternary block weight: bad shape vs d_model");
        return false;
      }
      torch::NoGradGuard guard;
      ex->weight_.copy_(t);
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
};

std::unique_ptr<LibTorchTpemTrainer> LibTorchTpemTrainer::create(double learning_rate, std::uint64_t seed,
                                                                   std::string* error_message) {
  (void)error_message;
  auto t = std::unique_ptr<LibTorchTpemTrainer>(new LibTorchTpemTrainer());
  t->impl_ = std::make_unique<Impl>(learning_rate, seed);
  return t;
}

bool LibTorchTpemTrainer::init_geometry(std::int64_t d_model, std::int64_t io_d_model, int num_ternary_blocks,
                                        std::string* error_message) {
  if (d_model < 32 || d_model > 1048576) {
    set_err(error_message, "init_geometry: d_model out of range [32,1048576]");
    return false;
  }
  if (io_d_model < 8 || io_d_model > 1048576) {
    set_err(error_message, "init_geometry: io_d_model out of range [8,1048576]");
    return false;
  }
  if (num_ternary_blocks < 1 || num_ternary_blocks > 1024) {
    set_err(error_message, "init_geometry: num_ternary_blocks out of range [1,1024]");
    return false;
  }
  impl_->rebuild_core(d_model, io_d_model, num_ternary_blocks);
  return true;
}

bool LibTorchTpemTrainer::load_interchange(const std::string& path, std::string* error_message) {
  std::string raw;
  if (!read_file_all(path, &raw, error_message)) {
    return false;
  }
  if (raw.size() < 8 + 8) {
    set_err(error_message, "interchange file too small");
    return false;
  }
  if (std::memcmp(raw.data(), kTpemInterchangeMagic, 8) != 0) {
    set_err(error_message,
            "not a native TPEM interchange file (expected magic QMWTPEM2); use Python export "
            "save_trainable_tpem_interchange_v2 or a C++ checkpoint");
    return false;
  }
  const auto* u = reinterpret_cast<const unsigned char*>(raw.data() + 8);
  const std::uint64_t json_len = read_u64_le(u);
  if (json_len > raw.size() - 16) {
    set_err(error_message, "interchange: invalid json length");
    return false;
  }
  std::string env_json(raw.substr(16, static_cast<std::size_t>(json_len)));
  nlohmann::json env;
  try {
    env = nlohmann::json::parse(env_json);
  } catch (const std::exception& e) {
    set_err(error_message, std::string("interchange: envelope JSON error: ") + e.what());
    return false;
  }
  const int ver = env.value("format_version", 0);
  if (ver != kTrainableTpemFormatVersionV2) {
    set_err(error_message, "interchange: unsupported format_version (expected 2)");
    return false;
  }
  int nblk = env.value("num_ternary_blocks", 1);
  if (nblk < 1) {
    nblk = 1;
  }
  if (nblk > 1024) {
    set_err(error_message, "interchange: num_ternary_blocks exceeds 1024");
    return false;
  }
  std::int64_t dm = kTpemDModel;
  if (env.contains("d_model") && env["d_model"].is_number_integer()) {
    dm = static_cast<std::int64_t>(env["d_model"].get<int>());
  } else if (env.contains("d_model") && env["d_model"].is_number_unsigned()) {
    dm = static_cast<std::int64_t>(env["d_model"].get<std::uint64_t>());
  }
  if (dm < 32 || dm > 1048576) {
    set_err(error_message, "interchange: d_model out of supported range");
    return false;
  }
  std::int64_t iodm = dm;
  if (env.contains("io_d_model") && env["io_d_model"].is_number_integer()) {
    iodm = static_cast<std::int64_t>(env["io_d_model"].get<int>());
  } else if (env.contains("io_d_model") && env["io_d_model"].is_number_unsigned()) {
    iodm = static_cast<std::int64_t>(env["io_d_model"].get<std::uint64_t>());
  }
  if (iodm < 8 || iodm > 1048576) {
    set_err(error_message, "interchange: io_d_model out of supported range");
    return false;
  }

  impl_->rebuild_core(dm, iodm, nblk);
  const std::size_t st_begin = 16 + static_cast<std::size_t>(json_len);
  const std::string_view st_blob(raw.data() + st_begin, raw.size() - st_begin);
  std::unordered_map<std::string, torch::Tensor> tensors;
  if (!decode_safetensors_f32(st_blob, &tensors, error_message)) {
    return false;
  }
  return impl_->load_flat_tensors(tensors, error_message);
}

bool LibTorchTpemTrainer::save_interchange(const std::string& path, const std::string& run_id,
                                           std::size_t epoch_1based, double train_loss, double val_loss,
                                           double learning_rate, std::string* error_message) {
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
  env["d_model"] = impl_->core_->d_model();
  env["num_ternary_blocks"] = impl_->core_->num_blocks();
  env["io_d_model"] = impl_->core_->io_d_model();
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
  auto st = encode_safetensors_f32(impl_->tensors_for_save(), error_message);
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

double LibTorchTpemTrainer::train_step(std::size_t batch_size, std::uint64_t step_mix) {
  return impl_->train_step(batch_size, step_mix);
}

double LibTorchTpemTrainer::eval_step(std::size_t batch_size, std::uint64_t step_mix) {
  return impl_->eval_step(batch_size, step_mix);
}

void LibTorchTpemTrainer::set_learning_rate(double lr) { impl_->set_learning_rate(lr); }

LibTorchTpemTrainer::~LibTorchTpemTrainer() = default;

}  // namespace qminiwasm::training
