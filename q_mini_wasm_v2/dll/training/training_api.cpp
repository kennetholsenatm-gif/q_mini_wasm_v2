// Training DLL API - REAL AutonomousTrainingPipeline Implementation
// Uses the full DataSynthesizer + ForwardForward + MoE + BettiExtractor pipeline

#define NOMINMAX  // Disable Windows min/max macros
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#if defined(_MSC_VER)
#include <stdlib.h> // _set_abort_behavior
#if defined(_DEBUG)
#include <crtdbg.h>
#endif
#endif
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
#include <limits>
#include <cstdlib>
#include <csignal>
#include <exception>

#ifdef TRAINING_API_EXPORTS
#define TRAINING_API __declspec(dllexport)
#else
#define TRAINING_API __declspec(dllimport)
#endif

// Include the REAL pipeline
#include "core/training/autonomous_training_pipeline.hpp"
#include "core/training/crash_breadcrumb.hpp"
#include "core/training/data_synthesizer.hpp"
#include "../common/sycl_dll_bootstrap.hpp"
#include "core/moe/router.hpp"
#include "core/moe/gf3_layers.hpp"
#if defined(USE_SYCL) && USE_SYCL
#include "core/moe/gf3_sycl_probe.hpp"
#include "core/ternary/packing.hpp"
#include "sycl/gf3_layers_sycl.hpp"
#include "sycl/tableau_kernels.hpp"
#include "training/gf3_negative_probe_sycl.hpp"
#endif

#if !defined(USE_SYCL) || !USE_SYCL
#error "q_training.dll must be built with SYCL (USE_SYCL=1). Non-SYCL training DLL builds are not supported."
#endif

using namespace q_mini_wasm_v2::core::training;

#if defined(_WIN32)
namespace {

std::once_flag g_crash_hooks_installed;
static LPTOP_LEVEL_EXCEPTION_FILTER g_prev_unhandled = nullptr;

static void format_crash_timestamp(char (&out)[40]) noexcept {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    sprintf_s(out, "%04u-%02u-%02uT%02u:%02u:%02u.%03u",
              static_cast<unsigned>(st.wYear), static_cast<unsigned>(st.wMonth),
              static_cast<unsigned>(st.wDay), static_cast<unsigned>(st.wHour),
              static_cast<unsigned>(st.wMinute), static_cast<unsigned>(st.wSecond),
              static_cast<unsigned>(st.wMilliseconds));
}

/** Append hex return addresses to @p line (NUL-terminated). */
static void append_hex_stack_to_line(char* line, size_t line_cap, unsigned frames_to_skip) noexcept {
    constexpr unsigned kMax = 48;
    void* frames[kMax]{};
    const USHORT got =
        CaptureStackBackTrace(frames_to_skip, kMax, frames, nullptr);
    if (got == 0) {
        return;
    }
    size_t off = strlen(line);
    if (off + 8 >= line_cap) {
        return;
    }
    int w = sprintf_s(line + off, line_cap - off, " stack=");
    if (w <= 0) {
        return;
    }
    off += static_cast<size_t>(w);
    for (USHORT i = 0; i < got && off + 24 < line_cap; ++i) {
        w = sprintf_s(line + off, line_cap - off, i ? "<=%p" : "%p", frames[i]);
        if (w <= 0) {
            break;
        }
        off += static_cast<size_t>(w);
    }
}

static void write_crash_line_to_disk_and_stderr(const char* line) noexcept {
    char localappdata[MAX_PATH]{};
    const DWORD n = GetEnvironmentVariableA("LOCALAPPDATA", localappdata, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        char path[MAX_PATH + 64]{};
        sprintf_s(path, "%s\\q_mini_training_crash.log", localappdata);
        FILE* f = nullptr;
        if (fopen_s(&f, path, "a") == 0 && f) {
            fprintf(f, "%s\n", line);
            fflush(f);
            fclose(f);
        }
    }
    char tmpdir[MAX_PATH]{};
    const DWORD t = GetTempPathA(MAX_PATH, tmpdir);
    if (t > 0 && t < MAX_PATH) {
        char path2[MAX_PATH + 64]{};
        sprintf_s(path2, "%sq_mini_training_crash.log", tmpdir);
        FILE* f2 = nullptr;
        if (fopen_s(&f2, path2, "a") == 0 && f2) {
            fprintf(f2, "%s\n", line);
            fflush(f2);
            fclose(f2);
        }
    }
    fprintf(stderr, "%s\n(q_training crash log: %%LOCALAPPDATA%% and %%TEMP%%\\q_mini_training_crash.log)\n", line);
    fflush(stderr);
}

static void append_training_crash_log_stack(const char* prefix, const char* detail, bool with_stack,
                                           unsigned stack_skip) noexcept {
    char line[3600]{};
    char ts[40]{};
    format_crash_timestamp(ts);
    sprintf_s(line, sizeof(line), "[%s] pid=%lu tid=%lu %s %s", ts,
              static_cast<unsigned long>(GetCurrentProcessId()),
              static_cast<unsigned long>(GetCurrentThreadId()), prefix, detail ? detail : "");
    qmini_training_breadcrumb_tail(line, sizeof(line));
    if (with_stack) {
        append_hex_stack_to_line(line, sizeof(line), stack_skip);
    }
    write_crash_line_to_disk_and_stderr(line);
}

static void append_training_crash_log(const char* prefix, const char* detail) noexcept {
    append_training_crash_log_stack(prefix, detail, false, 0);
}

#if defined(_MSC_VER) && defined(_DEBUG)
static int __cdecl qmini_crt_report_hook(int /*reportType*/, char* message, int* /*returnValue*/) {
    if (message && message[0]) {
        append_training_crash_log("[q_training.dll] CRT dbg report:", message);
    }
    return FALSE; // let CRT continue (dialog may still appear in Debug)
}
#endif

static void append_rip_module_tail(const void* rip, char* tail, size_t tail_cap) noexcept {
    if (!tail || tail_cap < 24 || !rip) {
        if (tail && tail_cap) {
            tail[0] = '\0';
        }
        return;
    }
    HMODULE hm = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                             reinterpret_cast<LPCSTR>(rip), &hm) ||
        !hm) {
        sprintf_s(tail, tail_cap, " rip_module=?");
        return;
    }
    char path[MAX_PATH]{};
    (void)GetModuleFileNameA(hm, path, MAX_PATH);
    const char* base = path;
    for (const char* p = path; *p; ++p) {
        if (*p == '\\' || *p == '/') {
            base = p + 1;
        }
    }
    const unsigned long long off = static_cast<unsigned long long>(
        reinterpret_cast<uintptr_t>(rip) - reinterpret_cast<uintptr_t>(hm));
    sprintf_s(tail, tail_cap, " rip_module=%s+0x%llX", base, off);
}

