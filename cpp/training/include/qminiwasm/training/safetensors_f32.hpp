#pragma once

#include <map>
#include <optional>
#include <string>
#include <unordered_map>

#include <torch/torch.h>

namespace qminiwasm::training {

/// Encode float32 CPU contiguous tensors as a standalone safetensors blob (sorted keys).
[[nodiscard]] std::optional<std::string> encode_safetensors_f32(
    const std::map<std::string, torch::Tensor>& tensors, std::string* error_message);

/// Decode a safetensors blob; tensors are CPU float32.
[[nodiscard]] bool decode_safetensors_f32(std::string_view bytes,
                                            std::unordered_map<std::string, torch::Tensor>* out,
                                            std::string* error_message);

}  // namespace qminiwasm::training
