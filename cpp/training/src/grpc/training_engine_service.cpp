#include <grpcpp/grpcpp.h>

#include <memory>
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
  return out;
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
    std::string error;
    const auto config = map_config(*request);
    const bool ok = engine_.start(config, &error);
    response->set_accepted(ok);
    response->set_message(ok ? "started" : error);
    response->set_run_id(config.run_id);
    return grpc::Status::OK;
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
      if (!writer->Write(msg)) {
        break;
      }
    }
    return grpc::Status::OK;
  }

 private:
  qminiwasm::training::TrainingEngine engine_;
};

}  // namespace

void RegisterTrainingEngineService(grpc::ServerBuilder& builder) {
  static TrainingEngineService service;
  builder.RegisterService(&service);
}

}  // namespace qminiwasm::training::grpcsvc
