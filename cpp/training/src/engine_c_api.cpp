#include "qminiwasm/training/engine_c_api.h"

#include <cstddef>
#include <string>

#include "qminiwasm/training/training_engine.hpp"

namespace qminiwasm::training {
namespace {

qmw_taxonomy_tier_t to_c_tier(TaxonomyTier tier) {
  switch (tier) {
    case TaxonomyTier::kEdgeConstrained:
      return QMW_TAXONOMY_TIER_EDGE_CONSTRAINED;
    case TaxonomyTier::kFogNode:
      return QMW_TAXONOMY_TIER_FOG_NODE;
    case TaxonomyTier::kXpuCluster:
      return QMW_TAXONOMY_TIER_XPU_CLUSTER;
  }
  return QMW_TAXONOMY_TIER_EDGE_CONSTRAINED;
}

TaxonomyTier from_c_tier(qmw_taxonomy_tier_t tier) {
  switch (tier) {
    case QMW_TAXONOMY_TIER_FOG_NODE:
      return TaxonomyTier::kFogNode;
    case QMW_TAXONOMY_TIER_XPU_CLUSTER:
      return TaxonomyTier::kXpuCluster;
    case QMW_TAXONOMY_TIER_EDGE_CONSTRAINED:
    default:
      return TaxonomyTier::kEdgeConstrained;
  }
}

}  // namespace
}  // namespace qminiwasm::training

struct qmw_engine_handle {
  qminiwasm::training::TrainingEngine engine;
  std::string last_error;
  qmw_telemetry_callback_t callback = nullptr;
  void* callback_user_data = nullptr;
  qminiwasm::training::TelemetryEvent last_event_storage;
  qmw_telemetry_event_t callback_event{};
};

extern "C" {

qmw_engine_handle_t* qmw_engine_create(void) {
  auto* handle = new qmw_engine_handle_t();
  return handle;
}

void qmw_engine_destroy(qmw_engine_handle_t* handle) {
  if (handle == nullptr) {
    return;
  }
  handle->engine.request_stop();
  delete handle;
}

bool qmw_engine_start(qmw_engine_handle_t* handle, const qmw_training_config_t* config, const char** error_message) {
  if (handle == nullptr || config == nullptr) {
    return false;
  }
  qminiwasm::training::TrainingConfig native;
  if (config->run_id != nullptr) {
    native.run_id = config->run_id;
  }
  native.epochs = static_cast<std::size_t>(config->epochs);
  native.batch_size = static_cast<std::size_t>(config->batch_size);
  native.micro_batch_size = static_cast<std::size_t>(config->micro_batch_size);
  native.worker_threads = static_cast<std::size_t>(config->worker_threads);
  native.prefetch_depth = static_cast<std::size_t>(config->prefetch_depth);
  native.compute_slots = static_cast<std::size_t>(config->compute_slots);
  native.classes = static_cast<std::size_t>(config->classes);
  native.samples_per_class = static_cast<std::size_t>(config->samples_per_class);
  native.seed = config->seed;
  native.learning_rate = config->learning_rate;
  native.taxonomy_tier = qminiwasm::training::from_c_tier(config->taxonomy_tier);

  std::string err;
  const bool ok = handle->engine.start(native, &err);
  handle->last_error = err;
  if (!ok && error_message != nullptr) {
    *error_message = handle->last_error.c_str();
  }
  return ok;
}

void qmw_engine_stop(qmw_engine_handle_t* handle) {
  if (handle == nullptr) {
    return;
  }
  handle->engine.request_stop();
}

qmw_engine_status_t qmw_engine_status(const qmw_engine_handle_t* handle) {
  if (handle == nullptr) {
    return qmw_engine_status_t{};
  }
  const qminiwasm::training::EngineStatus status = handle->engine.status();
  return qmw_engine_status_t{
      .state = static_cast<int32_t>(status.state),
      .epoch = static_cast<uint64_t>(status.epoch),
      .step = static_cast<uint64_t>(status.step),
      .train_loss = status.train_loss,
      .val_loss = status.val_loss,
      .learning_rate = status.learning_rate,
  };
}

bool qmw_engine_poll_telemetry(qmw_engine_handle_t* handle, qmw_telemetry_event_t* out_event, uint64_t timeout_ms) {
  if (handle == nullptr || out_event == nullptr) {
    return false;
  }
  qminiwasm::training::TelemetryEvent event;
  if (!handle->engine.pop_telemetry(&event, timeout_ms)) {
    return false;
  }
  handle->last_event_storage = std::move(event);
  const auto& ref = handle->last_event_storage;
  handle->callback_event = qmw_telemetry_event_t{
      .run_id = ref.run_id.c_str(),
      .unix_ms = ref.unix_ms,
      .epoch = static_cast<uint64_t>(ref.epoch),
      .step = static_cast<uint64_t>(ref.step),
      .train_loss = ref.train_loss,
      .val_loss = ref.val_loss,
      .learning_rate = ref.learning_rate,
      .samples_per_second = ref.samples_per_second,
      .sampler_queue_depth = static_cast<uint64_t>(ref.sampler_queue_depth),
      .prefetch_queue_depth = static_cast<uint64_t>(ref.prefetch_queue_depth),
      .compute_queue_depth = static_cast<uint64_t>(ref.compute_queue_depth),
      .taxonomy_tier = static_cast<int32_t>(qminiwasm::training::to_c_tier(ref.taxonomy_tier)),
      .precision_mode = static_cast<int32_t>(ref.precision_mode),
      .stage = ref.stage.c_str(),
      .event_type = ref.event_type.c_str(),
      .message = ref.message.c_str(),
  };
  *out_event = handle->callback_event;
  return true;
}

void qmw_engine_set_telemetry_callback(
    qmw_engine_handle_t* handle, qmw_telemetry_callback_t callback, void* user_data) {
  if (handle == nullptr) {
    return;
  }
  handle->callback = callback;
  handle->callback_user_data = user_data;
  handle->engine.set_telemetry_callback([handle](const qminiwasm::training::TelemetryEvent& event) {
    if (handle->callback == nullptr) {
      return;
    }
    handle->last_event_storage = event;
    const auto& ref = handle->last_event_storage;
    handle->callback_event = qmw_telemetry_event_t{
        .run_id = ref.run_id.c_str(),
        .unix_ms = ref.unix_ms,
        .epoch = static_cast<uint64_t>(ref.epoch),
        .step = static_cast<uint64_t>(ref.step),
        .train_loss = ref.train_loss,
        .val_loss = ref.val_loss,
        .learning_rate = ref.learning_rate,
        .samples_per_second = ref.samples_per_second,
        .sampler_queue_depth = static_cast<uint64_t>(ref.sampler_queue_depth),
        .prefetch_queue_depth = static_cast<uint64_t>(ref.prefetch_queue_depth),
        .compute_queue_depth = static_cast<uint64_t>(ref.compute_queue_depth),
        .taxonomy_tier = static_cast<int32_t>(qminiwasm::training::to_c_tier(ref.taxonomy_tier)),
        .precision_mode = static_cast<int32_t>(ref.precision_mode),
        .stage = ref.stage.c_str(),
        .event_type = ref.event_type.c_str(),
        .message = ref.message.c_str(),
    };
    handle->callback(&handle->callback_event, handle->callback_user_data);
  });
}

}  // extern "C"
