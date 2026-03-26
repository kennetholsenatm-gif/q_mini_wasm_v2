#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum qmw_taxonomy_tier {
  QMW_TAXONOMY_TIER_EDGE_CONSTRAINED = 0,
  QMW_TAXONOMY_TIER_FOG_NODE = 1,
  QMW_TAXONOMY_TIER_XPU_CLUSTER = 2,
} qmw_taxonomy_tier_t;

typedef struct qmw_training_config {
  const char* run_id;
  uint64_t epochs;
  uint64_t batch_size;
  uint64_t micro_batch_size;
  uint64_t worker_threads;
  uint64_t prefetch_depth;
  uint64_t compute_slots;
  uint64_t classes;
  uint64_t samples_per_class;
  uint64_t seed;
  double learning_rate;
  qmw_taxonomy_tier_t taxonomy_tier;
} qmw_training_config_t;

typedef struct qmw_engine_status {
  int32_t state;
  uint64_t epoch;
  uint64_t step;
  double train_loss;
  double val_loss;
  double learning_rate;
} qmw_engine_status_t;

typedef struct qmw_telemetry_event {
  const char* run_id;
  uint64_t unix_ms;
  uint64_t epoch;
  uint64_t step;
  double train_loss;
  double val_loss;
  double learning_rate;
  double samples_per_second;
  uint64_t sampler_queue_depth;
  uint64_t prefetch_queue_depth;
  uint64_t compute_queue_depth;
  int32_t taxonomy_tier;
  int32_t precision_mode;
  const char* stage;
  const char* event_type;
  const char* message;
} qmw_telemetry_event_t;

typedef struct qmw_engine_handle qmw_engine_handle_t;
typedef void (*qmw_telemetry_callback_t)(const qmw_telemetry_event_t* event, void* user_data);

qmw_engine_handle_t* qmw_engine_create(void);
void qmw_engine_destroy(qmw_engine_handle_t* handle);

bool qmw_engine_start(qmw_engine_handle_t* handle, const qmw_training_config_t* config, const char** error_message);
void qmw_engine_stop(qmw_engine_handle_t* handle);
qmw_engine_status_t qmw_engine_status(const qmw_engine_handle_t* handle);
bool qmw_engine_poll_telemetry(qmw_engine_handle_t* handle, qmw_telemetry_event_t* out_event, uint64_t timeout_ms);
void qmw_engine_set_telemetry_callback(
    qmw_engine_handle_t* handle, qmw_telemetry_callback_t callback, void* user_data);

#ifdef __cplusplus
}
#endif
