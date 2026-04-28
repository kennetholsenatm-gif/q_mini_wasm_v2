// Training DLL API - REAL AutonomousTrainingPipeline Implementation
// Uses the full DataSynthesizer + ForwardForward + MoE + BettiExtractor pipeline

#define NOMINMAX  // Disable Windows min/max macros
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <thread>
#include <atomic>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <exception>

#ifdef TRAINING_API_EXPORTS
#define TRAINING_API __declspec(dllexport)
#else
#define TRAINING_API __declspec(dllimport)
#endif

// Include the REAL pipeline
#include "core/training/autonomous_training_pipeline.hpp"
#include "core/training/data_synthesizer.hpp"
#include "../common/sycl_dll_bootstrap.hpp"

using namespace q_mini_wasm_v2::core::training;

#if defined(_WIN32)
namespace {

std::once_flag g_crash_hooks_installed;
std::terminate_handler g_prev_terminate = nullptr;

static void append_training_crash_log(const char* prefix, const char* detail) noexcept {
    char localappdata[MAX_PATH]{};
    const DWORD n = GetEnvironmentVariableA("LOCALAPPDATA", localappdata, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        return;
    }
    char path[MAX_PATH + 48]{};
    sprintf_s(path, "%s\\q_mini_training_crash.log", localappdata);
    FILE* f = nullptr;
    if (fopen_s(&f, path, "a") != 0 || !f) {
        return;
    }
    fprintf(f, "%s %s\n", prefix, detail ? detail : "");
    fclose(f);
    fprintf(stderr, "%s %s\nSee %%LOCALAPPDATA%%\\q_mini_training_crash.log\n", prefix,
            detail ? detail : "");
    fflush(stderr);
}

static LONG WINAPI qmini_vectored_exception(EXCEPTION_POINTERS* ep) {
    if (!ep || !ep->ExceptionRecord) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    const DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code == EXCEPTION_BREAKPOINT || code == 0x40010006) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    // MSVC C++ throw / catch uses 0xE06D7363 ("msc"); log = one line per throw (huge spam, not a hard fault).
    if (code == 0xE06D7363) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    char detail[320]{};
    sprintf_s(detail, "vectored SEH code=0x%08lX rip=%p",
              static_cast<unsigned long>(code),
              ep->ExceptionRecord->ExceptionAddress);
    append_training_crash_log("[q_training.dll]", detail);
    return EXCEPTION_CONTINUE_SEARCH;
}

static void qmini_on_terminate() noexcept {
    append_training_crash_log("[q_training.dll]", "std::terminate() uncaught exception");
    if (g_prev_terminate && g_prev_terminate != qmini_on_terminate) {
        g_prev_terminate();
    }
    std::abort();
}

static void install_training_crash_hooks() {
    std::call_once(g_crash_hooks_installed, [] {
        AddVectoredExceptionHandler(1, qmini_vectored_exception);
        g_prev_terminate = std::set_terminate(qmini_on_terminate);
        fprintf(stderr,
                "[q_training.dll] crash hooks: MSVC C++ exception code 0xE06D7363 is not logged "
                "(useful hard faults still go to %%LOCALAPPDATA%%\\q_mini_training_crash.log)\n");
    });
}

} // namespace
#endif

