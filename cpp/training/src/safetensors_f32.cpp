#include "qminiwasm/training/safetensors_f32.hpp"

#include <nlohmann/json.hpp>

#include <cstring>

namespace qminiwasm::training {
namespace {

void set_error(std::string* error_message, std::string_view text) {
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

}  // namespace

std::optional<std::string> encode_safetensors_f32(const std::map<std::string, torch::Tensor>& tensors,
                                                  std::string* error_message) {
  nlohmann::json header = nlohmann::json::object();
  std::string payload;
  std::uint64_t offset = 0;
  for (const auto& [name, tensor_in] : tensors) {
    if (name.empty()) {
      set_error(error_message, "safetensors: empty tensor name");
      return std::nullopt;
    }
    torch::Tensor t = tensor_in.detach().cpu().contiguous().to(torch::kFloat32);
    if (t.scalar_type() != torch::kFloat32) {
      set_error(error_message, "safetensors: internal dtype error");
      return std::nullopt;
    }
    const auto numel = static_cast<std::size_t>(t.numel());
    const std::size_t nbytes = numel * sizeof(float);
    nlohmann::json shape = nlohmann::json::array();
    for (int i = 0; i < t.dim(); ++i) {
      shape.push_back(static_cast<std::int64_t>(t.size(i)));
    }
    const std::uint64_t end = offset + static_cast<std::uint64_t>(nbytes);
    header[name] = {{"dtype", "F32"}, {"shape", std::move(shape)}, {"data_offsets", {offset, end}}};
    const float* data = t.data_ptr<float>();
    payload.append(reinterpret_cast<const char*>(data), static_cast<std::size_t>(nbytes));
    offset = end;
  }

  std::string header_str = header.dump();
  const std::size_t header_size = header_str.size();
  const std::size_t padded_header_size = (header_size + 7) & ~std::size_t{7};
  std::string padding(padded_header_size - header_size, ' ');

  std::string out;
  out.reserve(8 + padded_header_size + payload.size());
  const std::uint64_t n_header = static_cast<std::uint64_t>(padded_header_size);
  unsigned char len_le[8];
  for (int i = 0; i < 8; ++i) {
    len_le[i] = static_cast<unsigned char>((n_header >> (8 * i)) & 0xFF);
  }
  out.append(reinterpret_cast<const char*>(len_le), 8);
  out.append(header_str);
  out.append(padding);
  out.append(payload);
  return out;
}

bool decode_safetensors_f32(std::string_view bytes, std::unordered_map<std::string, torch::Tensor>* out,
                            std::string* error_message) {
  if (out == nullptr) {
    set_error(error_message, "safetensors: null output map");
    return false;
  }
  out->clear();
  if (bytes.size() < 8) {
    set_error(error_message, "safetensors: truncated header length");
    return false;
  }
  const auto* u = reinterpret_cast<const unsigned char*>(bytes.data());
  const std::uint64_t header_len = read_u64_le(u);
  if (header_len > bytes.size() - 8) {
    set_error(error_message, "safetensors: invalid header length");
    return false;
  }
  std::string header_json(bytes.substr(8, static_cast<std::size_t>(header_len)));
  nlohmann::json header;
  try {
    header = nlohmann::json::parse(header_json);
  } catch (const std::exception& e) {
    set_error(error_message, std::string("safetensors: JSON parse error: ") + e.what());
    return false;
  }
  if (!header.is_object()) {
    set_error(error_message, "safetensors: header must be object");
    return false;
  }
  std::size_t data_start = 8 + static_cast<std::size_t>(header_len);
  data_start = (data_start + 7) & ~std::size_t{7};
  if (data_start > bytes.size()) {
    set_error(error_message, "safetensors: invalid padding");
    return false;
  }
  const char* base = bytes.data();
  for (auto it = header.begin(); it != header.end(); ++it) {
    const std::string& name = it.key();
    if (name == "__metadata__") {
      continue;
    }
    const auto& desc = it.value();
    if (!desc.is_object()) {
      set_error(error_message, "safetensors: invalid tensor descriptor");
      return false;
    }
    const std::string dtype = desc.value("dtype", "");
    if (dtype != "F32") {
      set_error(error_message, "safetensors: only F32 tensors supported (got " + dtype + ")");
      return false;
    }
    const auto& shape_j = desc.at("shape");
    const auto& off_j = desc.at("data_offsets");
    if (!shape_j.is_array() || !off_j.is_array() || off_j.size() != 2) {
      set_error(error_message, "safetensors: invalid shape/offsets");
      return false;
    }
    std::vector<std::int64_t> shape;
    for (const auto& el : shape_j) {
      shape.push_back(el.get<std::int64_t>());
    }
    const std::uint64_t o0 = off_j[0].get<std::uint64_t>();
    const std::uint64_t o1 = off_j[1].get<std::uint64_t>();
    if (o1 < o0) {
      set_error(error_message, "safetensors: invalid data_offsets");
      return false;
    }
    const std::size_t start = data_start + static_cast<std::size_t>(o0);
    const std::size_t end = data_start + static_cast<std::size_t>(o1);
    if (end > bytes.size() || start > end) {
      set_error(error_message, "safetensors: tensor slice out of range");
      return false;
    }
    const std::size_t count = (end - start) / sizeof(float);
    if ((end - start) % sizeof(float) != 0) {
      set_error(error_message, "safetensors: tensor byte length not multiple of 4");
      return false;
    }
    std::int64_t expected = 1;
    for (std::int64_t d : shape) {
      expected *= d;
    }
    if (expected < 0 || static_cast<std::size_t>(expected) != count) {
      set_error(error_message, "safetensors: shape/byte length mismatch");
      return false;
    }
    std::vector<float> buf(count);
    std::memcpy(buf.data(), base + start, count * sizeof(float));
    auto opts = torch::TensorOptions().dtype(torch::kFloat32).device(torch::kCPU);
    torch::Tensor t = torch::from_blob(buf.data(), shape, opts).clone();
    (*out)[name] = std::move(t);
  }
  return true;
}

}  // namespace qminiwasm::training