/** Log access violations / hard faults (not MSVC C++ throws). */
static void log_hard_seh_from_ep(const EXCEPTION_POINTERS* ep, const char* tag, unsigned stack_skip) noexcept {
    if (!ep || !ep->ExceptionRecord) {
        return;
    }
    const DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code == EXCEPTION_BREAKPOINT || code == 0x40010006) {
        return;
    }
    // MSVC C++ throw / catch uses 0xE06D7363 ("msc"); log = one line per throw (huge spam, not a hard fault).
    if (code == 0xE06D7363) {
        return;
    }
    char modtail[200]{};
    append_rip_module_tail(ep->ExceptionRecord->ExceptionAddress, modtail, sizeof(modtail));
    char detail[512]{};
    sprintf_s(detail, sizeof(detail), "%s code=0x%08lX rip=%p%s", tag ? tag : "SEH",
              static_cast<unsigned long>(code), ep->ExceptionRecord->ExceptionAddress, modtail);
    append_training_crash_log_stack("[q_training.dll]", detail, true, stack_skip);
}

static LONG WINAPI qmini_vectored_exception(EXCEPTION_POINTERS* ep) {
    log_hard_seh_from_ep(ep, "vectored SEH", 2u);
    return EXCEPTION_CONTINUE_SEARCH;
}

