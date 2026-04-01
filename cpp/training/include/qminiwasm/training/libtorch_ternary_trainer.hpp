#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>

#include <torch/torch.h>

namespace qminiwasm::training {

/// CPU weight snapshot for interchange v2; safe to encode/write without holding the trainer mutex.
struct InterchangeTensorSnapshot {
  std::map<std::string, torch::Tensor> tensors;
  std::int64_t d_model = 0;
  std::int64_t io_d_model = 0;
  int num_ternary_blocks = 0;
  int native_bloch_seq_len = 0;
  int native_bloch_num_heads = 0;
};

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
                                   int native_bloch_seq_len, int native_bloch_num_heads,
                                   std::string* error_message);

  /// Load interchange v2. When ``attention_backend_is_bloch``, ``native_bloch_*`` select Bloch geometry (defaults 8×4
  /// when zero); otherwise the rebuilt core has no Bloch block even if the file contains Bloch tensors.
  [[nodiscard]] bool load_interchange(const std::string& path, bool attention_backend_is_bloch,
                                      std::uint32_t native_bloch_seq_len, std::uint32_t native_bloch_num_heads,
                                      std::string* error_message);
  [[nodiscard]] bool save_interchange(const std::string& path, const std::string& run_id,
                                      std::size_t epoch_1based, double train_loss, double val_loss,
                                      double learning_rate, std::string* error_message);

  /// Call only while holding the engine ``libtorch_mu_``; copies weights to CPU then returns.
  [[nodiscard]] InterchangeTensorSnapshot capture_interchange_tensors() const;

  /// Serialize snapshot to disk (encode + write); does not touch the live module (no trainer lock).
  [[nodiscard]] static bool write_interchange_to_path(const std::string& path, const std::string& run_id,
                                                      std::size_t epoch_1based, double train_loss, double val_loss,
                                                      double learning_rate, InterchangeTensorSnapshot snap,
                                                      std::string* error_message);

  /// Single Adam step on random synthetic (B, io_d_model) data; returns train loss (MSE).
  [[nodiscard]] double train_step(std::size_t batch_size, std::uint64_t step_mix);

  /// Eval MSE on a fresh random batch without optimizer step.
  [[nodiscard]] double eval_step(std::size_t batch_size, std::uint64_t step_mix);

  /// Supervised MSE step using caller-provided row-major ``[batch, io_dim]`` tensors (CPU float).
  [[nodiscard]] double train_step_supervised(std::size_t batch_size, std::int64_t io_dim,
                                             const float* x_row_major, const float* target_row_major,
                                             std::uint64_t step_mix);
  [[nodiscard]] double eval_step_supervised(std::size_t batch_size, std::int64_t io_dim,
                                            const float* x_row_major, const float* target_row_major,
                                            std::uint64_t step_mix);

  /// Frozen teacher copy of ``core_`` for distillation (same device: CPU).
  [[nodiscard]] bool clone_teacher_from_core(std::string* error_message);

  /// Load external FP32 teacher from interchange checkpoint (real distillation target, not a clone).
  /// The teacher must have matching geometry (d_model, io_d_model, num_blocks) to the student core.
  [[nodiscard]] bool load_teacher_from_interchange(const std::string& path,
                                                    std::uint32_t native_bloch_seq_len,
                                                    std::uint32_t native_bloch_num_heads,
                                                    std::string* error_message);
  void reset_teacher();
  /// Replace expert latent weights with greedy PTQTP reconstruction (post-training shrink).
  [[nodiscard]] bool apply_ptqtp_reconstruct_experts(int num_planes, std::string* error_message);

  /// Simulated Annealing quantization: Metropolis-Hastings cooling to freeze weights to ternary lattice.
  /// Acceptance: min(1, exp(-ΔE/T)) where ΔE = |ternary(w') - w'|² - |ternary(w) - w|².
  /// Returns acceptance rate for telemetry monitoring.
  [[nodiscard]] bool apply_simulated_annealing(double temperature, double cool_rate, double min_temperature,
                                               double target_acceptance, std::uint64_t step_mix,
                                               double* out_acceptance_rate, std::string* error_message);
  /// Distillation: MSE(student, teacher) + lambda_teacher * MSE(student, target).
  [[nodiscard]] double train_step_distill(std::size_t batch_size, std::int64_t io_dim,
                                          const float* x_row_major, const float* target_row_major,
                                          double lambda_teacher, std::uint64_t step_mix);

  /// Reverse-KL distillation: D_KL(q_student || p_teacher) + lambda_target * MSE(student, target).
  /// Mode-seeking distillation that focuses on teacher's high-density modes.
  /// Prefer when student is discrete (e.g., ternary) and teacher is continuous (FP32).
  [[nodiscard]] double train_step_distill_reverse_kl(std::size_t batch_size, std::int64_t io_dim,
                                                      const float* x_row_major, const float* target_row_major,
                                                      double lambda_kl, double lambda_target,
                                                      std::uint64_t step_mix);

  /// Toy cascade MDP (8-D state, 4 actions, 16 steps) + ``CascadeToyPolicy`` (Python ``TinyCascadePolicy``).
  /// Matches ``cascade_rl_train_step`` + ``CascadeGRPO``; use ``group_size >= 2`` for normalized advantages.
  [[nodiscard]] double train_step_cascade_grpo(std::size_t group_size, std::uint64_t step_mix);

  /// Same MDP; CISPO clipped ratio with detached clip coefficient (Python ``CascadeCISPO``).
  [[nodiscard]] double train_step_cascade_cispo(std::size_t group_size, double epsilon, std::uint64_t step_mix);

  /// Single step: MSE on ``CoreModule`` (synthetic batch, same seed contract as ``train_step``) plus
  /// ``cascade_lambda`` times toy GRPO or CISPO loss on ``CascadeToyPolicy``; one backward each parameter set,
  /// then both Adam steps.
  [[nodiscard]] double train_step_joint_supervised_cascade(std::size_t batch_size, std::size_t group_size,
                                                           double cascade_lambda, bool use_cispo, double cispo_epsilon,
                                                           std::uint64_t step_mix);

  void set_learning_rate(double lr);

  ~LibTorchTpemTrainer();

 private:
  LibTorchTpemTrainer() = default;

  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace qminiwasm::training
