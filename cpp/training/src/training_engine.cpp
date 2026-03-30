#include "qminiwasm/training/training_engine.hpp"

#include "qminiwasm/training/native_checkpoint.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cmath>
#include <optional>
#include <condition_variable>
#include <cstdlib>
#include <exception>
#include <limits>
#include <mutex>
#include <numeric>
#include <random>
#include <stop_token>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "internal/adapter_interfaces.hpp"
#include "internal/balanced_sampler.hpp"
#include "internal/bounded_queue.hpp"
#include "internal/telemetry_bus.hpp"

#ifdef QMINIWASM_HAS_LIBTORCH_TRAINING
#include "qminiwasm/training/hf_datasets_rows.hpp"
#include "qminiwasm/training/libtorch_ternary_trainer.hpp"
#endif

namespace qminiwasm::training {

namespace {
using Clock = std::chrono::steady_clock;

std::uint64_t now_unix_ms() {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
          .count());
}

struct PipelineBatch {
  std::size_t epoch = 0;
  std::size_t step = 0;
  std::vector<internal::SampleRef> samples;
};

struct ComputePacket {
  std::size_t epoch = 0;
  std::size_t step = 0;
  double train_loss = 0.0;
  double val_loss = 0.0;
  double samples_per_second = 0.0;
  /// True when active ``training_phases`` row has ``supervised=false`` (eval-only, no optimizer step).
  bool eval_only_supervised_skipped = false;
  /// True when active phase is RL-only (``!supervised && cascade_rl``); LibTorch toy GRPO/CISPO step.
  bool cascade_rl_native = false;
  bool cascade_rl_is_cispo = false;
};

std::string trim_ascii_copy(std::string s) {
  const auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
  s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
  return s;
}

void ascii_tolower_inplace(std::string* s) {
  for (auto& c : *s) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
}

std::string resolve_cascade_policy(const std::string& phase_field, const std::string& root_field) {
  std::string p = trim_ascii_copy(phase_field);
  ascii_tolower_inplace(&p);
  if (!p.empty()) {
    return p;
  }
  std::string r = trim_ascii_copy(root_field);
  ascii_tolower_inplace(&r);
  return r.empty() ? "grpo" : r;
}

std::optional<std::string> reject_unimplemented_unified_matrix(const TrainingConfig& cfg) {
  std::string ab = trim_ascii_copy(cfg.attention_backend);
  ascii_tolower_inplace(&ab);
  if (ab == "bloch") {
    return std::string(
        "native training engine: attention_backend=bloch is not implemented (use Python / "
        "BlochSphereAttention or attention_backend=tropical)");
  }
  return std::nullopt;
}

void attach_training_phase_telemetry(TelemetryEvent* ev, const TrainingConfig& cfg) {
  if (ev == nullptr) {
    return;
  }
  if (cfg.cascade_loop.enabled) {
    ev->training_phase.clear();
    ev->cascade_policy_optimizer = resolve_cascade_policy("", cfg.cascade_policy_optimizer);
    return;
  }
  if (cfg.training_phases.empty()) {
    ev->training_phase.clear();
    ev->cascade_policy_optimizer = resolve_cascade_policy("", cfg.cascade_policy_optimizer);
    return;
  }
  const TrainingPhaseNative* ph = phase_at_global_epoch(ev->epoch, cfg.training_phases);
  if (ph != nullptr) {
    ev->training_phase = ph->name;
    ev->cascade_policy_optimizer = effective_cascade_policy(ph, cfg.cascade_policy_optimizer);
  }
}
}  // namespace


class TrainingEngine::Impl {
 public:
  Impl() : tracker_(64), state_(EngineState::kIdle) {}

  ~Impl() { request_stop(); }

  bool start(const TrainingConfig& config, std::string* error_message) {
    std::lock_guard<std::recursive_mutex> lock(mu_);
    if (state_ == EngineState::kStarting || state_ == EngineState::kRunning) {
      if (error_message != nullptr) {
        *error_message = "engine is already running";
      }
      return false;
    }

    telemetry_bus_.reopen();

    config_ = config;
    if (const auto rej = reject_unimplemented_unified_matrix(config_); rej.has_value()) {
      if (error_message != nullptr) {
        *error_message = *rej;
      }
      return false;
    }
    if (config_.run_id.empty()) {
      config_.run_id = "run-cpp-foundation";
    }
    best_val_loss_for_checkpoint_ = std::numeric_limits<double>::infinity();
    last_completed_train_loss_ = 0.0;
    last_completed_val_loss_ = 0.0;
    last_completed_epoch_ = 0;
    pipeline_failed_.store(false, std::memory_order_release);
    policy_ = make_taxonomy_policy(config_.taxonomy_tier, config_.worker_threads);
    if (config_.prefetch_depth == 0) {
      config_.prefetch_depth = policy_.prefetch_depth;
    }
    if (config_.compute_slots == 0) {
      config_.compute_slots = policy_.compute_slots;
    }

    if (config_.cascade_loop.enabled) {
#ifndef QMINIWASM_HAS_LIBTORCH_TRAINING
      if (error_message != nullptr) {
        *error_message = "cascade_curriculum_loop requires LibTorch (QMINIWASM_TRAINING_WITH_LIBTORCH=ON)";
      }
      return false;
#endif
    }

#ifdef QMINIWASM_HAS_LIBTORCH_TRAINING
    config_.compute_slots = 1;
    {
      std::string lib_err;
      libtorch_trainer_ = LibTorchTpemTrainer::create(config_.learning_rate, config_.seed, &lib_err);
      std::lock_guard<std::mutex> tlock(libtorch_mu_);
      if (!config_.model_uri.empty()) {
        if (!libtorch_trainer_->load_interchange(config_.model_uri, &lib_err)) {
          if (error_message != nullptr) {
            *error_message = lib_err;
          }
          return false;
        }
      } else if (config_.native_d_model > 0 || config_.cascade_loop.enabled) {
        std::uint32_t d0 = config_.native_d_model;
        std::uint32_t io0 = config_.native_io_d_model;
        std::uint32_t nb0 = config_.native_num_ternary_blocks;
        if (config_.cascade_loop.enabled && d0 == 0) {
          d0 = 256;
          io0 = 256;
          nb0 = 1;
        }
        const auto d = static_cast<std::int64_t>(d0);
        const auto io = io0 > 0 ? static_cast<std::int64_t>(io0) : d;
        const int nb = nb0 > 0 ? static_cast<int>(nb0) : 1;
        if (!libtorch_trainer_->init_geometry(d, io, nb, &lib_err)) {
          if (error_message != nullptr) {
            *error_message = lib_err;
          }
          return false;
        }
      }
    }
#endif

    stop_source_ = std::stop_source{};
    state_ = EngineState::kStarting;
    status_ = EngineStatus{
        .state = EngineState::kStarting,
        .run_id = config_.run_id,
        .epoch = 0,
        .step = 0,
        .train_loss = 0.0,
        .val_loss = 0.0,
        .learning_rate = config_.learning_rate,
        .message = "starting workers",
    };
    workers_.clear();

#ifdef QMINIWASM_HAS_LIBTORCH_TRAINING
    if (config_.cascade_loop.enabled) {
      {
        double est_mib = 0.0;
        const double d = static_cast<double>(config_.native_d_model > 0 ? config_.native_d_model : 256);
        const int nb = config_.native_num_ternary_blocks > 0 ? static_cast<int>(config_.native_num_ternary_blocks) : 1;
        est_mib = (3.0 * d * d * static_cast<double>(nb) * 4.0) / (1024.0 * 1024.0);
        emit(TelemetryEvent{
            .run_id = config_.run_id,
            .unix_ms = now_unix_ms(),
            .epoch = 0,
            .step = 0,
            .train_loss = 0.0,
            .val_loss = 0.0,
            .learning_rate = config_.learning_rate,
            .samples_per_second = 0.0,
            .sampler_queue_depth = 0,
            .prefetch_queue_depth = 0,
            .compute_queue_depth = 0,
            .taxonomy_tier = policy_.tier,
            .precision_mode = policy_.precision,
            .stage = "cascade_setup",
            .event_type = "enclave_summary",
            .message = "native_cascade_curriculum_loop_cpp",
            .enclave_state = "native",
            .attestation_state = "n_a",
            .estimated_tpem_mib = est_mib,
            .tier_cap_mib = config_.cascade_loop.gate_max_tpem_mib > 0 ? config_.cascade_loop.gate_max_tpem_mib : -1.0,
        });
      }
      workers_.emplace_back([this](std::stop_token t) { run_cascade_curriculum(t); });
      return true;
    }
#endif

    sampler_to_prefetch_ =
        std::make_unique<internal::BoundedQueue<PipelineBatch>>(std::max<std::size_t>(2, config_.prefetch_depth * 2));
    prefetch_to_compute_ =
        std::make_unique<internal::BoundedQueue<PipelineBatch>>(std::max<std::size_t>(2, config_.prefetch_depth * 2));
    compute_to_update_ =
        std::make_unique<internal::BoundedQueue<ComputePacket>>(std::max<std::size_t>(2, config_.compute_slots * 2));

    {
      double est_mib = 0.0;
#ifdef QMINIWASM_HAS_LIBTORCH_TRAINING
      if (config_.native_d_model > 0) {
        const double d = static_cast<double>(config_.native_d_model);
        const int nb = config_.native_num_ternary_blocks > 0 ? static_cast<int>(config_.native_num_ternary_blocks) : 1;
        est_mib = (3.0 * d * d * static_cast<double>(nb) * 4.0) / (1024.0 * 1024.0);
      }
#endif
      emit(TelemetryEvent{
          .run_id = config_.run_id,
          .unix_ms = now_unix_ms(),
          .epoch = 0,
          .step = 0,
          .train_loss = 0.0,
          .val_loss = 0.0,
          .learning_rate = config_.learning_rate,
          .samples_per_second = 0.0,
          .sampler_queue_depth = 0,
          .prefetch_queue_depth = 0,
          .compute_queue_depth = 0,
          .taxonomy_tier = policy_.tier,
          .precision_mode = policy_.precision,
          .stage = "training_setup",
          .event_type = "enclave_summary",
          .message = "native_engine_synthetic_sampler_not_hf_tabular",
          .enclave_state = "native",
          .attestation_state = "n_a",
          .estimated_tpem_mib = est_mib,
          .tier_cap_mib = -1.0,
      });
    }
    launch_workers();
    return true;
  }

