#include "qminiwasm/training/bloch_attention.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

std::filesystem::path golden_json_path() {
  const std::filesystem::path here{__FILE__};
  return here.parent_path() / "data" / "bloch_golden.json";
}

torch::Tensor tensor_from_json_floats(const nlohmann::json& j,
                                      const std::vector<std::int64_t>& shape) {
  auto v = j.get<std::vector<float>>();
  int64_t n = 1;
  for (auto d : shape) {
    n *= d;
  }
  TORCH_CHECK(n == static_cast<std::int64_t>(v.size()), "json float count mismatch shape");
  auto t = torch::empty(shape, torch::dtype(torch::kFloat32));
  std::memcpy(t.data_ptr<float>(), v.data(), static_cast<std::size_t>(v.size()) * sizeof(float));
  return t;
}

}  // namespace

int main() {
  const auto path = golden_json_path();
  std::ifstream inf(path);
  if (!inf) {
    std::cerr << "bloch_golden_test: missing " << path.string() << "\n";
    return 2;
  }
  nlohmann::json j;
  inf >> j;

  const int d_model = j.at("d_model").get<int>();
  const int num_heads = j.at("num_heads").get<int>();
  const int d_value = j.at("d_value").get<int>();
  const int b = j.at("B").get<int>();
  const int t = j.at("T").get<int>();

  BlochSphereAttention attn(d_model, num_heads, d_value);
  torch::NoGradGuard ng;

  auto load_named = [&](const std::string& px, torch::nn::Linear& layer) {
    auto wt = tensor_from_json_floats(j.at(px + "_weight"), layer->weight.sizes().vec());
    auto bs = tensor_from_json_floats(j.at(px + "_bias"), layer->bias.sizes().vec());
    layer->weight.copy_(wt);
    layer->bias.copy_(bs);
  };
  load_named("q_proj", attn->q_proj_);
  load_named("k_proj", attn->k_proj_);
  load_named("v_proj", attn->v_proj_);
  load_named("out_proj", attn->out_proj_);

  auto x = tensor_from_json_floats(j.at("x"), {b, t, d_model});
  auto want = tensor_from_json_floats(j.at("y"), {b, t, d_model});

  attn->eval();
  auto got = attn->forward(x);
  auto diff = (got - want).abs().max().item<double>();
  const double tol = 1e-5;
  if (!(diff <= tol) || std::isnan(diff)) {
    std::cerr << "bloch_golden_test: max abs diff " << diff << " (tol " << tol << ")\n";
    return 1;
  }
  return 0;
}
