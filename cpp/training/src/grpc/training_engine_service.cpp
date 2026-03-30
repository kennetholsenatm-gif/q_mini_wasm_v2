#include <grpcpp/grpcpp.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <string>

#include "qminiwasm/training/grpc/training_engine_service.hpp"
#include "qminiwasm/training/training_engine.hpp"
#include "training_engine.grpc.pb.h"

namespace qminiwasm::training::grpcsvc {

namespace {

qminiwasm::training::TrainingConfig map_config(const qminiwasm::trainingrpc::StartTrainingRequest& req) {
  qminiwasm::training::TrainingConfig out;
  const auto& cfg = req.config();
  out.run_id = cfg.run_id();
  out.epochs = static_cast<std::size_t>(cfg.epochs());
  out.batch_size = static_cast<std::size_t>(cfg.batch_size());
  out.micro_batch_size = static_cast<std::size_t>(cfg.micro_batch_size());
  out.prefetch_depth = static_cast<std::size_t>(cfg.prefetch_depth());
  out.compute_slots = static_cast<std::size_t>(cfg.compute_slots());
  out.worker_threads = static_cast<std::size_t>(cfg.worker_threads());
  out.learning_rate = cfg.learning_rate();
  out.seed = cfg.seed();
  out.taxonomy_tier = parse_taxonomy_tier(cfg.taxonomy_tier());
  out.model_uri = cfg.model_uri();
  out.checkpoint_save_path = cfg.checkpoint_save_path();
  out.checkpoint_best_path = cfg.checkpoint_best_path();
  out.checkpoint_latest_path = cfg.checkpoint_latest_path();
  out.native_d_model = cfg.d_model();
  out.native_io_d_model = cfg.io_d_model();
  out.native_num_ternary_blocks = cfg.num_ternary_blocks();
  out.use_native_engine_only = cfg.use_native_engine_only();
  if (!cfg.hf().dataset_id().empty()) {
    const auto& h = cfg.hf();
    out.hf.dataset_id = h.dataset_id();
    out.hf.config_name = h.config_name();
    out.hf.split = h.split();
    if (out.hf.split.empty()) {
      out.hf.split = "train";
    }
    out.hf.revision = h.revision();
    out.hf.num_samples = h.num_samples();
    out.hf.mesh_blend_fraction = h.mesh_blend_fraction();
    out.hf.text_fields.clear();
    for (int i = 0; i < h.text_fields_size(); ++i) {
      out.hf.text_fields.push_back(h.text_fields(i));
    }
  }
  if (cfg.cascade_loop().enabled()) {
    const auto& c = cfg.cascade_loop();
    out.cascade_loop.enabled = c.enabled();
    out.cascade_loop.max_heal_rounds = c.max_heal_rounds() > 0 ? c.max_heal_rounds() : 3;
    out.cascade_loop.teacher_checkpoint_path = c.teacher_checkpoint_path();
    out.cascade_loop.ptqtp_num_planes = c.ptqtp_num_planes() > 0 ? c.ptqtp_num_planes() : 2;
    out.cascade_loop.gate_target_val_mse = c.gate_target_val_mse();
    out.cascade_loop.gate_max_tpem_mib = c.gate_max_tpem_mib();
    out.cascade_loop.run_taxonomy_linter = c.run_taxonomy_linter();
    out.cascade_loop.heal_learning_rate_scale =
        c.heal_learning_rate_scale() > 0 ? c.heal_learning_rate_scale() : 0.5;
    out.cascade_loop.teacher_epoch_fraction =
        c.teacher_epoch_fraction() > 0 ? c.teacher_epoch_fraction() : 0.5;
    out.cascade_loop.heal_epochs_per_round = c.heal_epochs_per_round() > 0 ? c.heal_epochs_per_round() : 2;
    out.cascade_loop.max_curriculum_cycles = c.max_curriculum_cycles() > 0 ? c.max_curriculum_cycles() : 1;
  }
  out.cascade_policy_optimizer = cfg.cascade_policy_optimizer();
  out.cispo_clip_epsilon = cfg.cispo_clip_epsilon();
  out.attention_backend = cfg.attention_backend();
  out.native_bloch_seq_len = cfg.native_bloch_seq_len();
  out.native_bloch_num_heads = cfg.native_bloch_num_heads();
  if (cfg.cascade_rl_group_size() > 0) {
    out.cascade_rl_group_size = static_cast<std::size_t>(cfg.cascade_rl_group_size());
  } else {
    out.cascade_rl_group_size = 4;
  }
  out.training_phases.clear();
  for (int i = 0; i < cfg.training_phases_size(); ++i) {
    const auto& p = cfg.training_phases(i);
    qminiwasm::training::TrainingPhaseNative ph;
    ph.name = p.name();
    ph.epochs = p.epochs() > 0 ? static_cast<std::size_t>(p.epochs()) : 1;
    ph.supervised = p.supervised();
    ph.cascade_rl = p.cascade_rl();
    ph.freeze_model_backbone = p.freeze_model_backbone();
    ph.router_only = p.router_only();
    ph.cascade_policy_optimizer = p.cascade_policy_optimizer();
    if (p.has_cispo_clip_epsilon()) {
      ph.cispo_clip_epsilon = p.cispo_clip_epsilon();
    }
    if (p.has_cascade_mopd_lambda()) {
      ph.cascade_mopd_lambda = p.cascade_mopd_lambda();
    }
    if (p.has_tequila_deadzone()) {
      ph.tequila_deadzone = p.tequila_deadzone();
    }
    ph.freeze_ternary_experts = p.freeze_ternary_experts();
    ph.mopd_teacher_checkpoint_path = p.mopd_teacher_checkpoint_path();
    out.training_phases.push_back(std::move(ph));
  }
  return out;
}

// Informational only (matches training-wui preflight / LibTorch dense cold-start layout).
void FillNativeColdStartMemoryEstimate(const qminiwasm::trainingrpc::TrainingConfig& cfg,
                                       qminiwasm::trainingrpc::NativeColdStartMemoryEstimate* out) {
  if (out == nullptr) {
    return;
  }
  const std::uint64_t d = cfg.d_model() > 0 ? static_cast<std::uint64_t>(cfg.d_model()) : 4096ULL;
  const std::uint64_t io = cfg.io_d_model() > 0 ? static_cast<std::uint64_t>(cfg.io_d_model()) : d;
  const std::uint64_t nb = cfg.num_ternary_blocks() > 0 ? static_cast<std::uint64_t>(cfg.num_ternary_blocks()) : 1ULL;
  const std::uint64_t batch = cfg.batch_size() > 0 ? static_cast<std::uint64_t>(cfg.batch_size()) : 1ULL;
  const bool checkpoint = !cfg.model_uri().empty();

  std::uint64_t param_floats = 0;
  if (io != d) {
    param_floats += 2ULL * io * d + d + io;
  }
  param_floats += nb * (d * d + 2ULL * d);

  std::string ab = cfg.attention_backend();
  std::transform(ab.begin(), ab.end(), ab.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (ab == "bloch") {
    std::uint32_t t_len = cfg.native_bloch_seq_len() > 0 ? cfg.native_bloch_seq_len() : 8;
    std::uint32_t H = cfg.native_bloch_num_heads() > 0 ? cfg.native_bloch_num_heads() : 4;
    while (H > 1U && (d % static_cast<std::uint64_t>(H)) != 0ULL) {
      --H;
    }
    if (H < 1U) {
      H = 1U;
    }
    const std::uint64_t dv = d / static_cast<std::uint64_t>(H);
    param_floats += static_cast<std::uint64_t>(t_len) * d;
    param_floats += 2ULL * (3ULL * static_cast<std::uint64_t>(H) * d + 3ULL * static_cast<std::uint64_t>(H));
    param_floats += static_cast<std::uint64_t>(H) * dv * d + static_cast<std::uint64_t>(H) * dv;
    param_floats += d * (static_cast<std::uint64_t>(H) * dv) + d;
  }

  const std::uint64_t param_bytes = param_floats * 4ULL;
  const std::uint64_t adam_bytes = param_bytes * 2ULL;
  const std::uint64_t act_bytes = batch * (io + d * (nb + 2ULL)) * 4ULL * 2ULL;

  out->set_parameter_bytes_fp32(param_bytes);
  out->set_adam_state_bytes_fp32(adam_bytes);
  out->set_activation_scratch_bytes_hint(act_bytes);
  out->set_effective_d_model(static_cast<std::uint32_t>(d));
  out->set_effective_io_d_model(static_cast<std::uint32_t>(io));
  out->set_effective_num_ternary_blocks(static_cast<std::uint32_t>(nb));
  out->set_batch_size(static_cast<std::uint32_t>(batch));
  out->set_model_uri_checkpoint_load(checkpoint);
  std::string summary =
      "Dense FP32 native TPEM cold-start: param floats = (io!=d ? 2*io*d+d+io : 0) + Nb*(d^2+2d); "
      "param_bytes=4*floats; adam_bytes~=2*param_bytes; scratch_hint=batch*(io+d*(Nb+2))*4*2 (order-of-magnitude).";
  if (checkpoint) {
    summary += " model_uri set: on-disk interchange defines actual tensors; this estimate uses proto geometry for planning.";
  }
  out->set_formula_summary(summary);
}

int map_state_value(qminiwasm::training::EngineState state) {
  switch (state) {
    case qminiwasm::training::EngineState::kIdle:
      return 1;
    case qminiwasm::training::EngineState::kStarting:
      return 2;
    case qminiwasm::training::EngineState::kRunning:
      return 3;
    case qminiwasm::training::EngineState::kStopping:
      return 4;
    case qminiwasm::training::EngineState::kStopped:
      return 5;
    case qminiwasm::training::EngineState::kFailed:
      return 6;
  }
  return 0;
}

class TrainingEngineService final : public qminiwasm::trainingrpc::TrainingEngineService::Service {
 public:
  grpc::Status StartTraining(grpc::ServerContext*,
                             const qminiwasm::trainingrpc::StartTrainingRequest* request,
                             qminiwasm::trainingrpc::StartTrainingResponse* response) override {
    const auto config = map_config(*request);
    FillNativeColdStartMemoryEstimate(request->config(), response->mutable_memory_estimate());
    std::string error;
    try {
      std::lock_guard<std::mutex> lk(start_mu_);
      const auto prior = engine_.status();
      if (prior.state == qminiwasm::training::EngineState::kStarting ||
          prior.state == qminiwasm::training::EngineState::kRunning) {
        engine_.request_stop();
      }
      const bool ok = engine_.start(config, &error);
      response->set_accepted(ok);
      response->set_message(ok ? "started" : error);
      response->set_run_id(config.run_id);
      return grpc::Status::OK;
    } catch (const std::exception& ex) {
      response->set_accepted(false);
      response->set_message(std::string("native training start failed: ") + ex.what());
      response->set_run_id(config.run_id);
      return grpc::Status::OK;
    } catch (...) {
      response->set_accepted(false);
      response->set_message("native training start failed: unknown exception");
      response->set_run_id(config.run_id);
      return grpc::Status::OK;
    }
  }

  grpc::Status StopTraining(grpc::ServerContext*,
                            const qminiwasm::trainingrpc::StopTrainingRequest*,
                            qminiwasm::trainingrpc::StopTrainingResponse* response) override {
    engine_.request_stop();
    response->set_accepted(true);
    response->set_message("stop_requested");
    return grpc::Status::OK;
  }

  grpc::Status GetStatus(grpc::ServerContext*,
                         const qminiwasm::trainingrpc::StatusRequest*,
                         qminiwasm::trainingrpc::StatusResponse* response) override {
    const auto status = engine_.status();
    response->set_state(static_cast<qminiwasm::trainingrpc::StatusResponse::EngineState>(map_state_value(status.state)));
    response->set_run_id(status.run_id);
    response->set_epoch(static_cast<std::uint32_t>(status.epoch));
    response->set_step(static_cast<std::uint32_t>(status.step));
    response->set_train_loss(status.train_loss);
    response->set_val_loss(status.val_loss);
    response->set_learning_rate(status.learning_rate);
    response->set_message(status.message);
    return grpc::Status::OK;
  }

  grpc::Status StreamTelemetry(
      grpc::ServerContext* context,
      const qminiwasm::trainingrpc::TelemetryRequest* request,
      grpc::ServerWriter<qminiwasm::trainingrpc::TelemetryEvent>* writer) override {
    while (!context->IsCancelled()) {
      qminiwasm::training::TelemetryEvent event;
      if (!engine_.pop_telemetry(&event, 200)) {
        const auto st = engine_.status();
        if (st.state == qminiwasm::training::EngineState::kStopped ||
            st.state == qminiwasm::training::EngineState::kFailed) {
          break;
        }
        // kIdle: without run_id the stream ends (legacy unfiltered client). With run_id the client may
        // have opened StreamTelemetry before StartTraining; keep waiting until terminal state or cancel.
        if (st.state == qminiwasm::training::EngineState::kIdle) {
          if (!request->run_id().empty()) {
            continue;
          }
          break;
        }
        continue;
      }
      if (!request->run_id().empty() && event.run_id != request->run_id()) {
        continue;
      }
      qminiwasm::trainingrpc::TelemetryEvent msg;
      msg.set_run_id(event.run_id);
      msg.set_unix_ms(event.unix_ms);
      msg.set_epoch(static_cast<std::uint32_t>(event.epoch));
      msg.set_step(static_cast<std::uint32_t>(event.step));
      msg.set_train_loss(event.train_loss);
      msg.set_val_loss(event.val_loss);
      msg.set_learning_rate(event.learning_rate);
      msg.set_samples_per_second(event.samples_per_second);
      msg.set_sampler_queue_depth(static_cast<std::uint32_t>(event.sampler_queue_depth));
      msg.set_prefetch_queue_depth(static_cast<std::uint32_t>(event.prefetch_queue_depth));
      msg.set_compute_queue_depth(static_cast<std::uint32_t>(event.compute_queue_depth));
      msg.set_taxonomy_tier(to_string(event.taxonomy_tier));
      msg.set_precision_mode(to_string(event.precision_mode));
      msg.set_stage(event.stage);
      msg.set_event_type(event.event_type);
      msg.set_message(event.message);
      msg.set_graph_id(event.graph_id);
      msg.set_node_id(event.node_id);
      msg.set_enclave_state(event.enclave_state);
      msg.set_attestation_state(event.attestation_state);
      msg.set_decoherence_score(event.decoherence_score);
      msg.set_epoch_wall_s(event.epoch_wall_s);
      msg.set_epoch_batch_count(event.epoch_batch_count);
      msg.set_epoch_sample_count(event.epoch_sample_count);
      msg.set_epoch_mean_samples_per_s(event.epoch_mean_samples_per_s);
      msg.set_host_rss_mib(event.host_rss_mib);
      msg.set_estimated_tpem_mib(event.estimated_tpem_mib);
      msg.set_tier_cap_mib(event.tier_cap_mib);
      msg.set_training_phase(event.training_phase);
      msg.set_cascade_policy_optimizer(event.cascade_policy_optimizer);
      if (!writer->Write(msg)) {
        break;
      }
    }
    return grpc::Status::OK;
  }

 private:
  std::mutex start_mu_;
  qminiwasm::training::TrainingEngine engine_;
};

}  // namespace

void RegisterTrainingEngineService(grpc::ServerBuilder& builder) {
  static TrainingEngineService service;
  builder.RegisterService(&service);
}

}  // namespace qminiwasm::training::grpcsvc