  void request_stop() {
    stop_source_.request_stop();
    if (sampler_to_prefetch_) sampler_to_prefetch_->close();
    if (prefetch_to_compute_) prefetch_to_compute_->close();
    if (compute_to_update_) compute_to_update_->close();
    for (std::jthread& worker : workers_) {
      if (worker.joinable()) {
        worker.join();
      }
    }
    workers_.clear();
    telemetry_bus_.close();
    std::lock_guard<std::recursive_mutex> lock(mu_);
    if (state_ != EngineState::kFailed) {
      state_ = EngineState::kStopped;
      status_.state = EngineState::kStopped;
      status_.message = "stopped";
    }
  }

  EngineStatus status() const {
    std::lock_guard<std::recursive_mutex> lock(mu_);
    return status_;
  }

  bool pop_telemetry(TelemetryEvent* out_event, std::uint64_t timeout_ms) {
    if (out_event == nullptr) {
      return false;
    }
    auto event = telemetry_bus_.pop_for(timeout_ms);
    if (!event.has_value()) {
      return false;
    }
    *out_event = std::move(*event);
    return true;
  }

  void set_telemetry_callback(TelemetryCallback callback) {
    std::lock_guard<std::recursive_mutex> lock(mu_);
    callback_ = std::move(callback);
  }

 private:
  void run_cascade_curriculum(std::stop_token token);
  void emit(TelemetryEvent event) {
    attach_training_phase_telemetry(&event, config_);
    telemetry_bus_.push(event);
    std::lock_guard<std::recursive_mutex> lock(mu_);
    if (callback_) {
      callback_(event);
    }
  }

  void set_status(EngineStatus next) {
    std::lock_guard<std::recursive_mutex> lock(mu_);
    status_ = std::move(next);
    state_ = status_.state;
  }

  /// First LibTorch/compute failure wins; closes queues so sibling workers exit without joining self.
  void fail_pipeline_from_compute(const char* context, const std::string& reason) {
    const bool first = !pipeline_failed_.exchange(true, std::memory_order_acq_rel);
    if (!first) {
      return;
    }
    std::string msg = reason;
    constexpr std::size_t kMax = 1024;
    if (msg.size() > kMax) {
      msg.resize(kMax);
      msg += "...";
    }
    if (context != nullptr && context[0] != '\0') {
      msg = std::string(context) + ": " + msg;
    }
    set_status(EngineStatus{
        .state = EngineState::kFailed,
        .run_id = config_.run_id,
        .epoch = 0,
        .step = 0,
        .train_loss = 0.0,
        .val_loss = 0.0,
        .learning_rate = config_.learning_rate,
        .message = msg,
    });
    emit(TelemetryEvent{
        .run_id = config_.run_id,
        .unix_ms = now_unix_ms(),
        .epoch = 0,
        .step = 0,
        .train_loss = 0.0,
        .val_loss = 0.0,
        .learning_rate = config_.learning_rate,
        .samples_per_second = 0.0,
        .sampler_queue_depth = sampler_to_prefetch_ ? sampler_to_prefetch_->size() : 0,
        .prefetch_queue_depth = prefetch_to_compute_ ? prefetch_to_compute_->size() : 0,
        .compute_queue_depth = compute_to_update_ ? compute_to_update_->size() : 0,
        .taxonomy_tier = policy_.tier,
        .precision_mode = policy_.precision,
        .stage = "compute",
        .event_type = "libtorch_exception",
        .message = msg,
    });
    if (sampler_to_prefetch_) {
      sampler_to_prefetch_->close();
    }
    if (prefetch_to_compute_) {
      prefetch_to_compute_->close();
    }
    if (compute_to_update_) {
      compute_to_update_->close();
    }
  }

