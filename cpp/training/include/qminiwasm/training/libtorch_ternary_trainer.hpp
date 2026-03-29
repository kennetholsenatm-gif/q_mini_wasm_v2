#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace qminiwasm::training {

/// LibTorch-backed TPEM Phase-1 trainer: residual PreNorm stack of ternary STE experts
/// (``d_model``→``d_model``), optional linear stem/head when ``io_d_model != d_model``, plus frozen
/// ``quantum_router`` tensor snapshot (pass-through on save). Interchange uses format v2
/// (``QMWTPEM2`` + JSON envelope + safetensors blob); see ``cpp/training/README.md``.
class LibTorchTpemTrainer {
 public:
  static std::unique_ptr<LibTorchTpemTrainer> create(double learning_rate, std::uint64_t seed,
                                                     std::string* error_message);

  /// Cold-start geometry when no interchange file is loaded (random init). Ignored after
  /// ``load_interchange`` replaces the core.
  [[nodiscard]] bool init_geometry(std::int64_t d_model, std::int64_t io_d_model, int num_ternary_blocks,
                                   std::string* error_message);

  [[nodiscard]] bool load_interchange(const std::string& path, std::string* error_message);
  [[nodiscard]] bool save_interchange(const std::string& path, const std::string& run_id,
                                      std::size_t epoch_1based, double train_loss, double val_loss,
                                      double learning_rate, std::string* error_message);

  /// Single Adam step on random synthetic (B, io_d_model) data; returns train loss (MSE).
  [[nodiscard]] double train_step(std::size_t batch_size, std::uint64_t step_mix);

  /// Eval MSE on a fresh random batch without optimizer step.
  [[nodiscard]] double eval_step(std::size_t batch_size, std::uint64_t step_mix);

  void set_learning_rate(double lr);

  ~LibTorchTpemTrainer();

 private:
  LibTorchTpemTrainer() = default;

  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace qminiwasm::training
