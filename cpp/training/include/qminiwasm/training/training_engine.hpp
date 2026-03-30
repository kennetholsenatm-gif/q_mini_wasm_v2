#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "qminiwasm/training/taxonomy_tier.hpp"
#include "qminiwasm/training/training_tracker.hpp"

namespace qminiwasm::training {

struct HfDatasetParamsNative {
  std::string dataset_id;
  /// Empty: resolved at fetch time via datasets-server /info (never assume "default" for multi-config sets).
  std::string config_name;
  std::string split = "train";
  std::string revision;
  std::uint32_t num_samples = 0;
  double mesh_blend_fraction = 0.0;
};

struct CascadeCurriculumLoopNative {
  bool enabled = false;
  std::uint32_t max_heal_rounds = 3;
  std::string teacher_checkpoint_path;
  std::uint32_t ptqtp_num_planes = 2;
  double gate_target_val_mse = 0.0;
  double gate_max_tpem_mib = 0.0;
  bool run_taxonomy_linter = false;
  double heal_learning_rate_scale = 0.5;
  double teacher_epoch_fraction = 0.5;
  std::uint32_t heal_epochs_per_round = 2;
  /// Gate fail: reload teacher checkpoint and repeat PTQTP+heal (1 = single macro pass).
  std::uint32_t max_curriculum_cycles = 1;
};
struct TrainingPhaseNative {
  std::string name;
  std::size_t epochs = 1;
  bool supervised = true;
  bool cascade_rl = true;
  bool freeze_model_backbone = false;
  bool router_only = false;
  std::string cascade_policy_optimizer;
  std::optional<double> cispo_clip_epsilon;
  std::optional<double> cascade_mopd_lambda;
  std::optional<double> tequila_deadzone;
  bool freeze_ternary_experts = false;
  std::string mopd_teacher_checkpoint_path;
};

/// Active unified-matrix phase for global epoch ``g`` (0-based). Empty ``phases`` => nullptr.
const TrainingPhaseNative* phase_at_global_epoch(std::size_t global_epoch,
                                                 const std::vector<TrainingPhaseNative>& phases);

/// Effective policy: ``grpo`` or ``cispo`` (lowercase).
std::string effective_cascade_policy(const TrainingPhaseNative* phase, const std::string& root_policy);

struct TrainingConfig {
  std::string run_id;
  std::size_t epochs = 1;
  std::size_t batch_size = 64;
  std::size_t micro_batch_size = 16;
  std::size_t worker_threads = 4;
  std::size_t prefetch_depth = 4;
  std::size_t compute_slots = 2;
  std::size_t classes = 2;
  std::size_t samples_per_class = 128;
  std::uint64_t seed = 42;
  double learning_rate = 1e-3;
  TaxonomyTier taxonomy_tier = TaxonomyTier::kEdgeConstrained;
  bool enable_enclave_adapter = false;
  /// Optional path to trainable TPEM **interchange v2** (``QMWTPEM2`` + safetensors). Empty: random init
  /// at default 4096/4096/1 unless ``native_*`` below are set (gRPC / TOML cold start).
  std::string model_uri;
  /// Native LibTorch cold-start geometry when ``model_uri`` is empty. Zero = use built-in default.
  std::uint32_t native_d_model = 0;
  std::uint32_t native_io_d_model = 0;
  std::uint32_t native_num_ternary_blocks = 0;
  /// Absolute paths for checkpoints. With LibTorch enabled, native engine writes interchange v2 (``.pt``).
  std::string checkpoint_save_path;
  std::string checkpoint_best_path;
  std::string checkpoint_latest_path;
  HfDatasetParamsNative hf;
  CascadeCurriculumLoopNative cascade_loop;
  bool use_native_engine_only = false;
  std::vector<TrainingPhaseNative> training_phases;
  std::string cascade_policy_optimizer = "grpo";
  double cispo_clip_epsilon = 0.2;
  std::string attention_backend;
  /// Native toy cascade RL (GRPO/CISPO): number of trajectories per step; from TOML [cascade] group_size.
  std::size_t cascade_rl_group_size = 4;
};

enum class EngineState {
  kIdle = 0,
  kStarting = 1,
  kRunning = 2,
  kStopping = 3,
  kStopped = 4,
  kFailed = 5,
};

struct TelemetryEvent {
  std::string run_id;
  std::uint64_t unix_ms = 0;
  std::size_t epoch = 0;
  std::size_t step = 0;
  double train_loss = 0.0;
  double val_loss = 0.0;
  double learning_rate = 0.0;
  double samples_per_second = 0.0;
  std::size_t sampler_queue_depth = 0;
  std::size_t prefetch_queue_depth = 0;
  std::size_t compute_queue_depth = 0;
  TaxonomyTier taxonomy_tier = TaxonomyTier::kEdgeConstrained;
  PrecisionMode precision_mode = PrecisionMode::kTernary;
  std::string stage;
  std::string event_type;
  std::string message;
  std::string graph_id;
  std::string node_id;
  std::string enclave_state;
  std::string attestation_state;
  double decoherence_score = 0.0;
  /// Epoch summary (native); used when ``event_type`` is ``epoch_throughput``.
  double epoch_wall_s = 0.0;
  std::uint32_t epoch_batch_count = 0;
  std::uint64_t epoch_sample_count = 0;
  double epoch_mean_samples_per_s = 0.0;
  double host_rss_mib = 0.0;
  /// Enclave / footprint (e.g. ``enclave_summary``).
  double estimated_tpem_mib = 0.0;
  double tier_cap_mib = 0.0;
  std::string training_phase;
  std::string cascade_policy_optimizer;
};

struct EngineStatus {
  EngineState state = EngineState::kIdle;
  std::string run_id;
  std::size_t epoch = 0;
  std::size_t step = 0;
  double train_loss = 0.0;
  double val_loss = 0.0;
  double learning_rate = 0.0;
  std::string message;
};

using TelemetryCallback = std::function<void(const TelemetryEvent&)>;

class TrainingEngine {
 public:
  TrainingEngine();
  ~TrainingEngine();

  TrainingEngine(const TrainingEngine&) = delete;
  TrainingEngine& operator=(const TrainingEngine&) = delete;

  bool start(const TrainingConfig& config, std::string* error_message);
  void request_stop();

  EngineStatus status() const;
  bool pop_telemetry(TelemetryEvent* out_event, std::uint64_t timeout_ms);
  void set_telemetry_callback(TelemetryCallback callback);

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace qminiwasm::training