  void launch_workers() {
    workers_.emplace_back([this](std::stop_token token) { sampler_stage(token); });
    workers_.emplace_back([this](std::stop_token token) { prefetch_stage(token); });
    const std::size_t compute_workers = std::max<std::size_t>(1, config_.compute_slots);
    for (std::size_t i = 0; i < compute_workers; ++i) {
      workers_.emplace_back([this](std::stop_token token) { compute_stage(token); });
    }
    workers_.emplace_back([this](std::stop_token token) { update_stage(token); });
  }

  void sampler_stage(std::stop_token token) {
    internal::BalancedSampler sampler(config_.classes, config_.samples_per_class, config_.seed);
    const std::size_t total_samples = std::max<std::size_t>(1, config_.classes * config_.samples_per_class);
    const std::size_t steps_per_epoch = std::max<std::size_t>(1, total_samples / std::max<std::size_t>(1, config_.batch_size));
    set_status(EngineStatus{
        .state = EngineState::kRunning,
        .run_id = config_.run_id,
        .epoch = 0,
        .step = 0,
        .train_loss = 0.0,
        .val_loss = 0.0,
        .learning_rate = config_.learning_rate,
        .message = "running",
    });
    if (config_.enable_enclave_adapter) {
      emit(TelemetryEvent{
          .run_id = config_.run_id,
          .unix_ms = now_unix_ms(),
          .epoch = 0,
          .step = 0,
          .train_loss = 0.0,
          .val_loss = 0.0,
          .learning_rate = config_.learning_rate,
          .samples_per_second = 0.0,
          .sampler_queue_depth = 0,
          .prefetch_queue_depth = 0,
          .compute_queue_depth = 0,
          .taxonomy_tier = policy_.tier,
          .precision_mode = policy_.precision,
          .stage = "adapter",
          .event_type = "enclave_adapter_fallback",
          .message = "enclave adapter requested; using default adapter dispatch (foundation no-op)",
      });
    }
    for (std::size_t epoch = 0; epoch < config_.epochs && !token.stop_requested(); ++epoch) {
      sampler.reseed_for_epoch(epoch);
      for (std::size_t step = 0; step < steps_per_epoch && !token.stop_requested(); ++step) {
        PipelineBatch batch{.epoch = epoch, .step = step, .samples = sampler.next_micro_batch(config_.micro_batch_size)};
        if (!sampler_to_prefetch_->push(std::move(batch), token)) {
          break;
        }
      }
    }
    sampler_to_prefetch_->close();
  }

  void prefetch_stage(std::stop_token token) {
    while (!token.stop_requested()) {
      auto maybe = sampler_to_prefetch_->pop(token);
      if (!maybe.has_value()) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      if (!prefetch_to_compute_->push(std::move(*maybe), token)) {
        break;
      }
    }
    prefetch_to_compute_->close();
  }