static LONG WINAPI qmini_unhandled_exception(EXCEPTION_POINTERS* ep) {
    // Last chance after other handlers; chains to prior filter (e.g. debugger / WER).
    log_hard_seh_from_ep(ep, "UNHANDLED SEH (last chance)", 2u);
    if (g_prev_unhandled) {
        return g_prev_unhandled(ep);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

static void qmini_on_sigabrt(int sig) noexcept {
    (void)sig;
    append_training_crash_log_stack("[q_training.dll]", "SIGABRT (abort/assertCRT)", true, 3u);
    std::_Exit(3);
}

static void qmini_on_terminate() noexcept {
    // Last-resort: log why terminate ran, then exit without MSVC debug "abort() has been called" dialog.
    try {
        if (std::current_exception()) {
            try {
                std::rethrow_exception(std::current_exception());
            } catch (const std::exception& ex) {
                append_training_crash_log_stack("[q_training.dll] terminate:", ex.what(), true, 3u);
            } catch (...) {
                append_training_crash_log_stack("[q_training.dll] terminate:", "non-std exception", true,
                                                 3u);
            }
        } else {
            append_training_crash_log_stack("[q_training.dll]", "std::terminate() (no active exception)",
                                           true, 3u);
        }
    } catch (...) {
        append_training_crash_log_stack("[q_training.dll]", "std::terminate() logging failed", true, 3u);
    }
    std::_Exit(3);
}

static void install_training_crash_hooks() {
    std::call_once(g_crash_hooks_installed, [] {
#if defined(_MSC_VER)
        // Reduce CRT "abort() has been called" UI noise when third-party code calls abort().
        (void)_set_abort_behavior(0u, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
#if defined(_MSC_VER) && defined(_DEBUG)
        (void)_CrtSetReportHook(qmini_crt_report_hook);
#endif
        (void)std::signal(SIGABRT, qmini_on_sigabrt);
        AddVectoredExceptionHandler(1, qmini_vectored_exception);
        g_prev_unhandled = SetUnhandledExceptionFilter(qmini_unhandled_exception);
        (void)std::set_terminate(qmini_on_terminate);
        char exe[MAX_PATH]{};
        char boot[464]{};
        if (GetModuleFileNameA(nullptr, exe, MAX_PATH) > 0) {
            sprintf_s(boot,
                      "crash hooks installed host=\"%s\" (log lines: wall time, pid, tid; stacks on SIGABRT/"
                      "terminate/SEH; MSVC throw 0xE06D7363 not logged)",
                      exe);
        } else {
            strcpy_s(boot,
                     "crash hooks installed (log lines: wall time, pid, tid; stacks on fatal hooks)");
        }
        append_training_crash_log("[q_training.dll]", boot);
    });
}

} // namespace

#if defined(QMINI_Q_TRAINING_DLL)
/** Called from dll/common/sycl_dll_bootstrap.cpp DllMain(DLL_PROCESS_ATTACH) — earliest hook point. */
extern "C" void qmini_q_training_install_hooks_on_attach(void) {
    install_training_crash_hooks();
}
#endif

#endif // _WIN32

namespace {
/** C++ exception crossed the exported C API; host should treat as fatal pipeline fault. */
constexpr int kTrainingApiNativeFault = -100;
} // namespace

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
    PipelineConfig stored_config = default_pipeline_config();

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
    // NOTE: ff_goodness_delta≈0 ⇒ this returns ~100.0 — that is *not* "perfect learning"; it usually means no pos/neg
    // contrast on the last FF averages (degenerate / collapsed discrimination). Prefer raw ff_goodness_delta in logs.
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
    const int degenerate_ff =
        (metrics.ff_route_steps_current_batch > 0u && metrics.ff_goodness_delta == 0) ? 1 : 0;
    printf("[Pipeline] Epoch %llu: goodness_proxy=%.4f, "
           "train_samples_epoch=%llu train_samples_total=%llu "
           "train_rows_in_batch=%u/%u ff_routes_in_batch=%llu "
           "collect_effective=%u route_topk=%u "
           "ds_items=%zu queue_hint=%zu prefill_tgt=%zu, "
           "ff_total_train_calls=%llu current_batch=%llu last_betti_eval_batch=%llu, "
           "ff_delta=%d ff_pos_avg=%u ff_neg_avg=%u ff_degenerate=%d, "
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
           static_cast<int>(metrics.ff_goodness_delta),
           static_cast<unsigned>(metrics.ff_positive_goodness),
           static_cast<unsigned>(metrics.ff_negative_goodness),
           degenerate_ff,
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
    uint32_t metrics_heartbeat_sec,
    uint32_t training_parallel_contrastive_rows,
    uint32_t training_parallel_batches,
    uint32_t ff_expert_chunk_size,
    uint32_t target_routes_per_batch,
    uint32_t collect_window_ms,
    uint32_t collect_min_rows_per_batch,
    uint32_t training_sycl_prereserve_gib,
    uint32_t training_sycl_prereserve_chunk_mib,
    int32_t training_sycl_gpu_device_index,
    uint64_t training_gf3_sycl_min_weight_cells,
    uint32_t training_gf3_sycl_submit_grid_log,
    uint64_t training_gf3_ff_batched_weight_mib,
    uint32_t training_ff_multi_row_batch,
    uint32_t training_ff_multi_row_slots_chunk,
    uint32_t training_gf3_ff_layers_per_batch,
    uint64_t training_lazy_moe_resident_cap,
    const char* training_checkpoint_data_dir_utf8,
    uint64_t training_gf3_ff_multislot_slots_chunk_max,
    uint32_t training_gf3_ff_multislot_ignore_host_slot_budget,
    uint32_t training_auto_resume_from_checkpoint,
    uint32_t training_parallel_batches_runtime_cap
) {
    if (!session_id_out) {
        printf("[Training] ERROR: Training_InitSession session_id_out is null\n");
        return -1;
    }
#if defined(_WIN32)
    install_training_crash_hooks();
    printf("[Training] crash_hooks_installed=1: MSVC C++ SEH 0xE06D7363 is filtered "
           "(see stderr banner; %%LOCALAPPDATA%%\\q_mini_training_crash.log stays small unless hard fault)\n");
#endif
    q_mini_wasm_v2::dll::common::qmini_dll_touch_sycl_device_once();
    (void)context_window;
    (void)entanglement_tokens;

    try {
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

        if (samples_per_epoch_or_zero == 0ull) {
            printf("[Training] ERROR: samples_per_epoch must be >= 1 (set training.samples_per_epoch in TOML)\n");
            return -5;
        }
        if (training_micro_batch_cap == 0u || training_collect_floor == 0u) {
            printf("[Training] WARN: training.micro_batch_cap/collect_floor received 0; using permissive auto values\n");
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
        // prefill_target_samples==0: native wait uses max(1,0) => exit prefill on first pair (fast start).
        if (prefill_timeout_ms < 1u || prefill_poll_ms < 1u || max_acquisition_queue_depth < 1u ||
            max_raw_queue_depth < 1u || max_train_queue_depth < 1u) {
            printf("[Training] ERROR: prefill timeout/poll/queue limits invalid (must be >= 1)\n");
            return -9;
        }
        if (acquisition_threads < 1u || perturbation_threads < 1u || checkpoint_async_queue_max < 1u) {
            printf("[Training] ERROR: training.acquisition_threads, training.perturbation_threads, "
                   "training.checkpoint_async_queue_max must be >= 1\n");
            return -14;
        }
        // Host must pass 0/1. If values are >1, treat as ABI drift (stale q_training.dll vs qminiwasm) or host bug:
        // interpret as boolean (nonzero=true) after warning instead of failing training.
        if (training_parallel_contrastive_rows > 1u) {
            printf("[Training] WARN: training.parallel_contrastive_rows raw=%u (expected 0/1); nonzero treated as "
                   "enabled — rebuild q_training.dll from the same commit as qminiwasm if this is unexpected\n",
                   training_parallel_contrastive_rows);
        }
        if (training_parallel_batches < 1u) {
            printf("[Training] ERROR: training.parallel_batches must be >= 1\n");
            return -20;
        }
        if (training_parallel_batches_runtime_cap > 65536u) {
            printf("[Training] ERROR: training.parallel_batches_runtime_cap must be <= 65536 (0 = uncapped)\n");
            return -20;
        }
        if (target_routes_per_batch < 1u) {
            printf("[Training] WARN: training.target_routes_per_batch is 0; pipeline will auto-pick route budget\n");
        }
        if (collect_window_ms < 1u) {
            printf("[Training] WARN: training.collect_window_ms is 0; forcing minimal 1ms window\n");
        }
        if (training_sycl_route_mode > 1u) {
            printf("[Training] ERROR: training.sycl_route_mode must be 0(auto) or 1(on)\n");
            return -11;
        }
        if (training_sycl_trit_quant_min_moe_dim == 0u) {
            printf("[Training] ERROR: training.sycl_trit_quant_min_moe_dim must be >= 1\n");
            return -12;
        }
        if (training_sycl_gpu_device_index < -1) {
            printf("[Training] ERROR: training.sycl_gpu_device_index must be >= -1 (use -1 for auto GPU pick)\n");
            return -20;
        }
        if (training_ff_multi_row_slots_chunk == 1u) {
            printf("[Training] ERROR: training.ff_multi_row_slots_chunk must be 0 (default) or >= 2\n");
            return -20;
        }
        if (training_gf3_ff_layers_per_batch == 1u) {
            printf("[Training] WARN: training.gf3_ff_layers_per_batch=1 is inefficient; use 0 (all layers) or >= 2\n");
        }
        if (training_auto_resume_from_checkpoint > 1u) {
            printf("[Training] WARN: training.auto_resume_from_checkpoint raw=%u (expected 0/1); nonzero treated as "
                   "enabled\n",
                   static_cast<unsigned>(training_auto_resume_from_checkpoint));
        }
        if (training_gf3_sycl_submit_grid_log > 1u) {
            printf("[Training] WARN: training.gf3_sycl_submit_grid_log raw=%u (expected 0/1); nonzero treated as "
                   "enabled\n",
                   static_cast<unsigned>(training_gf3_sycl_submit_grid_log));
        }
        // 0/1 only. Values like 8192 almost always mean qminiwasm.exe ↔ q_training.dll Training_InitSession ABI drift
        // (e.g. stale host after removing a parameter). Do not train with garbage scalars.
        if (training_gf3_ff_multislot_ignore_host_slot_budget > 1u) {
            printf(
                "[Training] ERROR: training.gf3_ff_multislot_ignore_host_slot_budget=%u (must be 0 or 1). "
                "This usually means qminiwasm.exe was not rebuilt against the same Training_InitSession signature as "
                "q_training.dll — rebuild both from the same commit (see also gf3_sycl_min_weight_cells sanity).\n",
                static_cast<unsigned>(training_gf3_ff_multislot_ignore_host_slot_budget));
            return -21;
        }
        constexpr std::uint64_t kAbsurdWeightCells = 1'000'000'000'000ull; // 1e12 — real configs are far smaller
        if (training_gf3_sycl_min_weight_cells > kAbsurdWeightCells) {
            printf(
                "[Training] ERROR: training.gf3_sycl_min_weight_cells=%llu is out of range (sanity cap=%llu). "
                "Typical cause: Go host ↔ DLL ABI mismatch — rebuild qminiwasm.exe from the same tree as "
                "q_training.dll.\n",
                static_cast<unsigned long long>(training_gf3_sycl_min_weight_cells),
                static_cast<unsigned long long>(kAbsurdWeightCells));
            return -21;
        }
        constexpr std::uint64_t kAbsurdMultislotMax = 1'000'000'000'000ull;
        if (training_gf3_ff_multislot_slots_chunk_max > kAbsurdMultislotMax) {
            printf(
                "[Training] ERROR: training.gf3_ff_multislot_slots_chunk_max=%llu out of range (cap=%llu). "
                "Rebuild qminiwasm.exe to match q_training.dll InitSession ABI.\n",
                static_cast<unsigned long long>(training_gf3_ff_multislot_slots_chunk_max),
                static_cast<unsigned long long>(kAbsurdMultislotMax));
            return -21;
        }
        if (training_gf3_ff_multislot_ignore_host_slot_budget != 0u &&
            training_gf3_ff_multislot_slots_chunk_max == 0ull) {
            printf("[Training] WARN: training.gf3_ff_multislot_ignore_host_slot_budget=1 with "
                   "training.gf3_ff_multislot_slots_chunk_max=0: chunks use 5/4× the MiB-derived slot cap; set "
                   "gf3_ff_multislot_slots_chunk_max or ff_multi_row_slots_chunk to hard-cap slots per SYCL wave.\n");
        }
#if defined(USE_SYCL) && USE_SYCL
        // SYCL training builds: throughput knobs are mandatory — no disabled multi-row FF,
        // no disabled parallel contrastive rows (validated here so misconfigured TOML fails fast).
        if (training_ff_multi_row_batch == 0u) {
            printf("[Training] ERROR: training.ff_multi_row_batch must be true (1) — SYCL GF3 FF uses batched "
                   "multi-row kernels only\n");
            return -22;
        }
        if (training_parallel_contrastive_rows == 0u) {
            printf("[Training] ERROR: training.parallel_contrastive_rows must be true (1) — SYCL builds require "
                   "parallel contrastive rows\n");
            return -22;
        }
        q_mini_wasm_v2::sycl_kernels::gf3_sycl_apply_runtime_host_config(
            training_sycl_gpu_device_index,
            training_gf3_sycl_min_weight_cells,
            training_gf3_sycl_submit_grid_log != 0u);
        q_mini_wasm_v2::core::moe::gf3_ff_set_layers_per_batch(training_gf3_ff_layers_per_batch);
#endif
        {
            const auto route_policy_mode =
                static_cast<q_mini_wasm_v2::core::moe::SyclRouteMode>(training_sycl_route_mode);
            const size_t route_experts = static_cast<size_t>(num_experts);
            if (q_mini_wasm_v2::core::moe::routing_sycl_failure_must_abort(route_policy_mode, route_experts)) {
                char sycl_gpu_err[640]{};
                if (!q_mini_wasm_v2::core::moe::gf3_sycl_gpu_queue_available(sycl_gpu_err, sizeof(sycl_gpu_err))) {
                    printf("[Training] ERROR: no usable SYCL GPU queue while TOML requires SYCL routing: %s\n",
                           sycl_gpu_err[0] ? sycl_gpu_err : "(no detail)");
                    return -18;
                }
            }
        }
#if defined(USE_SYCL) && USE_SYCL
        if (moe_input_dim > 0u) {
            const size_t probe_n = static_cast<size_t>(moe_input_dim);
            std::vector<int8_t> pos_lanes(probe_n, static_cast<int8_t>(1));
            std::vector<uint8_t> pos_packed;
            q::ternary::pack_batch_t5(pos_lanes, pos_packed);
            std::vector<uint8_t> neg_out;
            if (!q_mini_wasm_v2::training_dll::gf3_training_negative_probe_sycl(pos_packed, probe_n, 42u, neg_out) ||
                neg_out.size() != pos_packed.size()) {
                printf("[Training] ERROR: SYCL contrastive-negative probe failed (GPU-mandatory; fix SYCL/GPU)\n");
                return -19;
            }
        }
#endif
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

        const uint32_t effective_top_k = std::min(std::max(1u, top_k), std::max(1u, num_experts));
        const uint32_t effective_micro_batch_cap = (training_micro_batch_cap == 0u) ? std::max(1u, batch_size) : training_micro_batch_cap;
        const uint32_t effective_collect_limit =
            std::max(1u, std::min(std::max(1u, effective_micro_batch_cap), std::max(1u, batch_size)));
        const uint32_t bounded_collect_limit = effective_collect_limit;
        uint64_t derived_routes_u64 =
            static_cast<uint64_t>(bounded_collect_limit) * static_cast<uint64_t>(effective_top_k);
        if (derived_routes_u64 == 0ull) {
            derived_routes_u64 = 1ull;
        }
        const size_t effective_target_routes_per_batch =
            static_cast<size_t>((target_routes_per_batch == 0u) ? derived_routes_u64 : static_cast<uint64_t>(target_routes_per_batch));
        const uint32_t effective_collect_min_rows =
            static_cast<uint32_t>(std::max<uint64_t>(
                1ull,
                std::min<uint64_t>(
                    static_cast<uint64_t>(bounded_collect_limit),
                    (collect_min_rows_per_batch == 0u)
                        ? static_cast<uint64_t>(bounded_collect_limit)
                        : static_cast<uint64_t>(collect_min_rows_per_batch))));
        const size_t effective_ff_active_internal_layers =
            static_cast<size_t>(std::max<uint32_t>(
                1u,
                std::min<uint32_t>(
                    moe_expert_internal_layers,
                    (moe_ff_active_internal_layers == 0u) ? moe_expert_internal_layers : moe_ff_active_internal_layers)));

        PipelineConfig cfg = default_pipeline_config();
        cfg.moe_num_experts = num_experts;
        cfg.moe_top_k = effective_top_k;
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
        cfg.moe_ff_active_internal_layers = effective_ff_active_internal_layers;
        cfg.ff_learning_rate_step = std::max(1u, static_cast<uint32_t>(std::llround(std::max(1.0, learning_rate))));
        
        cfg.acquisition_threads = static_cast<size_t>(acquisition_threads);
        cfg.perturbation_threads = static_cast<size_t>(perturbation_threads);
        cfg.checkpoint_async_queue_max = static_cast<size_t>(checkpoint_async_queue_max);
        cfg.collect_empty_backoff_base_ms = collect_empty_backoff_base_ms;
        cfg.collect_empty_backoff_max_shift = collect_empty_backoff_max_shift;
        cfg.collect_empty_backoff_cap_ms = collect_empty_backoff_cap_ms;
        cfg.metrics_heartbeat_sec = metrics_heartbeat_sec;
        cfg.training_parallel_contrastive_rows = (training_parallel_contrastive_rows != 0u);
        cfg.training_parallel_batches = static_cast<size_t>(training_parallel_batches);
        cfg.training_parallel_batches_runtime_cap = static_cast<size_t>(training_parallel_batches_runtime_cap);
        cfg.ff_expert_chunk_size = static_cast<size_t>(ff_expert_chunk_size);
        cfg.target_routes_per_batch = effective_target_routes_per_batch;
        cfg.collect_window_ms = collect_window_ms;
        cfg.collect_min_rows_per_batch = effective_collect_min_rows;
        cfg.training_checkpoint_data_dir =
            (training_checkpoint_data_dir_utf8 && training_checkpoint_data_dir_utf8[0] != '\0')
                ? std::string(training_checkpoint_data_dir_utf8)
                : std::string{};
        cfg.sycl_prereserve_gib = training_sycl_prereserve_gib;
        cfg.sycl_prereserve_chunk_mib =
            (training_sycl_prereserve_chunk_mib == 0u)
                ? q_mini_wasm_v2::core::training::default_pipeline_config().sycl_prereserve_chunk_mib
                : training_sycl_prereserve_chunk_mib;
        cfg.sycl_gpu_device_index = training_sycl_gpu_device_index;
        cfg.gf3_sycl_min_weight_cells = static_cast<size_t>(std::min<uint64_t>(
            training_gf3_sycl_min_weight_cells,
            static_cast<uint64_t>(std::numeric_limits<size_t>::max())));
        cfg.gf3_sycl_submit_grid_log = (training_gf3_sycl_submit_grid_log != 0u);
        cfg.gf3_ff_batched_weight_mib = static_cast<size_t>(std::min<uint64_t>(
            training_gf3_ff_batched_weight_mib,
            static_cast<uint64_t>(std::numeric_limits<size_t>::max())));
        cfg.ff_multi_row_batch = (training_ff_multi_row_batch != 0u);
        cfg.ff_multi_row_slots_chunk = training_ff_multi_row_slots_chunk;
        cfg.gf3_ff_layers_per_batch = training_gf3_ff_layers_per_batch;
        cfg.gf3_ff_multislot_slots_chunk_max = static_cast<size_t>(std::min<uint64_t>(
            training_gf3_ff_multislot_slots_chunk_max,
            static_cast<uint64_t>(std::numeric_limits<size_t>::max())));
        cfg.gf3_ff_multislot_ignore_host_slot_budget = (training_gf3_ff_multislot_ignore_host_slot_budget != 0u);
        cfg.lazy_moe_resident_cap = static_cast<size_t>(std::min<uint64_t>(
            training_lazy_moe_resident_cap,
            static_cast<uint64_t>(std::numeric_limits<size_t>::max())));
        cfg.training_auto_resume_from_checkpoint = (training_auto_resume_from_checkpoint != 0u);
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
        printf("[Training]   parallel_contrastive_rows=%d parallel_batches=%zu parallel_batches_runtime_cap=%zu "
               "ff_expert_chunk_size=%zu target_routes_per_batch=%zu "
               "collect_window_ms=%u collect_min_rows_per_batch=%u auto_resume_checkpoint=%d "
               "checkpoint_data_dir=%s\n",
               cfg.training_parallel_contrastive_rows ? 1 : 0,
               cfg.training_parallel_batches,
               cfg.training_parallel_batches_runtime_cap,
               cfg.ff_expert_chunk_size,
               cfg.target_routes_per_batch,
               static_cast<unsigned>(cfg.collect_window_ms),
               static_cast<unsigned>(cfg.collect_min_rows_per_batch),
               cfg.training_auto_resume_from_checkpoint ? 1 : 0,
               cfg.training_checkpoint_data_dir.empty()
                   ? "(unset: ./checkpoints under cwd — set training.checkpoint_data_dir, e.g. C:/q_mini_data)"
                   : cfg.training_checkpoint_data_dir.c_str());
        printf("[Training]   gf3_sycl_min_weight_cells=%zu gf3_sycl_submit_grid_log=%d (TOML training.gf3_sycl_*)\n",
               cfg.gf3_sycl_min_weight_cells,
               cfg.gf3_sycl_submit_grid_log ? 1 : 0);
        printf("[Training]   gf3_ff_multislot_slots_chunk_max=%zu ignore_host_slot_budget=%d (TOML training.gf3_ff_multislot_*)\n",
               cfg.gf3_ff_multislot_slots_chunk_max,
               cfg.gf3_ff_multislot_ignore_host_slot_budget ? 1 : 0);
        printf("[Training]   ff_multi_row_batch=%d ff_multi_row_slots_chunk=%u gf3_ff_batched_weight_mib=%zu "
               "(SYCL throughput: ff_multi_row_batch=0 forces serial contrastive rows)\n",
               cfg.ff_multi_row_batch ? 1 : 0,
               static_cast<unsigned>(cfg.ff_multi_row_slots_chunk),
               cfg.gf3_ff_batched_weight_mib);
        printf("[Training]   gf3_ff_layers_per_batch=%u (0=all layers, >0=layer batching for deep networks)\n",
               static_cast<unsigned>(cfg.gf3_ff_layers_per_batch));

        cfg.samples_per_epoch = static_cast<size_t>(samples_per_epoch_or_zero);
        cfg.training_micro_batch_cap = static_cast<size_t>(effective_micro_batch_cap);
        cfg.training_collect_floor = static_cast<size_t>(training_collect_floor);
        cfg.training_timing_to_stderr = training_timing_to_stderr;
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
        printf("[Training]   micro_batch_cap=%zu collect_floor=%zu timing_to_stderr=%d sycl_route_mode=%u sycl_trit_quant_min_moe_dim=%zu goodness_log_level=%u\n",
               cfg.training_micro_batch_cap,
               cfg.training_collect_floor,
               training_timing_to_stderr ? 1 : 0,
               static_cast<unsigned>(training_sycl_route_mode),
               cfg.sycl_trit_quant_min_moe_dim,
               static_cast<unsigned>(cfg.goodness_log_level));
        printf("[Training]   effective_top_k=%zu collect_limit=%u target_routes_per_batch=%zu collect_min_rows_per_batch=%u ff_active_internal_layers=%zu\n",
               cfg.moe_top_k,
               static_cast<unsigned>(bounded_collect_limit),
               cfg.target_routes_per_batch,
               static_cast<unsigned>(cfg.collect_min_rows_per_batch),
               cfg.moe_ff_active_internal_layers);
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

        qmini_training_breadcrumb("dll:InitSession:staged");
        return 0;
    } catch (const std::exception& e) {
        printf("[Training] ERROR Training_InitSession: %s\n", e.what());
        return -1;
    } catch (...) {
        printf("[Training] ERROR Training_InitSession: non-std exception (native fault)\n");
        return kTrainingApiNativeFault;
    }
}

TRAINING_API int Training_StartTraining(
    uint64_t session_id,
    uint32_t epochs,
    const char* data_path,
    bool enable_data_accumulation
) {
    try {
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
        qmini_training_breadcrumb("dll:StartTraining:initialize");
        if (!session->pipeline->initialize(config)) {
            printf("[Training] ERROR: Pipeline initialization failed\n");
            return -3;
        }
        session->pipeline_materialized = true;
    } else {
        qmini_training_breadcrumb("dll:StartTraining:apply_overrides");
        session->pipeline->apply_run_overrides(session->data_path, epochs);
    }

    qmini_training_breadcrumb("dll:StartTraining:start_training");
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
    } catch (const std::exception& e) {
        printf("[Training] ERROR Training_StartTraining: %s\n", e.what());
        return kTrainingApiNativeFault;
    } catch (...) {
        printf("[Training] ERROR Training_StartTraining: non-std exception\n");
        return kTrainingApiNativeFault;
    }
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
    try {
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
                   state == PipelineState::CHECKPOINTING ||
                   state == PipelineState::EVALUATING_TOPOLOGY ||
                   state == PipelineState::OPTIMIZING_GRAPH ||
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
    } catch (const std::exception& e) {
        printf("[Training] ERROR Training_GetProgress: %s\n", e.what());
        return kTrainingApiNativeFault;
    } catch (...) {
        printf("[Training] ERROR Training_GetProgress: non-std exception\n");
        return kTrainingApiNativeFault;
    }
}

TRAINING_API int Training_StopTraining(uint64_t session_id) {
    try {
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
    } catch (const std::exception& e) {
        printf("[Training] ERROR Training_StopTraining: %s\n", e.what());
        return kTrainingApiNativeFault;
    } catch (...) {
        return kTrainingApiNativeFault;
    }
}

TRAINING_API int Training_CleanupSession(uint64_t session_id) {
    try {
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
        if (g_sessions.empty()) {
            std::string note;
            const size_t after = q_mini_wasm_v2::sycl_kernels::reserve_sycl_device_memory_bytes(
                0, 1, false, &note);
            (void)after;
            printf("[Training] Last session removed: SYCL USM pre-reserve released (%s)\n", note.c_str());
        }
        printf("[Training] Session %llu: Cleaned up\n", static_cast<unsigned long long>(session_id));
        return 0;
    }
    
    return -1;
    } catch (const std::exception& e) {
        printf("[Training] ERROR Training_CleanupSession: %s\n", e.what());
        return kTrainingApiNativeFault;
    } catch (...) {
        return kTrainingApiNativeFault;
    }
}

TRAINING_API int Training_ExportCheckpoint(uint64_t session_id, const char* path_utf8) {
    try {
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
    } catch (const std::exception& e) {
        printf("[Training] ERROR Training_ExportCheckpoint: %s\n", e.what());
        return kTrainingApiNativeFault;
    } catch (...) {
        return kTrainingApiNativeFault;
    }
}

TRAINING_API int Training_ImportCheckpoint(uint64_t session_id, const char* path_utf8) {
    try {
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
    // Merge InitSession / live TOML throughput knobs over checkpoint blob (same as auto-resume).
    // Without this, stale ff_multi_row_slots_chunk / gf3_ff_* from the file masks rebuilt DLL behavior.
    if (!session->pipeline->import_model(path_utf8, &session->stored_config)) {
        session->pipeline_materialized = false;
        return -3;
    }
    session->stored_config = session->pipeline->get_config();
    session->pipeline_materialized = true;
    return 0;
    } catch (const std::exception& e) {
        printf("[Training] ERROR Training_ImportCheckpoint: %s\n", e.what());
        return kTrainingApiNativeFault;
    } catch (...) {
        return kTrainingApiNativeFault;
    }
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
    try {
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
    } catch (const std::exception& e) {
        printf("[Training] ERROR Training_GetMetrics: %s\n", e.what());
        return kTrainingApiNativeFault;
    } catch (...) {
        return kTrainingApiNativeFault;
    }
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
    try {
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
    } catch (const std::exception& e) {
        printf("[Training] ERROR Training_GetIngestionStats: %s\n", e.what());
        return kTrainingApiNativeFault;
    } catch (...) {
        return kTrainingApiNativeFault;
    }
}

/** SYCL device VRAM / kind probe for Go autoscale (before Training_InitSession). Returns 0 on success. */
TRAINING_API int Training_SetPendingLiveParallelStart(uint64_t session_id, uint32_t parallel_batches_start) {
    try {
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
        session->pipeline->set_pending_live_parallel_start(static_cast<size_t>(parallel_batches_start));
        return 0;
    } catch (...) {
        return kTrainingApiNativeFault;
    }
}

TRAINING_API int Training_SetLiveParallelBatches(uint64_t session_id, uint32_t parallel_batches) {
    try {
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
        session->pipeline->set_live_parallel_batches(static_cast<size_t>(parallel_batches));
        return 0;
    } catch (...) {
        return kTrainingApiNativeFault;
    }
}

TRAINING_API int Training_GetLiveParallelBatches(
    uint64_t session_id, uint32_t* live_out, uint32_t* ceiling_out) {
    if (!live_out || !ceiling_out) {
        return -1;
    }
    *live_out = 0;
    *ceiling_out = 0;
    try {
        std::lock_guard<std::mutex> lock(g_sessions_mutex);
        TrainingSession* session = nullptr;
        for (auto& s : g_sessions) {
            if (s->id == session_id) {
                session = s.get();
                break;
            }
        }
        if (!session || !session->pipeline) {
            return -2;
        }
        const size_t lv = session->pipeline->live_parallel_batches_value();
        const size_t ce = session->pipeline->parallel_batches_ceiling_value();
        *live_out = static_cast<uint32_t>(std::min<size_t>(lv, static_cast<size_t>(std::numeric_limits<uint32_t>::max())));
        *ceiling_out = static_cast<uint32_t>(std::min<size_t>(ce, static_cast<size_t>(std::numeric_limits<uint32_t>::max())));
        return 0;
    } catch (...) {
        return kTrainingApiNativeFault;
    }
}

TRAINING_API int Training_ProbeSyclDevice(
    int32_t gpu_device_index,
    uint64_t* global_mem_bytes_out,
    uint32_t* is_gpu_u32_out,
    char* name_utf8_out,
    size_t name_cap,
    char* err_utf8_out,
    size_t err_cap) {
    if (!global_mem_bytes_out || !is_gpu_u32_out) {
        return -1;
    }
    *global_mem_bytes_out = 0;
    *is_gpu_u32_out = 0;
    if (name_utf8_out && name_cap > 0) {
        name_utf8_out[0] = '\0';
    }
    if (err_utf8_out && err_cap > 0) {
        err_utf8_out[0] = '\0';
    }
    q_mini_wasm_v2::dll::common::qmini_dll_touch_sycl_device_once();
    return q_mini_wasm_v2::sycl_kernels::gf3_sycl_probe_device_resources(
        gpu_device_index,
        global_mem_bytes_out,
        is_gpu_u32_out,
        name_utf8_out,
        name_cap,
        err_utf8_out,
        err_cap);
}

} // extern "C"
