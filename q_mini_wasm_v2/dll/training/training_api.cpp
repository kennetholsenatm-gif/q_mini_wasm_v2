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

#ifdef TRAINING_API_EXPORTS
#define TRAINING_API __declspec(dllexport)
#else
#define TRAINING_API __declspec(dllimport)
#endif

// Include the REAL pipeline
#include "core/training/autonomous_training_pipeline.hpp"
#include "core/training/data_synthesizer.hpp"

using namespace q_mini_wasm_v2::core::training;

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

static void on_pipeline_metrics(TrainingSession* session, const PipelineMetrics& metrics) {
    std::lock_guard<std::mutex> lock(session->metrics_mutex);
    session->last_metrics = metrics;
    session->current_epoch.store(static_cast<uint32_t>(metrics.current_epoch));
    session->samples_processed.store(metrics.samples_processed);
    
    // Display scalar derived from tropical goodness delta (not cross-entropy loss).
    double loss = 0.0;
    if (metrics.ff_positive_goodness > 0) {
        loss = 100.0 - (static_cast<double>(metrics.ff_goodness_delta) / 1000.0);
    }
    session->current_loss.store(loss);
    
    const size_t ds_items = metrics.ds_total_acquired + metrics.ds_total_perturbed;
    printf("[Pipeline] Epoch %llu: goodness_proxy=%.4f, samples=%zu, ds_items=%zu, betti=[%u,%u,%u], graph=%s\n",
           static_cast<unsigned long long>(metrics.current_epoch), loss,
           metrics.samples_processed,
           ds_items,
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
    uint32_t moe_expert_internal_layers
) {
    (void)context_window;
    (void)entanglement_tokens;
    
    std::lock_guard<std::mutex> lock(g_sessions_mutex);
    
    printf("[Training] Training_InitSession: staging AutonomousTrainingPipeline (init deferred to StartTraining)\n");
    printf("[Training]   experts=%u, top_k=%u, layers=%u, batch_size=%u\n",
           num_experts, top_k, num_layers, batch_size);
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
        cfg.routing_qutrits = routing_qutrits > 0 ? routing_qutrits : 16u;
        cfg.moe_input_dim = moe_input_dim > 0 ? moe_input_dim : neurons_per_layer;
        cfg.moe_output_dim = moe_output_dim > 0 ? moe_output_dim : neurons_per_layer;
        cfg.moe_hidden_dim = moe_hidden_dim > 0 ? moe_hidden_dim : std::max(neurons_per_layer * 2u, 128u);
        cfg.moe_expert_internal_layers = moe_expert_internal_layers > 0 ? moe_expert_internal_layers : 2u;
        cfg.ff_learning_rate_step = std::max(1u, static_cast<uint32_t>(std::llround(std::max(1.0, learning_rate))));
        
        const uint32_t wt = std::max(1u, worker_threads);
        cfg.acquisition_threads = std::max<size_t>(1u, static_cast<size_t>(wt / 2u));
        cfg.perturbation_threads = std::max<size_t>(1u, static_cast<size_t>(wt / 4u));
        
        cfg.samples_per_epoch = std::max(cfg.batch_size * size_t{10}, size_t{1000});
        
        if (data_sources_toml_path && data_sources_toml_path[0] != '\0') {
            cfg.data_sources_toml_path = data_sources_toml_path;
        }
        
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
    
    if (!session->pipeline->initialize(config)) {
        printf("[Training] ERROR: Pipeline initialization failed\n");
        return -3;
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
    
    {
        std::lock_guard<std::mutex> metrics_lock(session->metrics_mutex);
        session->current_epoch.store(static_cast<uint32_t>(session->last_metrics.current_epoch));
        session->samples_processed.store(session->last_metrics.samples_processed);
        session->loop_count.store(session->last_metrics.loop_count);
    }
    
    bool running = false;
    if (session->pipeline) {
        auto state = session->pipeline->get_state();
        running = (state != PipelineState::IDLE &&
                   state != PipelineState::COMPLETE &&
                   state != PipelineState::FAILED);
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

TRAINING_API void Training_GetVersion(char* version_out, size_t max_len) {
    const char* version = "q_training_v3.1_deferred_init";
    strncpy(version_out, version, max_len - 1);
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
    uint32_t* betti_2_out
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
    
    return 0;
}

} // extern "C"