  void compute_stage(std::stop_token token) {
#ifndef QMINIWASM_HAS_LIBTORCH_TRAINING
    std::mt19937_64 rng(config_.seed + 17);
    std::uniform_real_distribution<double> noise(0.0, 0.005);
#endif
    while (!token.stop_requested()) {
      auto maybe = prefetch_to_compute_->pop(token);
      if (!maybe.has_value()) {
        break;
      }
#ifndef QMINIWASM_HAS_LIBTORCH_TRAINING
      const auto t0 = Clock::now();
#endif
      PipelineBatch batch = std::move(*maybe);
      double train_loss = 0.0;
      double val_loss = 0.0;
#ifdef QMINIWASM_HAS_LIBTORCH_TRAINING
      try {
        const auto t_inner = Clock::now();
        std::lock_guard<std::mutex> lk(libtorch_mu_);
        const std::uint64_t mix =
            (static_cast<std::uint64_t>(batch.epoch) << 32) ^ static_cast<std::uint64_t>(batch.step);
        const TrainingPhaseNative* active_ph = nullptr;
        if (!config_.training_phases.empty() && !config_.cascade_loop.enabled) {
          active_ph = phase_at_global_epoch(batch.epoch, config_.training_phases);
        }
        const bool rl_only =
            active_ph != nullptr && !active_ph->supervised && active_ph->cascade_rl;
        const bool eval_only =
            active_ph != nullptr && !active_ph->supervised && !active_ph->cascade_rl;
        bool cascade_rl_is_cispo = false;
        if (rl_only) {
          const std::string pol = effective_cascade_policy(active_ph, config_.cascade_policy_optimizer);
          cascade_rl_is_cispo = (pol == "cispo");
          double cispo_eps = config_.cispo_clip_epsilon;
          if (active_ph->cispo_clip_epsilon.has_value()) {
            cispo_eps = *active_ph->cispo_clip_epsilon;
          }
          const std::size_t rl_group =
              std::max<std::size_t>(1, config_.cascade_rl_group_size);
          if (cascade_rl_is_cispo) {
            train_loss = libtorch_trainer_->train_step_cascade_cispo(rl_group, cispo_eps, mix);
          } else {
            train_loss = libtorch_trainer_->train_step_cascade_grpo(rl_group, mix);
          }
          val_loss = libtorch_trainer_->eval_step(config_.micro_batch_size, mix ^ 0xCAFEBABECAFECAFEULL);
        } else if (eval_only) {
          val_loss = libtorch_trainer_->eval_step(config_.micro_batch_size, mix);
          train_loss = val_loss;
        } else {
          train_loss = libtorch_trainer_->train_step(config_.micro_batch_size, mix);
          val_loss = libtorch_trainer_->eval_step(config_.micro_batch_size, mix);
        }
        const auto dt_inner = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - t_inner).count();
        const double sps =
            static_cast<double>(batch.samples.size()) / std::max(1.0, static_cast<double>(dt_inner) / 1e6);
        ComputePacket packet{
            .epoch = batch.epoch,
            .step = batch.step,
            .train_loss = train_loss,
            .val_loss = val_loss,
            .samples_per_second = sps,
            .eval_only_supervised_skipped = eval_only,
            .cascade_rl_native = rl_only,
            .cascade_rl_is_cispo = cascade_rl_is_cispo,
        };
        if (!compute_to_update_->push(std::move(packet), token)) {
          break;
        }
        continue;
      } catch (const std::exception& ex) {
        fail_pipeline_from_compute("libtorch", ex.what());
        break;
      } catch (...) {
        fail_pipeline_from_compute("libtorch", "non-std exception");
        break;
      }
#else
      double loss = 0.0;
      if (policy_.tier == TaxonomyTier::kXpuCluster) {
        loss = adapters::run_sycl_compute(batch.samples.size(), policy_.precision);
      } else if (policy_.tier == TaxonomyTier::kFogNode) {
        loss = adapters::run_avx512_compute(batch.samples.size(), policy_.precision);
      } else {
        loss = adapters::run_wasmedge_step(batch.samples.size(), policy_.precision);
      }
      const double epoch_decay = 1.0 / (1.0 + static_cast<double>(batch.epoch));
      train_loss = std::max(0.0001, loss * epoch_decay + noise(rng));
      val_loss = std::max(0.0001, train_loss + 0.005 * (1.0 + static_cast<double>(batch.epoch) * 0.1));
#endif
#ifndef QMINIWASM_HAS_LIBTORCH_TRAINING
      const auto dt = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - t0).count();
      const double sps = static_cast<double>(batch.samples.size()) / std::max(1.0, static_cast<double>(dt) / 1e6);
      ComputePacket packet{
          .epoch = batch.epoch,
          .step = batch.step,
          .train_loss = train_loss,
          .val_loss = val_loss,
          .samples_per_second = sps,
      };
      if (!compute_to_update_->push(std::move(packet), token)) {
        break;
      }
#endif
    }
  }

  void update_stage(std::stop_token token) {
    const std::size_t total_samples = std::max<std::size_t>(1, config_.classes * config_.samples_per_class);
    const std::size_t steps_per_epoch = std::max<std::size_t>(1, total_samples / std::max<std::size_t>(1, config_.batch_size));
    const std::size_t total_pipeline_batches = config_.epochs * steps_per_epoch;
    std::size_t pipeline_packets_done = 0;
    double current_lr = config_.learning_rate;
    double epoch_train_sum = 0.0;
    double epoch_val_sum = 0.0;
    double epoch_sps_sum = 0.0;
    std::size_t epoch_count = 0;
    std::size_t current_epoch = 0;
    auto epoch_wall_start = Clock::now();
    while (!token.stop_requested()) {
      auto maybe = compute_to_update_->pop(token);
      if (!maybe.has_value()) {
        break;
      }
      const ComputePacket packet = std::move(*maybe);
      ++pipeline_packets_done;
      current_epoch = packet.epoch;
      epoch_train_sum += packet.train_loss;
      epoch_val_sum += packet.val_loss;
      epoch_sps_sum += packet.samples_per_second;
      ++epoch_count;

      TelemetryEvent event{
          .run_id = config_.run_id,
          .unix_ms = now_unix_ms(),
          .epoch = packet.epoch,
          .step = packet.step,
          .train_loss = packet.train_loss,
          .val_loss = packet.val_loss,
          .learning_rate = current_lr,
          .samples_per_second = packet.samples_per_second,
          .sampler_queue_depth = sampler_to_prefetch_->size(),
          .prefetch_queue_depth = prefetch_to_compute_->size(),
          .compute_queue_depth = compute_to_update_->size(),
          .taxonomy_tier = policy_.tier,
          .precision_mode = policy_.precision,
          .stage = packet.cascade_rl_native
                       ? (packet.cascade_rl_is_cispo ? "cascade_cispo" : "cascade_grpo")
                   : packet.eval_only_supervised_skipped
                       ? "eval_only"
                       : "update",
          .event_type =
              packet.cascade_rl_native
                  ? (packet.cascade_rl_is_cispo ? "cascade_cispo_native" : "cascade_grpo_native")
                  : packet.eval_only_supervised_skipped ? "phase_supervised_skipped_native" : "step",
          .message = packet.cascade_rl_native
                         ? (packet.cascade_rl_is_cispo ? "native_cascade_cispo_toy_mdp" : "native_cascade_grpo_toy_mdp")
                     : packet.eval_only_supervised_skipped
                         ? "native_eval_only_supervised_false"
                         : "step_completed",
      };
      emit(std::move(event));
      set_status(EngineStatus{
          .state = EngineState::kRunning,
          .run_id = config_.run_id,
          .epoch = packet.epoch,
          .step = packet.step,
          .train_loss = packet.train_loss,
          .val_loss = packet.val_loss,
          .learning_rate = current_lr,
          .message = "running",
      });

      if ((packet.step + 1) >= steps_per_epoch) {
        const EpochMetrics metrics{
            .epoch = packet.epoch,
            .train_loss = epoch_train_sum / static_cast<double>(std::max<std::size_t>(1, epoch_count)),
            .val_loss = epoch_val_sum / static_cast<double>(std::max<std::size_t>(1, epoch_count)),
            .learning_rate = current_lr,
            .samples_per_second = epoch_sps_sum / static_cast<double>(std::max<std::size_t>(1, epoch_count)),
            .elapsed_ms = 0,
        };
        const auto epoch_end_time = Clock::now();
        const double wall_s =
            std::chrono::duration<double>(epoch_end_time - epoch_wall_start).count();
        epoch_wall_start = epoch_end_time;
        const std::uint64_t epoch_samples =
            static_cast<std::uint64_t>(epoch_count) * static_cast<std::uint64_t>(config_.batch_size);
        emit(TelemetryEvent{
            .run_id = config_.run_id,
            .unix_ms = now_unix_ms(),
            .epoch = packet.epoch,
            .step = packet.step,
            .train_loss = metrics.train_loss,
            .val_loss = metrics.val_loss,
            .learning_rate = current_lr,
            .samples_per_second = metrics.samples_per_second,
            .sampler_queue_depth = sampler_to_prefetch_->size(),
            .prefetch_queue_depth = prefetch_to_compute_->size(),
            .compute_queue_depth = compute_to_update_->size(),
            .taxonomy_tier = policy_.tier,
            .precision_mode = policy_.precision,
            .stage = "throughput",
            .event_type = "epoch_throughput",
            .message = "epoch_throughput",
            .epoch_wall_s = wall_s,
            .epoch_batch_count = static_cast<std::uint32_t>(epoch_count),
            .epoch_sample_count = epoch_samples,
            .epoch_mean_samples_per_s = metrics.samples_per_second,
        });
        tracker_.record_epoch(metrics);
        const ScheduleAction action = tracker_.evaluate_and_suggest(current_lr);
        if (action.type != ScheduleActionType::kNone) {
          current_lr = action.new_learning_rate;
#ifdef QMINIWASM_HAS_LIBTORCH_TRAINING
          {
            std::lock_guard<std::mutex> lk(libtorch_mu_);
            libtorch_trainer_->set_learning_rate(current_lr);
          }
#endif
          emit(TelemetryEvent{
              .run_id = config_.run_id,
              .unix_ms = now_unix_ms(),
              .epoch = packet.epoch,
              .step = packet.step,
              .train_loss = metrics.train_loss,
              .val_loss = metrics.val_loss,
              .learning_rate = current_lr,
              .samples_per_second = metrics.samples_per_second,
              .sampler_queue_depth = sampler_to_prefetch_->size(),
              .prefetch_queue_depth = prefetch_to_compute_->size(),
              .compute_queue_depth = compute_to_update_->size(),
              .taxonomy_tier = policy_.tier,
              .precision_mode = policy_.precision,
              .stage = "scheduler",
              .event_type = "schedule_adjustment",
              .message = action.reason,
          });
        }
        last_completed_train_loss_ = metrics.train_loss;
        last_completed_val_loss_ = metrics.val_loss;
        last_completed_epoch_ = packet.epoch;
        {
          const std::size_t epoch_1based = packet.epoch + 1;
          auto emit_checkpoint_begin = [&](const std::string& path, const char* label) {
            if (path.empty()) {
              return;
            }
            emit(TelemetryEvent{
                .run_id = config_.run_id,
                .unix_ms = now_unix_ms(),
                .epoch = packet.epoch,
                .step = packet.step,
                .train_loss = metrics.train_loss,
                .val_loss = metrics.val_loss,
                .learning_rate = current_lr,
                .samples_per_second = metrics.samples_per_second,
                .sampler_queue_depth = sampler_to_prefetch_->size(),
                .prefetch_queue_depth = prefetch_to_compute_->size(),
                .compute_queue_depth = compute_to_update_->size(),
                .taxonomy_tier = policy_.tier,
                .precision_mode = policy_.precision,
                .stage = "checkpoint",
                .event_type = "checkpoint_begin",
                .message = std::string(label) + ":" + path,
            });
          };
          emit_checkpoint_begin(config_.checkpoint_latest_path, "latest");
          if (metrics.val_loss < best_val_loss_for_checkpoint_) {
            emit_checkpoint_begin(config_.checkpoint_best_path, "best");
          }
          auto try_write = [&](const std::string& path, const char* label) {
            if (path.empty()) {
              return;
            }
            std::string err;
#ifdef QMINIWASM_HAS_LIBTORCH_TRAINING
            InterchangeTensorSnapshot snap;
            {
              std::lock_guard<std::mutex> lk(libtorch_mu_);
              snap = libtorch_trainer_->capture_interchange_tensors();
            }
            const bool ok = LibTorchTpemTrainer::write_interchange_to_path(
                path, config_.run_id, epoch_1based, metrics.train_loss, metrics.val_loss, current_lr, std::move(snap),
                &err);
#else
            const bool ok = write_native_training_checkpoint(path, config_.run_id, epoch_1based, metrics.train_loss,
                                                             metrics.val_loss, current_lr, &err);
#endif
            emit(TelemetryEvent{
                .run_id = config_.run_id,
                .unix_ms = now_unix_ms(),
                .epoch = packet.epoch,
                .step = packet.step,
                .train_loss = metrics.train_loss,
                .val_loss = metrics.val_loss,
                .learning_rate = current_lr,
                .samples_per_second = metrics.samples_per_second,
                .sampler_queue_depth = sampler_to_prefetch_->size(),
                .prefetch_queue_depth = prefetch_to_compute_->size(),
                .compute_queue_depth = compute_to_update_->size(),
                .taxonomy_tier = policy_.tier,
                .precision_mode = policy_.precision,
                .stage = "checkpoint",
                .event_type = ok ? "native_saved" : "native_save_failed",
                .message = ok ? (std::string(label) + ":" + path) : err,
            });
          };
          try_write(config_.checkpoint_latest_path, "latest");
          if (metrics.val_loss < best_val_loss_for_checkpoint_) {
            best_val_loss_for_checkpoint_ = metrics.val_loss;
            try_write(config_.checkpoint_best_path, "best");
          }
        }
        epoch_train_sum = 0.0;
        epoch_val_sum = 0.0;
        epoch_sps_sum = 0.0;
        epoch_count = 0;
      }
      if (total_pipeline_batches > 0 && pipeline_packets_done >= total_pipeline_batches) {
        break;
      }
    }
    const bool pipeline_failed = pipeline_failed_.load(std::memory_order_acquire);
    std::string failed_message;
    if (pipeline_failed) {
      std::lock_guard<std::recursive_mutex> lk(mu_);
      failed_message = status_.message;
    }
    if (!pipeline_failed && !config_.checkpoint_save_path.empty()) {
      const std::size_t epoch_1based = last_completed_epoch_ + 1;
      emit(TelemetryEvent{
          .run_id = config_.run_id,
          .unix_ms = now_unix_ms(),
          .epoch = current_epoch,
          .step = 0,
          .train_loss = last_completed_train_loss_,
          .val_loss = last_completed_val_loss_,
          .learning_rate = current_lr,
          .samples_per_second = 0.0,
          .sampler_queue_depth = sampler_to_prefetch_->size(),
          .prefetch_queue_depth = prefetch_to_compute_->size(),
          .compute_queue_depth = compute_to_update_->size(),
          .taxonomy_tier = policy_.tier,
          .precision_mode = policy_.precision,
          .stage = "checkpoint",
          .event_type = "checkpoint_begin",
          .message = std::string("final:") + config_.checkpoint_save_path,
      });
      std::string err;
#ifdef QMINIWASM_HAS_LIBTORCH_TRAINING
      InterchangeTensorSnapshot snap_final;
      {
        std::lock_guard<std::mutex> lk(libtorch_mu_);
        snap_final = libtorch_trainer_->capture_interchange_tensors();
      }
      const bool ok = LibTorchTpemTrainer::write_interchange_to_path(
          config_.checkpoint_save_path, config_.run_id, epoch_1based, last_completed_train_loss_,
          last_completed_val_loss_, current_lr, std::move(snap_final), &err);
#else
      const bool ok =
          write_native_training_checkpoint(config_.checkpoint_save_path, config_.run_id, epoch_1based,
                                           last_completed_train_loss_, last_completed_val_loss_, current_lr, &err);
#endif
      emit(TelemetryEvent{
          .run_id = config_.run_id,
          .unix_ms = now_unix_ms(),
          .epoch = current_epoch,
          .step = 0,
          .train_loss = last_completed_train_loss_,
          .val_loss = last_completed_val_loss_,
          .learning_rate = current_lr,
          .samples_per_second = 0.0,
          .sampler_queue_depth = sampler_to_prefetch_->size(),
          .prefetch_queue_depth = prefetch_to_compute_->size(),
          .compute_queue_depth = compute_to_update_->size(),
          .taxonomy_tier = policy_.tier,
          .precision_mode = policy_.precision,
          .stage = "checkpoint",
          .event_type = ok ? "native_saved" : "native_save_failed",
          .message = ok ? (std::string("final:") + config_.checkpoint_save_path) : err,
      });
    }
    if (!pipeline_failed) {
      set_status(EngineStatus{
          .state = EngineState::kStopped,
          .run_id = config_.run_id,
          .epoch = current_epoch,
          .step = 0,
          .train_loss = 0.0,
          .val_loss = 0.0,
          .learning_rate = current_lr,
          .message = "completed",
      });
      emit(TelemetryEvent{
          .run_id = config_.run_id,
          .unix_ms = now_unix_ms(),
          .epoch = current_epoch,
          .step = 0,
          .train_loss = 0.0,
          .val_loss = 0.0,
          .learning_rate = current_lr,
          .samples_per_second = 0.0,
          .sampler_queue_depth = sampler_to_prefetch_->size(),
          .prefetch_queue_depth = prefetch_to_compute_->size(),
          .compute_queue_depth = compute_to_update_->size(),
          .taxonomy_tier = policy_.tier,
          .precision_mode = policy_.precision,
          .stage = "lifecycle",
          .event_type = "completed",
          .message = "training_completed",
      });
    } else {
      emit(TelemetryEvent{
          .run_id = config_.run_id,
          .unix_ms = now_unix_ms(),
          .epoch = current_epoch,
          .step = 0,
          .train_loss = 0.0,
          .val_loss = 0.0,
          .learning_rate = current_lr,
          .samples_per_second = 0.0,
          .sampler_queue_depth = sampler_to_prefetch_->size(),
          .prefetch_queue_depth = prefetch_to_compute_->size(),
          .compute_queue_depth = compute_to_update_->size(),
          .taxonomy_tier = policy_.tier,
          .precision_mode = policy_.precision,
          .stage = "lifecycle",
          .event_type = "failed",
          .message = failed_message.empty() ? "pipeline_failed" : failed_message,
      });
    }
    telemetry_bus_.close();
  }

  mutable std::recursive_mutex mu_;
  TrainingConfig config_{};
  TaxonomyPolicy policy_{};
  EngineStatus status_{};
  TelemetryCallback callback_;
  std::stop_source stop_source_{};
  std::vector<std::jthread> workers_;
  std::unique_ptr<internal::BoundedQueue<PipelineBatch>> sampler_to_prefetch_;
  std::unique_ptr<internal::BoundedQueue<PipelineBatch>> prefetch_to_compute_;
  std::unique_ptr<internal::BoundedQueue<ComputePacket>> compute_to_update_;
  internal::TelemetryBus telemetry_bus_;
  TrainingTracker tracker_;
  EngineState state_;
  double best_val_loss_for_checkpoint_{std::numeric_limits<double>::infinity()};
  double last_completed_train_loss_{0.0};
  double last_completed_val_loss_{0.0};
  std::size_t last_completed_epoch_{0};