extern "C" {

// ============================================================================
// REAL Training Session with AutonomousTrainingPipeline
// ============================================================================

struct TrainingSession {
    uint64_t id;
    std::atomic<bool> active;
    std::atomic<uint32_t> current_epoch;
    std::atomic<uint32_t> total_epochs;
    std::atomic<double> current_loss;
    std::atomic<uint64_t> samples_processed;
    std::atomic<uint32_t> loop_count;
    
    // REAL pipeline (constructed uninitialized; initialize() runs in StartTraining)
    std::unique_ptr<AutonomousTrainingPipeline> pipeline;
    PipelineMetrics last_metrics;
    std::mutex metrics_mutex;
    
    /** Full config from host/TOML; data_path and num_epochs applied at start. */
    PipelineConfig stored_config;

    /** After first successful initialize() or import, StartTraining skips full re-init to preserve weights. */
    bool pipeline_materialized = false;
    
    // Session fields mirrored for progress / restart
    uint32_t num_experts = 0;
    uint32_t top_k = 0;
    uint32_t num_layers = 0;
    uint32_t batch_size = 0;
    bool lazy_init = false;
    bool continuous_mode = false;
    std::string data_path;
    bool data_accumulation = false;
};

std::mutex g_sessions_mutex;
std::vector<std::unique_ptr<TrainingSession>> g_sessions;
uint64_t g_next_session_id = 1;

// ============================================================================
// Metrics Callback from Pipeline
// ============================================================================

static double loss_proxy_from_metrics(const PipelineMetrics& metrics) {
    // ff_* may be refreshed mid–micro-batch (running averages); ff_total_train_calls only increments when a batch completes.
    const bool has_ff_signal = (metrics.ff_total_train_calls > 0) || (metrics.ff_route_steps_current_batch > 0) ||
                               (metrics.train_batch_rows_done > 0) || (metrics.ff_positive_goodness > 0) ||
                               (metrics.ff_negative_goodness > 0) || (metrics.ff_goodness_delta != 0);
    if (!has_ff_signal) {
        return 0.0;
    }
    return 100.0 - (static_cast<double>(metrics.ff_goodness_delta) / 1000.0);
}

static void on_pipeline_metrics(TrainingSession* session, const PipelineMetrics& metrics) {
    std::lock_guard<std::mutex> lock(session->metrics_mutex);
    session->last_metrics = metrics;
    session->current_epoch.store(static_cast<uint32_t>(metrics.current_epoch));
    // Never substitute ingestion counters for trained contrastive rows (strict real counts only).
    session->samples_processed.store(metrics.samples_processed);
    session->current_loss.store(loss_proxy_from_metrics(metrics));
    
    const size_t ds_items = metrics.ds_total_acquired + metrics.ds_total_perturbed;
    const double loss = loss_proxy_from_metrics(metrics);
    const uint64_t train_epoch_rows = metrics.samples_processed;
    const uint64_t train_total_rows = metrics.samples_processed_total;
    printf("[Pipeline] Epoch %llu: goodness_proxy=%.4f, "
           "train_samples_epoch=%llu train_samples_total=%llu "
           "train_rows_in_batch=%u/%u ff_routes_in_batch=%llu "
           "collect_effective=%u route_topk=%u "
           "ds_items=%zu queue_hint=%zu prefill_tgt=%zu, "
           "ff_total_train_calls=%llu current_batch=%llu last_betti_eval_batch=%llu, "
           "route_t5=%d router_sycl=%d betti=[%u,%u,%u], graph=%s\n",
           static_cast<unsigned long long>(metrics.current_epoch), loss,
           static_cast<unsigned long long>(train_epoch_rows),
           static_cast<unsigned long long>(train_total_rows),
           static_cast<unsigned>(metrics.train_batch_rows_done),
           static_cast<unsigned>(metrics.train_batch_collect_limit),
           static_cast<unsigned long long>(metrics.ff_route_steps_current_batch),
           static_cast<unsigned>(metrics.train_collect_limit_effective),
           static_cast<unsigned>(metrics.route_topk_effective),
           ds_items,
           metrics.prefill_current_samples,
           metrics.prefill_target_samples,
           static_cast<unsigned long long>(metrics.ff_total_train_calls),
           static_cast<unsigned long long>(metrics.current_batch),
           static_cast<unsigned long long>(metrics.last_betti_eval_batch),
           metrics.used_tritpack5_input_route ? 1 : 0,
           metrics.router_sycl_path_used ? 1 : 0,
           metrics.betti_beta_0, metrics.betti_beta_1, metrics.betti_beta_2,
           metrics.graph_topology.c_str());
}

// ============================================================================
// DLL API Implementation - REAL PIPELINE
// ============================================================================

TRAINING_API int Training_InitSession(
    uint64_t* session_id_out,
    uint32_t num_experts,
    uint32_t top_k,
    uint32_t num_layers,
    uint32_t batch_size,
    bool lazy_init,
    bool continuous_mode,
    uint32_t epochs,
    double learning_rate,
    uint32_t checkpoint_interval,
    uint32_t context_window,
    uint32_t entanglement_tokens,
    uint32_t shadow_dim,
    uint32_t neurons_per_layer,
    uint32_t routing_qutrits,
    bool steane_correction,
    bool flash_cim,
    uint32_t worker_threads,
    const char* data_sources_toml_path,
    uint32_t moe_input_dim,
    uint32_t moe_output_dim,
    uint32_t moe_hidden_dim,
    uint32_t moe_expert_internal_layers,
    uint32_t moe_ff_active_internal_layers,
    uint32_t prefill_target_samples,
    uint32_t prefill_timeout_ms,
    uint32_t prefill_poll_ms,
    uint32_t max_acquisition_queue_depth,
    uint32_t max_raw_queue_depth,
    uint32_t max_train_queue_depth,
    uint64_t samples_per_epoch_or_zero,
    uint32_t training_micro_batch_cap,
    uint32_t training_collect_floor,
    bool training_timing_to_stderr,
    bool training_serial_experts,
    uint32_t training_sycl_route_mode,
    uint32_t training_sycl_trit_quant_min_moe_dim,
    uint32_t training_goodness_log_level,
    uint32_t training_allow_generated_negatives,
    uint64_t directory_max_lines,
    uint64_t max_jsonl_local_samples,
    uint32_t min_text_length,
    uint32_t max_text_length,
    uint32_t acquisition_threads,
    uint32_t perturbation_threads,
    uint32_t checkpoint_async_queue_max,
    uint32_t collect_empty_backoff_base_ms,
    uint32_t collect_empty_backoff_max_shift,
    uint32_t collect_empty_backoff_cap_ms,
    uint32_t metrics_heartbeat_sec
) {
#if defined(_WIN32)
    install_training_crash_hooks();
    printf("[Training] crash_hooks_installed=1: MSVC C++ SEH 0xE06D7363 is filtered "
           "(see stderr banner; %%LOCALAPPDATA%%\\q_mini_training_crash.log stays small unless hard fault)\n");
#endif
    q_mini_wasm_v2::dll::common::qmini_dll_touch_sycl_device_once();
    (void)context_window;
    (void)entanglement_tokens;
    
    std::lock_guard<std::mutex> lock(g_sessions_mutex);
    
    printf("[Training] Training_InitSession: staging AutonomousTrainingPipeline (init deferred to StartTraining)\n");
    printf("[Training]   experts=%u, top_k=%u, layers=%u, batch_size=%u, expert_internal=%u, ff_active_internal_cap=%u\n",
           num_experts, top_k, num_layers, batch_size,
           static_cast<unsigned>(moe_expert_internal_layers),
           static_cast<unsigned>(moe_ff_active_internal_layers));
    printf("[Training]   epochs=%u, lr=%.4f, checkpoint_interval_epochs=%u\n",
           epochs, learning_rate, checkpoint_interval);
    
    auto session = std::make_unique<TrainingSession>();
    session->id = g_next_session_id++;
    session->active.store(false);
    session->data_path.clear();
    session->total_epochs.store(epochs);
    session->current_loss.store(0.0);
    session->samples_processed.store(0);
    
    session->num_experts = num_experts;
    session->top_k = top_k;
    session->num_layers = num_layers;
    session->batch_size = batch_size;
    session->lazy_init = lazy_init;
    session->continuous_mode = continuous_mode;
    
    try {
        if (samples_per_epoch_or_zero == 0ull) {
            printf("[Training] ERROR: samples_per_epoch must be >= 1 (set training.samples_per_epoch in TOML)\n");
            return -5;
        }
        if (training_micro_batch_cap == 0u || training_collect_floor == 0u) {
            printf("[Training] ERROR: training.micro_batch_cap and training.collect_floor must be >= 1\n");
            return -6;
        }
        if (num_experts == 0u || top_k == 0u || num_layers == 0u || batch_size == 0u ||
            routing_qutrits == 0u || moe_input_dim == 0u || moe_output_dim == 0u || moe_hidden_dim == 0u ||
            moe_expert_internal_layers == 0u) {
            printf("[Training] ERROR: experts/top_k/layers/batch_size/routing_qutrits/MoE dims/expert_internal_layers must be >= 1\n");
            return -7;
        }
        if (top_k > num_experts) {
            printf("[Training] ERROR: model.moe_top_k must be <= model.moe_experts\n");
            return -7;
        }
        if (worker_threads == 0u) {
            printf("[Training] ERROR: features.worker_threads must be >= 1\n");
            return -8;
        }
        if (prefill_target_samples < 1u || prefill_timeout_ms < 1u || prefill_poll_ms < 1u ||
            max_acquisition_queue_depth < 1u || max_raw_queue_depth < 1u || max_train_queue_depth < 1u) {
            printf("[Training] ERROR: prefill/queue limits invalid (all must be >= 1)\n");
            return -9;
        }
        if (acquisition_threads < 1u || perturbation_threads < 1u || checkpoint_async_queue_max < 1u) {
            printf("[Training] ERROR: training.acquisition_threads, training.perturbation_threads, "
                   "training.checkpoint_async_queue_max must be >= 1\n");
            return -14;
        }
        if (training_sycl_route_mode > 2u) {
            printf("[Training] ERROR: training.sycl_route_mode must be 0(auto),1(on),2(off)\n");
            return -11;
        }
        if (training_sycl_trit_quant_min_moe_dim == 0u) {
            printf("[Training] ERROR: training.sycl_trit_quant_min_moe_dim must be >= 1\n");
            return -12;
        }
        if (directory_max_lines == 0ull || max_jsonl_local_samples == 0ull || min_text_length == 0u ||
            max_text_length < 1u || min_text_length > max_text_length) {
            printf("[Training] ERROR: local corpus limits invalid (directory_max_lines>=1, max_jsonl>=1, "
                   "min_text_length>=1, max_text_length>=1, min_text_length<=max_text_length)\n");
            printf("[Training]   received: directory_max_lines=%llu max_jsonl_local_samples=%llu "
                   "min_text_length=%u max_text_length=%u (if zeros with valid TOML, rebuild qminiwasm + q_training; "
                   "CGO bool-before-uint64 ABI was fixed to uint32 for allow_generated_negatives)\n",
                   static_cast<unsigned long long>(directory_max_lines),
                   static_cast<unsigned long long>(max_jsonl_local_samples),
                   static_cast<unsigned>(min_text_length),
                   static_cast<unsigned>(max_text_length));
            return -13;
        }

        PipelineConfig cfg;
        cfg.moe_num_experts = num_experts;
        cfg.moe_top_k = top_k;
        cfg.ff_num_layers = num_layers;
        cfg.batch_size = batch_size;
        cfg.num_epochs = epochs;
        cfg.learning_rate = static_cast<float>(learning_rate);
        cfg.checkpoint_interval = checkpoint_interval;
        cfg.enable_continuous_mode = continuous_mode;
        cfg.ff_layer_width = neurons_per_layer;
        cfg.shadow_dim = shadow_dim;
        cfg.enable_steane_correction = steane_correction;
        cfg.enable_error_correction = steane_correction;
        cfg.enable_flash_cim = flash_cim;
        cfg.routing_qutrits = routing_qutrits;
        cfg.moe_input_dim = moe_input_dim;
        cfg.moe_output_dim = moe_output_dim;
        cfg.moe_hidden_dim = moe_hidden_dim;
        cfg.moe_expert_internal_layers = moe_expert_internal_layers;
        cfg.moe_ff_active_internal_layers = static_cast<size_t>(moe_ff_active_internal_layers);
        cfg.ff_learning_rate_step = std::max(1u, static_cast<uint32_t>(std::llround(std::max(1.0, learning_rate))));
        
        cfg.acquisition_threads = static_cast<size_t>(acquisition_threads);
        cfg.perturbation_threads = static_cast<size_t>(perturbation_threads);
        cfg.checkpoint_async_queue_max = static_cast<size_t>(checkpoint_async_queue_max);
        cfg.collect_empty_backoff_base_ms = collect_empty_backoff_base_ms;
        cfg.collect_empty_backoff_max_shift = collect_empty_backoff_max_shift;
        cfg.collect_empty_backoff_cap_ms = collect_empty_backoff_cap_ms;
        cfg.metrics_heartbeat_sec = metrics_heartbeat_sec;
        printf("[Training]   features.worker_threads=%u (OMP hint) acquisition_threads=%zu perturbation_threads=%zu "
               "checkpoint_async_queue_max=%zu collect_empty_backoff(base/max_shift/cap_ms)=%u/%u/%u "
               "metrics_heartbeat_sec=%u\n",
               static_cast<unsigned>(worker_threads),
               cfg.acquisition_threads,
               cfg.perturbation_threads,
               cfg.checkpoint_async_queue_max,
               static_cast<unsigned>(cfg.collect_empty_backoff_base_ms),
               static_cast<unsigned>(cfg.collect_empty_backoff_max_shift),
               static_cast<unsigned>(cfg.collect_empty_backoff_cap_ms),
               static_cast<unsigned>(cfg.metrics_heartbeat_sec));

        cfg.samples_per_epoch = static_cast<size_t>(samples_per_epoch_or_zero);
        cfg.training_micro_batch_cap = static_cast<size_t>(training_micro_batch_cap);
        cfg.training_collect_floor = static_cast<size_t>(training_collect_floor);
        cfg.training_timing_to_stderr = training_timing_to_stderr;
        cfg.training_serial_experts = training_serial_experts;
        cfg.training_sycl_route_mode =
            static_cast<q_mini_wasm_v2::core::moe::SyclRouteMode>(training_sycl_route_mode);
        cfg.sycl_trit_quant_min_moe_dim = static_cast<size_t>(training_sycl_trit_quant_min_moe_dim);
        cfg.goodness_log_level = training_goodness_log_level;
        cfg.allow_generated_negatives = (training_allow_generated_negatives != 0u);
        cfg.directory_max_lines = static_cast<size_t>(directory_max_lines);
        cfg.max_jsonl_local_samples = static_cast<size_t>(max_jsonl_local_samples);
        cfg.min_text_length = static_cast<size_t>(min_text_length);
        cfg.max_text_length = static_cast<size_t>(max_text_length);
        printf("[Training]   samples_per_epoch=%zu (training.samples_per_epoch from TOML)\n", cfg.samples_per_epoch);
        printf("[Training]   directory_max_lines=%zu max_jsonl_local_samples=%zu min_text_length=%zu max_text_length=%zu\n",
               cfg.directory_max_lines,
               cfg.max_jsonl_local_samples,
               cfg.min_text_length,
               cfg.max_text_length);
        printf("[Training]   micro_batch_cap=%zu collect_floor=%zu timing_to_stderr=%d serial_expert_train=%d sycl_route_mode=%u sycl_trit_quant_min_moe_dim=%zu goodness_log_level=%u\n",
               cfg.training_micro_batch_cap,
               cfg.training_collect_floor,
               training_timing_to_stderr ? 1 : 0,
               training_serial_experts ? 1 : 0,
               static_cast<unsigned>(training_sycl_route_mode),
               cfg.sycl_trit_quant_min_moe_dim,
               static_cast<unsigned>(cfg.goodness_log_level));
        cfg.enable_prefill_ring_buffer = true;
        cfg.prefill_target_samples = static_cast<size_t>(prefill_target_samples);
        cfg.prefill_timeout_ms = prefill_timeout_ms;
        cfg.prefill_poll_ms = prefill_poll_ms;
        cfg.max_acquisition_queue_depth = static_cast<size_t>(max_acquisition_queue_depth);
        cfg.max_raw_queue_depth = static_cast<size_t>(max_raw_queue_depth);
        cfg.max_train_queue_depth = static_cast<size_t>(max_train_queue_depth);
        
        if (data_sources_toml_path && data_sources_toml_path[0] != '\0') {
            cfg.data_sources_toml_path = data_sources_toml_path;
        }

        cfg.lazy_moe_experts = lazy_init;
        
        session->stored_config = cfg;
        session->pipeline = create_training_pipeline();
        if (!session->pipeline) {
            printf("[Training] ERROR: create_training_pipeline returned null\n");
            return -1;
        }
        
        TrainingSession* session_ptr = session.get();
        session->pipeline->on_metrics_update([session_ptr](const PipelineMetrics& metrics) {
            on_pipeline_metrics(session_ptr, metrics);
        });
        
        printf("[Training] Session %llu: pipeline object created (initialize on StartTraining)\n",
               static_cast<unsigned long long>(session->id));
        
        *session_id_out = session->id;
        g_sessions.push_back(std::move(session));
        
        return 0;
    } catch (const std::exception& e) {
        printf("[Training] ERROR creating pipeline: %s\n", e.what());
        return -1;
    }
}

TRAINING_API int Training_StartTraining(
    uint64_t session_id,
    uint32_t epochs,
    const char* data_path,
    bool enable_data_accumulation
) {
    std::lock_guard<std::mutex> lock(g_sessions_mutex);
    
    TrainingSession* session = nullptr;
    for (auto& s : g_sessions) {
        if (s->id == session_id) {
            session = s.get();
            break;
        }
    }
    
    if (!session) {
        printf("[Training] ERROR: Session %llu not found\n", static_cast<unsigned long long>(session_id));
        return -1;
    }
    
    if (session->active.load()) {
        printf("[Training] ERROR: Session %llu already training\n", static_cast<unsigned long long>(session_id));
        return -2;
    }
    
    session->total_epochs.store(epochs);
    session->data_path = data_path ? data_path : "";
    session->data_accumulation = enable_data_accumulation;
    
    printf("[Training] Session %llu: Starting REAL training for %u epochs\n",
           static_cast<unsigned long long>(session_id), epochs);
    printf("[Training] Data path: %s\n", data_path ? data_path : "(none)");
    
    PipelineConfig config = session->stored_config;
    config.data_path = session->data_path;
    config.num_epochs = epochs;

    if (!session->pipeline_materialized) {
        if (!session->pipeline->initialize(config)) {
            printf("[Training] ERROR: Pipeline initialization failed\n");
            return -3;
        }
        session->pipeline_materialized = true;
    } else {
        session->pipeline->apply_run_overrides(session->data_path, epochs);
    }

    if (!session->pipeline->start_training()) {
        printf("[Training] ERROR: Pipeline start_training failed\n");
        return -4;
    }
    session->active.store(true);
    
    printf("[Training] Session %llu: AutonomousTrainingPipeline STARTED\n",
           static_cast<unsigned long long>(session_id));
    printf("[Training]   - Data: local dir or data_sources.toml per pipeline config\n");
    printf("[Training]   - ForwardForward + MoE (%u experts)\n", session->num_experts);
    
    return 0;
}

TRAINING_API int Training_GetProgress(
    uint64_t session_id,
    uint32_t* current_epoch_out,
    uint32_t* total_epochs_out,
    double* current_loss_out,
    uint64_t* samples_processed_out,
    bool* is_running_out,
    uint32_t* loop_count_out
) {
    std::lock_guard<std::mutex> lock(g_sessions_mutex);
    
    TrainingSession* session = nullptr;
    for (auto& s : g_sessions) {
        if (s->id == session_id) {
            session = s.get();
            break;
        }
    }
    
    if (!session) {
        return -1;
    }

    // Prefer live pipeline metrics via get_metrics() (FF averages update during a micro-batch).
    bool running = false;
    if (session->pipeline) {
        const PipelineMetrics m = session->pipeline->get_metrics();
        const double loss = loss_proxy_from_metrics(m);
        auto state = session->pipeline->get_state();
        running = (state == PipelineState::TRAINING ||
                   state == PipelineState::ACQUIRING_DATA ||
                   state == PipelineState::PAUSED ||
                   state == PipelineState::INITIALIZING ||
                   state == PipelineState::STOPPING);
        {
            std::lock_guard<std::mutex> metrics_lock(session->metrics_mutex);
            session->last_metrics = m;
            session->current_epoch.store(static_cast<uint32_t>(m.current_epoch));
            session->samples_processed.store(m.samples_processed);
            session->loop_count.store(m.loop_count);
            session->current_loss.store(loss);
        }
    } else {
        std::lock_guard<std::mutex> metrics_lock(session->metrics_mutex);
        session->current_epoch.store(static_cast<uint32_t>(session->last_metrics.current_epoch));
        session->samples_processed.store(session->last_metrics.samples_processed);
        session->loop_count.store(session->last_metrics.loop_count);
        session->current_loss.store(loss_proxy_from_metrics(session->last_metrics));
    }
    session->active.store(running);

    *current_epoch_out = session->current_epoch.load();
    *total_epochs_out = session->total_epochs.load();
    *current_loss_out = session->current_loss.load();
    *samples_processed_out = session->samples_processed.load();
    *is_running_out = running;
    *loop_count_out = session->loop_count.load();

    return 0;
}

TRAINING_API int Training_StopTraining(uint64_t session_id) {
    std::lock_guard<std::mutex> lock(g_sessions_mutex);
    
    TrainingSession* session = nullptr;
    for (auto& s : g_sessions) {
        if (s->id == session_id) {
            session = s.get();
            break;
        }
    }
    
    if (!session) {
        return -1;
    }
    
    if (session->pipeline) {
        session->pipeline->stop_training();
    }
    session->active.store(false);
    
    printf("[Training] Session %llu: Training stopped\n", static_cast<unsigned long long>(session_id));
    
    return 0;
}

TRAINING_API int Training_CleanupSession(uint64_t session_id) {
    std::lock_guard<std::mutex> lock(g_sessions_mutex);
    
    auto it = std::remove_if(g_sessions.begin(), g_sessions.end(),
        [session_id](const std::unique_ptr<TrainingSession>& s) {
            return s->id == session_id;
        });
    
    if (it != g_sessions.end()) {
        if ((*it)->pipeline) {
            (*it)->pipeline->stop_training();
        }
        (*it)->active.store(false);
        g_sessions.erase(it, g_sessions.end());
        printf("[Training] Session %llu: Cleaned up\n", static_cast<unsigned long long>(session_id));
        return 0;
    }
    
    return -1;
}

TRAINING_API int Training_ExportCheckpoint(uint64_t session_id, const char* path_utf8) {
    std::lock_guard<std::mutex> lock(g_sessions_mutex);

    TrainingSession* session = nullptr;
    for (auto& s : g_sessions) {
        if (s->id == session_id) {
            session = s.get();
            break;
        }
    }
    if (!session || !session->pipeline) {
        return -1;
    }
    if (session->active.load()) {
        return -2;
    }
    if (!path_utf8 || path_utf8[0] == '\0') {
        return -4;
    }
    if (!session->pipeline->export_model(path_utf8)) {
        return -3;
    }
    return 0;
}

TRAINING_API int Training_ImportCheckpoint(uint64_t session_id, const char* path_utf8) {
    std::lock_guard<std::mutex> lock(g_sessions_mutex);

    TrainingSession* session = nullptr;
    for (auto& s : g_sessions) {
        if (s->id == session_id) {
            session = s.get();
            break;
        }
    }
    if (!session || !session->pipeline) {
        return -1;
    }
    if (session->active.load()) {
        return -2;
    }
    if (!path_utf8 || path_utf8[0] == '\0') {
        return -4;
    }
    if (!session->pipeline->import_model(path_utf8)) {
        session->pipeline_materialized = false;
        return -3;
    }
    session->stored_config = session->pipeline->get_config();
    session->pipeline_materialized = true;
    return 0;
}

TRAINING_API void Training_GetVersion(char* version_out, size_t max_len) {
    if (!version_out || max_len == 0) {
        return;
    }
    // Compile-time stamp so hosts can verify they picked up the intended DLL build.
    (void)snprintf(version_out, max_len, "q_training_v3.10_metrics_ff %s %s", __DATE__, __TIME__);
    version_out[max_len - 1] = '\0';
}

TRAINING_API int Training_GetMetrics(
    uint64_t session_id,
    uint32_t* experts_active_out,
    uint32_t* data_acquired_out,
    uint32_t* data_perturbed_out,
    uint32_t* api_failures_out,
    uint32_t* betti_0_out,
    uint32_t* betti_1_out,
    uint32_t* betti_2_out,
    uint32_t* topic_frontier_size_out,
    uint32_t* topic_frontier_max_out,
    uint32_t* topic_frontier_evictions_out,
    uint32_t* ds_queue_depth_out,
    uint32_t* ds_raw_queue_depth_out,
    uint32_t* ds_raw_queue_max_out,
    uint32_t* ds_train_queue_max_out,
    uint32_t* ds_acq_queue_depth_out,
    uint32_t* ds_acq_queue_max_out,
    uint64_t* ds_blocked_raw_pushes_out,
    uint64_t* ds_blocked_train_pushes_out,
    uint64_t* ds_blocked_wait_ms_out,
    uint64_t* ds_dropped_payloads_out,
    uint64_t* ds_acq_blocked_pushes_out,
    uint64_t* ds_acq_blocked_wait_ms_out,
    uint64_t* samples_total_out,
    uint64_t* ds_acq_dropped_too_short_out,
    uint64_t* gf3_hebbian_weight_cell_updates_out,
    char* pipeline_status_utf8_out,
    size_t pipeline_status_utf8_cap
) {
    std::lock_guard<std::mutex> lock(g_sessions_mutex);
    
    TrainingSession* session = nullptr;
    for (auto& s : g_sessions) {
        if (s->id == session_id) {
            session = s.get();
            break;
        }
    }
    
    if (!session) {
        return -1;
    }
    
    std::lock_guard<std::mutex> metrics_lock(session->metrics_mutex);
    *experts_active_out = static_cast<uint32_t>(session->last_metrics.expert_utilization.size());
    *data_acquired_out = static_cast<uint32_t>(session->last_metrics.ds_total_acquired);
    *data_perturbed_out = static_cast<uint32_t>(session->last_metrics.ds_total_perturbed);
    *api_failures_out = static_cast<uint32_t>(session->last_metrics.ds_api_failures);
    *betti_0_out = session->last_metrics.betti_beta_0;
    *betti_1_out = session->last_metrics.betti_beta_1;
    *betti_2_out = session->last_metrics.betti_beta_2;
    *topic_frontier_size_out = static_cast<uint32_t>(session->last_metrics.ds_topic_frontier_size);
    *topic_frontier_max_out = static_cast<uint32_t>(session->last_metrics.ds_topic_frontier_max);
    *topic_frontier_evictions_out = static_cast<uint32_t>(session->last_metrics.ds_topic_frontier_evictions);
    *ds_queue_depth_out = static_cast<uint32_t>(session->last_metrics.ds_queue_depth);
    *ds_raw_queue_depth_out = static_cast<uint32_t>(session->last_metrics.ds_raw_queue_depth);
    *ds_raw_queue_max_out = static_cast<uint32_t>(session->last_metrics.ds_raw_queue_max);
    *ds_train_queue_max_out = static_cast<uint32_t>(session->last_metrics.ds_train_queue_max);
    *ds_acq_queue_depth_out = static_cast<uint32_t>(session->last_metrics.ds_acquisition_queue_depth);
    *ds_acq_queue_max_out = static_cast<uint32_t>(session->last_metrics.ds_acquisition_queue_max);
    *ds_blocked_raw_pushes_out = session->last_metrics.ds_blocked_raw_pushes;
    *ds_blocked_train_pushes_out = session->last_metrics.ds_blocked_train_pushes;
    *ds_blocked_wait_ms_out = session->last_metrics.ds_blocked_wait_ms;
    *ds_dropped_payloads_out = session->last_metrics.ds_dropped_payloads;
    *ds_acq_blocked_pushes_out = session->last_metrics.ds_acquisition_blocked_pushes;
    *ds_acq_blocked_wait_ms_out = session->last_metrics.ds_acquisition_blocked_wait_ms;
    *samples_total_out = session->last_metrics.samples_processed_total;
    *ds_acq_dropped_too_short_out = session->last_metrics.ds_acquisition_dropped_too_short;
    *gf3_hebbian_weight_cell_updates_out = session->last_metrics.gf3_hebbian_weight_cell_updates;

    if (pipeline_status_utf8_out && pipeline_status_utf8_cap > 0) {
        const std::string& st = session->last_metrics.status_message;
        strncpy(pipeline_status_utf8_out, st.c_str(), pipeline_status_utf8_cap - 1);
        pipeline_status_utf8_out[pipeline_status_utf8_cap - 1] = '\0';
    }

    return 0;
}

TRAINING_API int Training_GetIngestionStats(
    uint64_t session_id,
    uint64_t* samples_total_out,
    uint32_t* ds_queue_depth_out,
    uint32_t* ds_raw_queue_depth_out,
    uint32_t* ds_raw_queue_max_out,
    uint32_t* ds_train_queue_max_out,
    uint32_t* ds_acq_queue_depth_out,
    uint32_t* ds_acq_queue_max_out,
    uint64_t* ds_blocked_raw_pushes_out,
    uint64_t* ds_blocked_train_pushes_out,
    uint64_t* ds_blocked_wait_ms_out,
    uint64_t* ds_dropped_payloads_out,
    uint64_t* ds_acq_blocked_pushes_out,
    uint64_t* ds_acq_blocked_wait_ms_out
) {
    std::lock_guard<std::mutex> lock(g_sessions_mutex);

    TrainingSession* session = nullptr;
    for (auto& s : g_sessions) {
        if (s->id == session_id) {
            session = s.get();
            break;
        }
    }
    if (!session) {
        return -1;
    }

    std::lock_guard<std::mutex> metrics_lock(session->metrics_mutex);
    const PipelineMetrics& m = session->last_metrics;
    *samples_total_out = m.samples_processed_total;
    *ds_queue_depth_out = static_cast<uint32_t>(m.ds_queue_depth);
    *ds_raw_queue_depth_out = static_cast<uint32_t>(m.ds_raw_queue_depth);
    *ds_raw_queue_max_out = static_cast<uint32_t>(m.ds_raw_queue_max);
    *ds_train_queue_max_out = static_cast<uint32_t>(m.ds_train_queue_max);
    *ds_acq_queue_depth_out = static_cast<uint32_t>(m.ds_acquisition_queue_depth);
    *ds_acq_queue_max_out = static_cast<uint32_t>(m.ds_acquisition_queue_max);
    *ds_blocked_raw_pushes_out = m.ds_blocked_raw_pushes;
    *ds_blocked_train_pushes_out = m.ds_blocked_train_pushes;
    *ds_blocked_wait_ms_out = m.ds_blocked_wait_ms;
    *ds_dropped_payloads_out = m.ds_dropped_payloads;
    *ds_acq_blocked_pushes_out = m.ds_acquisition_blocked_pushes;
    *ds_acq_blocked_wait_ms_out = m.ds_acquisition_blocked_wait_ms;
    return 0;
}

} // extern "C"