#ifdef QMINIWASM_HAS_LIBTORCH_TRAINING
  std::mutex libtorch_mu_;
  std::unique_ptr<LibTorchTpemTrainer> libtorch_trainer_;
#endif
  std::atomic<bool> pipeline_failed_{false};
};

#ifdef QMINIWASM_HAS_LIBTORCH_TRAINING
void TrainingEngine::Impl::run_cascade_curriculum(std::stop_token token) {
  set_status(EngineStatus{
      .state = EngineState::kRunning,
      .run_id = config_.run_id,
      .epoch = 0,
      .step = 0,
      .train_loss = 0.0,
      .val_loss = 0.0,
      .learning_rate = config_.learning_rate,
      .message = "cascade_native_cpp",
  });

  const std::int64_t io_dim =
      config_.native_io_d_model > 0
          ? static_cast<std::int64_t>(config_.native_io_d_model)
          : (config_.native_d_model > 0 ? static_cast<std::int64_t>(config_.native_d_model) : 256);
  std::vector<float> rows;
  std::string hf_err;
  const std::uint32_t cap = config_.hf.num_samples > 0 ? config_.hf.num_samples : 2048;
  if (!config_.hf.dataset_id.empty()) {
    if (!hf_fetch_encoded_rows(config_.hf.dataset_id, config_.hf.config_name, config_.hf.split, config_.hf.revision,
                               cap, io_dim, config_.seed, &rows, &hf_err)) {
      emit(TelemetryEvent{
          .run_id = config_.run_id,
          .unix_ms = now_unix_ms(),
          .epoch = 0,
          .step = 0,
          .train_loss = 0.0,
          .val_loss = 0.0,
          .learning_rate = config_.learning_rate,
          .samples_per_second = 0.0,
          .taxonomy_tier = policy_.tier,
          .precision_mode = policy_.precision,
          .stage = "cascade_hf",
          .event_type = "hf_fetch_fallback",
          .message = hf_err.empty() ? "hf_fetch_failed" : hf_err,
      });
      rows.clear();
    }
    if (!rows.empty()) {
      hf_append_mesh_blend(&rows, io_dim, static_cast<std::uint32_t>(rows.size() / static_cast<std::size_t>(io_dim)),
                           config_.hf.mesh_blend_fraction, config_.seed);
    }
  }
  if (rows.empty()) {
    std::mt19937_64 rng(config_.seed ^ 0xFEEDFACEULL);
    std::uniform_real_distribution<float> dist(-1.0F, 1.0F);
    const std::size_t n = static_cast<std::size_t>(cap) * static_cast<std::size_t>(io_dim);
    rows.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
      rows[i] = dist(rng);
    }
  }

  const std::uint32_t num_rows = static_cast<std::uint32_t>(rows.size() / static_cast<std::size_t>(io_dim));
  if (num_rows == 0) {
    set_status(EngineStatus{
        .state = EngineState::kFailed,
        .run_id = config_.run_id,
        .epoch = 0,
        .step = 0,
        .train_loss = 0.0,
        .val_loss = 0.0,
        .learning_rate = config_.learning_rate,
        .message = "cascade: no training rows",
    });
    telemetry_bus_.close();
    return;
  }

  const std::size_t bs = std::max<std::size_t>(1, config_.batch_size);
  const std::size_t steps_per_epoch =
      std::max<std::size_t>(1, (static_cast<std::size_t>(num_rows) + bs - 1) / bs);
  const double base_lr = config_.learning_rate;
  double lr = base_lr;
  const auto& c = config_.cascade_loop;
  const std::uint32_t max_heal = std::max<std::uint32_t>(1u, c.max_heal_rounds);
  const std::uint32_t max_cycles = c.max_curriculum_cycles > 0 ? c.max_curriculum_cycles : 1u;
  const std::size_t teacher_epochs = static_cast<std::size_t>(std::max(
      1.0, std::ceil(static_cast<double>(config_.epochs) * std::max(0.05, c.teacher_epoch_fraction))));

  std::size_t total_heal_epochs = 0;
  if (config_.epochs > teacher_epochs) {
    total_heal_epochs = config_.epochs - teacher_epochs;
  } else {
    const std::uint32_t hpr = std::max<std::uint32_t>(1u, c.heal_epochs_per_round);
    total_heal_epochs = static_cast<std::size_t>(hpr) * static_cast<std::size_t>(max_heal);
  }
  total_heal_epochs = std::max<std::size_t>(1, total_heal_epochs);

  std::vector<std::uint32_t> idx(num_rows);
  std::iota(idx.begin(), idx.end(), 0U);
  std::mt19937_64 shuffle_rng(config_.seed + 7);

  std::uint32_t global_epoch_0based = 0;

  auto emit_phase = [&](const char* stage, const char* msg) {
    emit(TelemetryEvent{
        .run_id = config_.run_id,
        .unix_ms = now_unix_ms(),
        .epoch = global_epoch_0based,
        .step = 0,
        .train_loss = last_completed_train_loss_,
        .val_loss = last_completed_val_loss_,
        .learning_rate = lr,
        .samples_per_second = 0.0,
        .taxonomy_tier = policy_.tier,
        .precision_mode = policy_.precision,
        .stage = stage,
        .event_type = "cascade_phase",
        .message = msg,
    });
  };

  auto run_eval_short = [&](std::uint64_t seed_tag) -> double {
    double val_sum = 0.0;
    std::size_t vc = 0;
    const std::size_t nvb = std::min<std::size_t>(4, steps_per_epoch);
    for (std::size_t vs = 0; vs < nvb && !token.stop_requested(); ++vs) {
      std::vector<float> batch(bs * static_cast<std::size_t>(io_dim));
      for (std::size_t i = 0; i < bs; ++i) {
        const std::uint32_t r = idx[(vs * bs + i) % num_rows];
        std::memcpy(batch.data() + i * static_cast<std::size_t>(io_dim),
                    rows.data() + static_cast<std::size_t>(r) * static_cast<std::size_t>(io_dim),
                    static_cast<std::size_t>(io_dim) * sizeof(float));
      }
      double v = 0.0;
      {
        std::lock_guard<std::mutex> lk(libtorch_mu_);
        v = libtorch_trainer_->eval_step_supervised(bs, io_dim, batch.data(), batch.data(),
                                                    config_.seed ^ seed_tag ^ static_cast<std::uint64_t>(vs + 99));
      }
      val_sum += v;
      ++vc;
    }
    return val_sum / static_cast<double>(std::max<std::size_t>(1, vc));
  };

  auto emit_epoch_throughput = [&](const char* stage, const char* msg) {
    const std::uint32_t ep = global_epoch_0based;
    last_completed_epoch_ = static_cast<std::size_t>(ep);
    emit(TelemetryEvent{
        .run_id = config_.run_id,
        .unix_ms = now_unix_ms(),
        .epoch = ep,
        .step = static_cast<std::uint32_t>(steps_per_epoch > 0 ? steps_per_epoch - 1 : 0),
        .train_loss = last_completed_train_loss_,
        .val_loss = last_completed_val_loss_,
        .learning_rate = lr,
        .samples_per_second = 0.0,
        .taxonomy_tier = policy_.tier,
        .precision_mode = policy_.precision,
        .stage = stage,
        .event_type = "epoch_throughput",
        .message = msg,
    });
    ++global_epoch_0based;
  };

  const double d = static_cast<double>(config_.native_d_model > 0 ? config_.native_d_model : 256);
  const int nb = config_.native_num_ternary_blocks > 0 ? static_cast<int>(config_.native_num_ternary_blocks) : 1;
  const double est_mib = (3.0 * d * d * static_cast<double>(nb) * 4.0) / (1024.0 * 1024.0);

  bool gate_ok = false;
  const std::size_t heal_base = total_heal_epochs / static_cast<std::size_t>(max_heal);
  const std::size_t heal_rem = total_heal_epochs % static_cast<std::size_t>(max_heal);

  for (std::uint32_t cycle = 0; cycle < max_cycles && !token.stop_requested(); ++cycle) {
    if (cycle == 0) {
      emit_phase("cascade_phase_teacher", "teacher_fit_start");
      for (std::size_t e = 0; e < teacher_epochs && !token.stop_requested(); ++e) {
        std::shuffle(idx.begin(), idx.end(), shuffle_rng);
        double tr_sum = 0.0;
        std::size_t cnt = 0;
        for (std::size_t step = 0; step < steps_per_epoch && !token.stop_requested(); ++step) {
          std::vector<float> batch(bs * static_cast<std::size_t>(io_dim));
          for (std::size_t i = 0; i < bs; ++i) {
            const std::uint32_t r = idx[(step * bs + i) % num_rows];
            std::memcpy(batch.data() + i * static_cast<std::size_t>(io_dim),
                        rows.data() + static_cast<std::size_t>(r) * static_cast<std::size_t>(io_dim),
                        static_cast<std::size_t>(io_dim) * sizeof(float));
          }
          double loss = 0.0;
          {
            std::lock_guard<std::mutex> lk(libtorch_mu_);
            loss = libtorch_trainer_->train_step_supervised(bs, io_dim, batch.data(), batch.data(),
                                                            config_.seed ^ static_cast<std::uint64_t>(e ^ (step + 1)));
          }
          tr_sum += loss;
          ++cnt;
        }
        last_completed_train_loss_ = tr_sum / static_cast<double>(std::max<std::size_t>(1, cnt));
        last_completed_val_loss_ = run_eval_short(static_cast<std::uint64_t>(e + 501));
        emit_epoch_throughput("cascade_teacher", "teacher_epoch");
      }

      if (!c.teacher_checkpoint_path.empty()) {
        std::string err;
        InterchangeTensorSnapshot snap_teacher;
        {
          std::lock_guard<std::mutex> lk(libtorch_mu_);
          snap_teacher = libtorch_trainer_->capture_interchange_tensors();
        }
        const bool ok = LibTorchTpemTrainer::write_interchange_to_path(
            c.teacher_checkpoint_path, config_.run_id, teacher_epochs, last_completed_train_loss_,
            last_completed_val_loss_, lr, std::move(snap_teacher), &err);
        emit(TelemetryEvent{
            .run_id = config_.run_id,
            .unix_ms = now_unix_ms(),
            .epoch = global_epoch_0based > 0 ? global_epoch_0based - 1 : 0,
            .step = 0,
            .train_loss = last_completed_train_loss_,
            .val_loss = last_completed_val_loss_,
            .learning_rate = lr,
            .samples_per_second = 0.0,
            .taxonomy_tier = policy_.tier,
            .precision_mode = policy_.precision,
            .stage = "cascade_checkpoint",
            .event_type = ok ? "native_saved" : "native_save_failed",
            .message = ok ? ("teacher:" + c.teacher_checkpoint_path) : err,
        });
      }
    } else {
      if (c.teacher_checkpoint_path.empty()) {
        emit(TelemetryEvent{
            .run_id = config_.run_id,
            .unix_ms = now_unix_ms(),
            .taxonomy_tier = policy_.tier,
            .precision_mode = policy_.precision,
            .stage = "cascade_cycle_reload",
            .event_type = "failed",
            .message = "max_curriculum_cycles>1 requires teacher_checkpoint_path",
        });
        break;
      }
      emit_phase("cascade_phase_cycle", ("curriculum_cycle_" + std::to_string(cycle)).c_str());
      lr = base_lr;
      if (!c.teacher_checkpoint_path.empty()) {
        std::string err;
        const bool loaded = [&]() {
          std::lock_guard<std::mutex> lk(libtorch_mu_);
          return libtorch_trainer_->load_interchange(c.teacher_checkpoint_path, &err);
        }();
        if (!loaded) {
          emit(TelemetryEvent{
              .run_id = config_.run_id,
              .unix_ms = now_unix_ms(),
              .taxonomy_tier = policy_.tier,
              .precision_mode = policy_.precision,
              .stage = "cascade_cycle_reload",
              .event_type = "failed",
              .message = err,
          });
          break;
        }
      }
    }

    {
      std::string err;
      std::lock_guard<std::mutex> lk(libtorch_mu_);
      if (!libtorch_trainer_->clone_teacher_from_core(&err)) {
        emit(TelemetryEvent{
            .run_id = config_.run_id,
            .unix_ms = now_unix_ms(),
            .taxonomy_tier = policy_.tier,
            .precision_mode = policy_.precision,
            .stage = "cascade_teacher_clone",
            .event_type = "failed",
            .message = err,
        });
      }
      if (!libtorch_trainer_->apply_ptqtp_reconstruct_experts(static_cast<int>(c.ptqtp_num_planes), &err)) {
        emit(TelemetryEvent{
            .run_id = config_.run_id,
            .unix_ms = now_unix_ms(),
            .taxonomy_tier = policy_.tier,
            .precision_mode = policy_.precision,
            .stage = "cascade_ptqtp",
            .event_type = "failed",
            .message = err,
        });
      }
    }
    emit_phase("cascade_phase_ptqtp", "ptqtp_applied");

    for (std::uint32_t round = 0; round < max_heal && !token.stop_requested(); ++round) {
      lr *= c.heal_learning_rate_scale;
      {
        std::lock_guard<std::mutex> lk(libtorch_mu_);
        libtorch_trainer_->set_learning_rate(lr);
      }
      emit_phase("cascade_phase_heal", ("heal_round_" + std::to_string(round)).c_str());

      const std::size_t epochs_this_round =
          heal_base + (static_cast<std::size_t>(round) < heal_rem ? 1u : 0u);
      for (std::size_t he = 0; he < epochs_this_round && !token.stop_requested(); ++he) {
        std::shuffle(idx.begin(), idx.end(), shuffle_rng);
        double tr_sum = 0.0;
        std::size_t cnt = 0;
        for (std::size_t step = 0; step < steps_per_epoch && !token.stop_requested(); ++step) {
          std::vector<float> batch(bs * static_cast<std::size_t>(io_dim));
          for (std::size_t i = 0; i < bs; ++i) {
            const std::uint32_t r = idx[(step * bs + i) % num_rows];
            std::memcpy(batch.data() + i * static_cast<std::size_t>(io_dim),
                        rows.data() + static_cast<std::size_t>(r) * static_cast<std::size_t>(io_dim),
                        static_cast<std::size_t>(io_dim) * sizeof(float));
          }
          double loss = 0.0;
          {
            std::lock_guard<std::mutex> lk(libtorch_mu_);
            loss = libtorch_trainer_->train_step_distill(bs, io_dim, batch.data(), batch.data(), 0.25,
                                                         config_.seed ^
                                                             static_cast<std::uint64_t>(cycle * 100000 + round * 1000 + he + step));
          }
          tr_sum += loss;
          ++cnt;
        }
        last_completed_train_loss_ = tr_sum / static_cast<double>(std::max<std::size_t>(1, cnt));
        last_completed_val_loss_ = run_eval_short(static_cast<std::uint64_t>(cycle * 50000 + round * 777 + he));
        emit_epoch_throughput("cascade_heal", "heal_epoch");
      }

      const bool mse_ok = c.gate_target_val_mse <= 0.0 || last_completed_val_loss_ <= c.gate_target_val_mse;
      const bool mib_ok = c.gate_max_tpem_mib <= 0.0 || est_mib <= c.gate_max_tpem_mib;
      gate_ok = mse_ok && mib_ok;
      const std::uint32_t gate_ep =
          global_epoch_0based > 0 ? global_epoch_0based - 1U : 0U;
      emit(TelemetryEvent{
          .run_id = config_.run_id,
          .unix_ms = now_unix_ms(),
          .epoch = gate_ep,
          .step = 0,
          .train_loss = last_completed_train_loss_,
          .val_loss = last_completed_val_loss_,
          .learning_rate = lr,
          .samples_per_second = 0.0,
          .taxonomy_tier = policy_.tier,
          .precision_mode = policy_.precision,
          .stage = "cascade_gate",
          .event_type = gate_ok ? "cascade_gate_pass" : "cascade_gate_fail",
          .message = gate_ok ? "gate_ok" : "gate_retry",
          .estimated_tpem_mib = est_mib,
          .tier_cap_mib = c.gate_max_tpem_mib,
      });
      if (gate_ok) {
        break;
      }
    }
    if (gate_ok) {
      break;
    }
  }

  if (c.run_taxonomy_linter) {
    const int rc = std::system("python scripts/taxonomy_linter.py --full");
    emit(TelemetryEvent{
        .run_id = config_.run_id,
        .unix_ms = now_unix_ms(),
        .taxonomy_tier = policy_.tier,
        .precision_mode = policy_.precision,
        .stage = "taxonomy_linter",
        .event_type = rc == 0 ? "taxonomy_ok" : "taxonomy_fail",
        .message = "exit_code=" + std::to_string(rc),
    });
  }

  const std::size_t cascade_save_epoch =
      std::max<std::size_t>(1, static_cast<std::size_t>(global_epoch_0based));
  if (!config_.checkpoint_latest_path.empty()) {
    std::string err;
    InterchangeTensorSnapshot snap_latest;
    {
      std::lock_guard<std::mutex> lk(libtorch_mu_);
      snap_latest = libtorch_trainer_->capture_interchange_tensors();
    }
    const bool ok = LibTorchTpemTrainer::write_interchange_to_path(
        config_.checkpoint_latest_path, config_.run_id, cascade_save_epoch, last_completed_train_loss_,
        last_completed_val_loss_, lr, std::move(snap_latest), &err);
    (void)ok;
  }
  if (!config_.checkpoint_save_path.empty()) {
    std::string err;
    const std::size_t ep_final = cascade_save_epoch + 1;
    InterchangeTensorSnapshot snap_cascade_final;
    {
      std::lock_guard<std::mutex> lk(libtorch_mu_);
      snap_cascade_final = libtorch_trainer_->capture_interchange_tensors();
    }
    const bool ok = LibTorchTpemTrainer::write_interchange_to_path(
        config_.checkpoint_save_path, config_.run_id, ep_final, last_completed_train_loss_, last_completed_val_loss_,
        lr, std::move(snap_cascade_final), &err);
    emit(TelemetryEvent{
        .run_id = config_.run_id,
        .unix_ms = now_unix_ms(),
        .epoch = last_completed_epoch_,
        .step = 0,
        .train_loss = last_completed_train_loss_,
        .val_loss = last_completed_val_loss_,
        .learning_rate = lr,
        .samples_per_second = 0.0,
        .taxonomy_tier = policy_.tier,
        .precision_mode = policy_.precision,
        .stage = "checkpoint",
        .event_type = ok ? "native_saved" : "native_save_failed",
        .message = ok ? ("final:" + config_.checkpoint_save_path) : err,
    });
  }

  set_status(EngineStatus{
      .state = EngineState::kStopped,
      .run_id = config_.run_id,
      .epoch = static_cast<std::size_t>(last_completed_epoch_),
      .step = 0,
      .train_loss = last_completed_train_loss_,
      .val_loss = last_completed_val_loss_,
      .learning_rate = lr,
      .message = gate_ok ? "cascade_completed_gate_ok" : "cascade_completed",
  });
  emit(TelemetryEvent{
      .run_id = config_.run_id,
      .unix_ms = now_unix_ms(),
      .epoch = last_completed_epoch_,
      .step = 0,
      .train_loss = last_completed_train_loss_,
      .val_loss = last_completed_val_loss_,
      .learning_rate = lr,
      .samples_per_second = 0.0,
      .taxonomy_tier = policy_.tier,
      .precision_mode = policy_.precision,
      .stage = "lifecycle",
      .event_type = "completed",
      .message = "cascade_curriculum_completed",
  });
  telemetry_bus_.close();
}
#endif

TrainingEngine::TrainingEngine() : impl_(std::make_unique<Impl>()) {}
TrainingEngine::~TrainingEngine() = default;

bool TrainingEngine::start(const TrainingConfig& config, std::string* error_message) {
  return impl_->start(config, error_message);
}

void TrainingEngine::request_stop() { impl_->request_stop(); }
EngineStatus TrainingEngine::status() const { return impl_->status(); }
bool TrainingEngine::pop_telemetry(TelemetryEvent* out_event, std::uint64_t timeout_ms) {
  return impl_->pop_telemetry(out_event, timeout_ms);
}
void TrainingEngine::set_telemetry_callback(TelemetryCallback callback) {
  impl_->set_telemetry_callback(std::move(callback));
}

}  // namespace qminiwasm::training
