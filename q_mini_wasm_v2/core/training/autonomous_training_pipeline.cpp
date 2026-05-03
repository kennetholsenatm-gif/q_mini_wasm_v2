#include "autonomous_training_pipeline.hpp"
#include "crash_breadcrumb.hpp"
#include "../gf3/gf3_types.hpp"
#include "../moe/gf3_layers.hpp"
#include "../moe/unified_config.hpp"
#include "../moe/runtime_orchestrator.hpp"
#include "../moe/gf3_sycl_probe.hpp"
#include "../../sycl/gf3_layers_sycl.hpp"
#ifdef _OPENMP
#include <omp.h>
#endif
#include "../ternary/trit.hpp"
#include "../ternary/packing.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <numeric>
#include <variant>
#include <unordered_map>
#include <chrono>
#include <optional>
#include <future>
#include <random>
#include <thread>
#include <cmath>
#include <functional>
#include <cstdio>
#include <cstring>
#include <span>
#include <mutex>
#include <memory>
#include <atomic>
#include <limits>
#include <filesystem>
#if defined(USE_SYCL) && USE_SYCL
#include "../../sycl/tableau_kernels.hpp"
#include <sycl/sycl.hpp>
#endif
#if defined(_MSC_VER)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace q_mini_wasm_v2::core::training {

// default_pipeline_config() is generated from config/pipeline_defaults.toml into
// autonomous_training_pipeline_defaults.gen.cpp (see scripts/gen_pipeline_default_config.py).

#if defined(USE_SYCL) && USE_SYCL
static_assert(USE_SYCL == 1, "GPU training builds must define USE_SYCL=1 (see CMakeLists.txt).");
#endif

namespace {

#if defined(USE_SYCL) && USE_SYCL
/** UR reports numeric backend results; ABI strings like OUT_OF_RESOURCES are symbol names, not a VRAM gauge. */
std::string augment_ur_sycl_exception_text_for_log(const std::exception& ex) {
    std::string w = ex.what();
    (void)dynamic_cast<const sycl::exception*>(&ex);
    if (w.find("OUT_OF_RESOURCES") != std::string::npos || w.find("returns:40") != std::string::npos) {
        w += " | UR numeric/symbol is backend ABI text—not synonymous with GPU RAM exhaustion.";
    }
    return w;
}
#else
std::string augment_ur_sycl_exception_text_for_log(const std::exception& ex) {
    return std::string(ex.what());
}
#endif

constexpr char kFileMagicV3[8] = {'Q', 'M', 'I', 'N', 'I', '_', 'V', '3'};
constexpr const char* kSpillFilePrefix = "spill://";

bool rd_u32(std::istream& is, uint32_t& v);
bool read_pipeline_config(std::istream& is, PipelineConfig& c, uint32_t checkpoint_fmt);
std::string checkpoint_output_dir(const PipelineConfig& c);
std::optional<std::string> latest_checkpoint_path(const PipelineConfig& c);
std::string expert_spill_dir(const PipelineConfig& c);

bool spill_marker_to_path(const std::string& marker, std::string& out_path) {
    const std::string prefix(kSpillFilePrefix);
    if (marker.rfind(prefix, 0) != 0) {
        return false;
    }
    out_path = marker.substr(prefix.size());
    return !out_path.empty();
}

std::string expert_spill_dir(const PipelineConfig& c) {
    namespace fs = std::filesystem;
    return (fs::path(checkpoint_output_dir(c)) / "expert_spills").string();
}

std::string spill_marker_for_path(const std::string& path) {
    return std::string(kSpillFilePrefix) + path;
}

bool read_spill_payload(const std::string& stored, std::string& out_blob) {
    std::string spill_path;
    if (!spill_marker_to_path(stored, spill_path)) {
        out_blob = stored;
        return true;
    }
    std::ifstream f(spill_path, std::ios::binary);
    if (!f.is_open()) {
        return false;
    }
    std::ostringstream oss(std::ios::binary);
    oss << f.rdbuf();
    out_blob = std::move(oss).str();
    return static_cast<bool>(f) || f.eof();
}

void delete_spill_file_if_marker(const std::string& stored) {
    std::string spill_path;
    if (!spill_marker_to_path(stored, spill_path)) {
        return;
    }
    std::error_code ec;
    std::filesystem::remove(spill_path, ec);
}

std::string checkpoint_output_dir(const PipelineConfig& c) {
    namespace fs = std::filesystem;
    if (!c.training_checkpoint_data_dir.empty()) {
        fs::path p(c.training_checkpoint_data_dir);
        return (p / "checkpoints").string();
    }
    return fs::path("checkpoints").string();
}

std::optional<std::string> latest_checkpoint_path(const PipelineConfig& c) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path dir(checkpoint_output_dir(c));
    if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec)) {
        return std::nullopt;
    }
    fs::file_time_type best_time{};
    fs::path best_path;
    bool found = false;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (ec || !entry.is_regular_file()) {
            continue;
        }
        const auto& p = entry.path();
        if (p.extension() != ".qmini") {
            continue;
        }
        const std::string name = p.filename().string();
        if (name.rfind("checkpoint_epoch_", 0) != 0) {
            continue;
        }
        const auto t = entry.last_write_time(ec);
        if (ec) {
            continue;
        }
        if (!found || t > best_time) {
            best_time = t;
            best_path = p;
            found = true;
        }
    }
    if (!found) {
        return std::nullopt;
    }
    return best_path.string();
}

std::optional<std::string> checkpoint_incompatibility_reason(
    const std::string& checkpoint_path,
    const PipelineConfig& requested
) {
    std::ifstream file(checkpoint_path, std::ios::binary);
    if (!file.is_open()) {
        return std::string("open_failed");
    }
    char magic[8] = {};
    file.read(magic, 8);
    if (!file || std::memcmp(magic, kFileMagicV3, 8) != 0) {
        return std::string("bad_magic");
    }
    uint32_t fmt = 0;
    if (!rd_u32(file, fmt) ||
        (fmt != 3 && fmt != 4 && fmt != 5 && fmt != 6 && fmt != 7 && fmt != 8 && fmt != 9 && fmt != 10 &&
         fmt != 11 && fmt != 12 && fmt != 13 && fmt != 14 && fmt != 15 && fmt != 16)) {
        return std::string("bad_format");
    }
    PipelineConfig ck{};
    if (!read_pipeline_config(file, ck, fmt)) {
        return std::string("config_decode_failed");
    }
    // Structural/configuration mismatches can silently erase a user's "turn it up" settings.
    // Skip auto-resume when key shape/cost knobs differ from the current requested run config.
    if (ck.moe_num_experts != requested.moe_num_experts) return std::string("moe_num_experts");
    if (ck.moe_input_dim != requested.moe_input_dim) return std::string("moe_input_dim");
    if (ck.moe_output_dim != requested.moe_output_dim) return std::string("moe_output_dim");
    if (ck.moe_hidden_dim != requested.moe_hidden_dim) return std::string("moe_hidden_dim");
    if (ck.moe_expert_internal_layers != requested.moe_expert_internal_layers) return std::string("expert_internal_layers");
    if (ck.moe_ff_active_internal_layers != requested.moe_ff_active_internal_layers) {
        return std::string("moe_ff_active_internal_layers");
    }
    if (ck.moe_top_k != requested.moe_top_k) return std::string("moe_top_k");
    if (ck.training_collect_floor != requested.training_collect_floor) return std::string("collect_floor");
    if (ck.training_micro_batch_cap != requested.training_micro_batch_cap) return std::string("micro_batch_cap");
    return std::nullopt;
}

void goodness_log_banner_once(unsigned level) {
    if (level < 1u) {
        return;
    }
    static bool done = false;
    if (done) {
        return;
    }
    done = true;
    std::fprintf(stderr,
                 "[TrainingPipeline][Goodness] training.goodness_log_level=%u: MoE FF metrics count non-zero "
                 "{-1,+1} trits on each expert's **last GF3 layer output** after Forward(positive) vs "
                 "Forward(negative) (same values as inside TrainForwardForward). "
                 "Level 1 = batch averages; level 2 = first 3 contrastive rows per expert.\n",
                 level);
}

namespace {
/** First training_loop tick forces parallel_batches=1; keep this modest so `training.parallel_batches` concurrent
 *  waves begin quickly—full micro-batches here dominated wall time on integrated GPUs before ramp (see progress logs). */
} // namespace

/** Upper bound on contrastive rows collected per `process_batch`: min(batch_size, micro_batch_cap); 0 micro cap uses batch_size. */
size_t training_micro_batch_cap_for(const PipelineConfig& c) {
    return (c.training_micro_batch_cap == 0u) ? std::max<size_t>(size_t{1}, c.batch_size) : c.training_micro_batch_cap;
}

bool training_timing_enabled_for(const PipelineConfig& c) {
    return c.training_timing_to_stderr;
}


/** Process multiple contrastive rows concurrently (OpenMP); gated by training.parallel_contrastive_rows. */
#if defined(_OPENMP)
bool training_parallel_rows_enabled_for(const PipelineConfig& c) {
    return c.training_parallel_contrastive_rows;
}
#endif

/**
 * Split the per-row FF expert loop into chunks so the training thread can log and push metrics between chunks.
 * training.ff_expert_chunk_size: 0 or > nr means one chunk for the full route.
 */
size_t ff_expert_chunk_count(const PipelineConfig& c, int nr) {
    if (nr <= 1) {
        return static_cast<size_t>(std::max(1, nr));
    }
    size_t chunk = c.ff_expert_chunk_size;
    if (chunk == 0u || chunk > static_cast<size_t>(nr)) {
        return static_cast<size_t>(nr);
    }
    return chunk;
}

template <typename Fn>
void run_parallel_indices(size_t count, bool enabled, Fn&& fn) {
    if (!enabled || count < 2) {
        for (size_t i = 0; i < count; ++i) {
            fn(i);
        }
        return;
    }
    const unsigned hw = std::max(1u, std::thread::hardware_concurrency());
    const size_t workers = std::min<size_t>(count, static_cast<size_t>(hw));
    std::atomic<size_t> next{0};
    std::exception_ptr first_exception;
    std::mutex ex_mu;
    std::vector<std::thread> threads;
    threads.reserve(workers);
    for (size_t w = 0; w < workers; ++w) {
        threads.emplace_back([&]() {
            try {
                while (true) {
                    const size_t i = next.fetch_add(1, std::memory_order_relaxed);
                    if (i >= count) {
                        break;
                    }
                    fn(i);
                }
            } catch (...) {
                std::lock_guard<std::mutex> lk(ex_mu);
                if (!first_exception) {
                    first_exception = std::current_exception();
                }
            }
        });
    }
    for (auto& t : threads) {
        if (t.joinable()) {
            t.join();
        }
    }
    if (first_exception) {
        std::rethrow_exception(first_exception);
    }
}

inline int64_t elapsed_ms(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - t0)
        .count();
}

uint64_t router_work_estimate(const PipelineConfig& c) {
    return static_cast<uint64_t>(c.moe_num_experts) * static_cast<uint64_t>(c.routing_qutrits);
}

size_t adaptive_collect_limit(const PipelineConfig& c) {
    const size_t hard_cap = std::min(c.batch_size, training_micro_batch_cap_for(c));
    return std::max(size_t{1}, hard_cap);
}

size_t adaptive_route_topk_cap(const PipelineConfig& c) {
    if (c.moe_num_experts == 0) {
        return 1;
    }
    const size_t requested = std::max<size_t>(size_t{1}, c.moe_top_k);
    return std::min(requested, c.moe_num_experts);
}

size_t effective_target_routes_per_batch(const PipelineConfig& c) {
    if (c.target_routes_per_batch > 0u) {
        return c.target_routes_per_batch;
    }
    const size_t collect_limit = adaptive_collect_limit(c);
    const size_t topk = adaptive_route_topk_cap(c);
    const uint64_t derived = static_cast<uint64_t>(collect_limit) * static_cast<uint64_t>(topk);
    return static_cast<size_t>(std::max<uint64_t>(1ull, derived));
}

/** Build first-layer TritPack5 for FF: prefer @p route_in.route_packed_t5 (zero-pad / truncate), else pack trits with zero tail. */
inline void ff_route_inputs_to_pack5(
    const TernaryRouteInput& route_in,
    const std::vector<ternary::Trit>& trits_if_unpacked,
    size_t input_dim,
    std::vector<uint8_t>& out_pack5) {
    const size_t need_b = (input_dim + 4u) / 5u;
    out_pack5.assign(need_b, static_cast<uint8_t>(0));
    if (!route_in.route_packed_t5.empty()) {
        const size_t n = std::min(need_b, route_in.route_packed_t5.size());
        if (n > 0) {
            std::memcpy(out_pack5.data(), route_in.route_packed_t5.data(), n);
        }
        return;
    }
    std::vector<int8_t> lanes(input_dim, 0);
    for (size_t i = 0; i < trits_if_unpacked.size() && i < input_dim; ++i) {
        lanes[i] = static_cast<int8_t>(trits_if_unpacked[i]);
    }
#if defined(USE_SYCL) && USE_SYCL
    out_pack5 = ::q_mini_wasm_v2::sycl_kernels::pack_int8_lanes_to_tritpack5_sycl(lanes, input_dim);
#else
    q::ternary::pack_batch_t5(lanes, out_pack5);
#endif
}

#if defined(USE_SYCL) && USE_SYCL
/**
 * Use SYCL trit-quantization kernels for packed route input whenever MoE input width > 0.
 * @p min_moe_dim is retained for TOML/CGO ABI; SYCL is always selected here when USE_SYCL (no host quant path).
 */
inline bool pipeline_use_sycl_trit_quant(size_t moe_dim, size_t /*min_moe_dim*/) noexcept {
    return moe_dim > 0;
}

inline void collect_floats_rowmajor_upto(const std::vector<std::vector<float>>& arg, size_t max_n, std::vector<float>& out) {
    out.clear();
    for (const auto& row : arg) {
        for (float v : row) {
            out.push_back(v);
            if (out.size() >= max_n) {
                return;
            }
        }
    }
}

inline void collect_i32_rowmajor_upto(const std::vector<std::vector<int32_t>>& arg, size_t max_n, std::vector<int32_t>& out) {
    out.clear();
    for (const auto& row : arg) {
        for (int32_t v : row) {
            out.push_back(v);
            if (out.size() >= max_n) {
                return;
            }
        }
    }
}
#endif

void wr_u8(std::ostream& os, uint8_t v) {
    os.write(reinterpret_cast<const char*>(&v), 1);
}

void wr_u32(std::ostream& os, uint32_t v) {
    os.write(reinterpret_cast<const char*>(&v), 4);
}

void wr_u64(std::ostream& os, uint64_t v) {
    os.write(reinterpret_cast<const char*>(&v), 8);
}

void wr_f32(std::ostream& os, float v) {
    os.write(reinterpret_cast<const char*>(&v), 4);
}

void wr_sz(std::ostream& os, size_t v) {
    wr_u64(os, static_cast<uint64_t>(v));
}

void wr_str(std::ostream& os, const std::string& s) {
    wr_u64(os, static_cast<uint64_t>(s.size()));
    if (!s.empty()) {
        os.write(s.data(), static_cast<std::streamsize>(s.size()));
    }
}

bool rd_u8(std::istream& is, uint8_t& v) {
    is.read(reinterpret_cast<char*>(&v), 1);
    return static_cast<bool>(is);
}

bool rd_u32(std::istream& is, uint32_t& v) {
    is.read(reinterpret_cast<char*>(&v), 4);
    return static_cast<bool>(is);
}

bool rd_u64(std::istream& is, uint64_t& v) {
    is.read(reinterpret_cast<char*>(&v), 8);
    return static_cast<bool>(is);
}

bool rd_f32(std::istream& is, float& v) {
    is.read(reinterpret_cast<char*>(&v), 4);
    return static_cast<bool>(is);
}

bool rd_sz(std::istream& is, size_t& out) {
    uint64_t v = 0;
    if (!rd_u64(is, v)) {
        return false;
    }
    out = static_cast<size_t>(v);
    return true;
}

bool rd_str(std::istream& is, std::string& s) {
    uint64_t n = 0;
    if (!rd_u64(is, n)) {
        return false;
    }
    if (n > static_cast<uint64_t>(s.max_size())) {
        return false;
    }
    s.resize(static_cast<size_t>(n));
    if (n > 0) {
        is.read(s.data(), static_cast<std::streamsize>(n));
    }
    return static_cast<bool>(is);
}

/** After loading checkpoint `dst`, restore live TOML / InitSession throughput policy from `src`. */
static void merge_session_runtime_throughput(PipelineConfig& dst, const PipelineConfig& src) {
    dst.training_micro_batch_cap = src.training_micro_batch_cap;
    dst.training_collect_floor = src.training_collect_floor;
    dst.training_parallel_contrastive_rows = src.training_parallel_contrastive_rows;
    dst.training_parallel_batches = src.training_parallel_batches;
    dst.training_parallel_batches_runtime_cap = src.training_parallel_batches_runtime_cap;
    dst.ff_expert_chunk_size = src.ff_expert_chunk_size;
    dst.target_routes_per_batch = src.target_routes_per_batch;
    dst.collect_window_ms = src.collect_window_ms;
    dst.collect_min_rows_per_batch = src.collect_min_rows_per_batch;
    dst.training_checkpoint_data_dir = src.training_checkpoint_data_dir;
    dst.sycl_prereserve_gib = src.sycl_prereserve_gib;
    dst.sycl_prereserve_chunk_mib = src.sycl_prereserve_chunk_mib;
    dst.sycl_gpu_device_index = src.sycl_gpu_device_index;
    dst.gf3_sycl_min_weight_cells = src.gf3_sycl_min_weight_cells;
    dst.gf3_sycl_submit_grid_log = src.gf3_sycl_submit_grid_log;
    dst.gf3_ff_batched_weight_mib = src.gf3_ff_batched_weight_mib;
    dst.ff_multi_row_batch = src.ff_multi_row_batch;
    dst.ff_multi_row_slots_chunk = src.ff_multi_row_slots_chunk;
    dst.gf3_ff_multislot_slots_chunk_max = src.gf3_ff_multislot_slots_chunk_max;
    dst.gf3_ff_multislot_ignore_host_slot_budget = src.gf3_ff_multislot_ignore_host_slot_budget;
    dst.lazy_moe_resident_cap = src.lazy_moe_resident_cap;
    dst.prefill_progress_log_interval_sec = src.prefill_progress_log_interval_sec;
    dst.prefill_stall_warn_sec = src.prefill_stall_warn_sec;
    dst.collect_first_sample_timeout_sec = src.collect_first_sample_timeout_sec;
    dst.collect_ff_coalesce_sleep_us = src.collect_ff_coalesce_sleep_us;
    dst.training_pause_poll_ms = src.training_pause_poll_ms;
    dst.training_idle_retry_ms = src.training_idle_retry_ms;
    dst.training_empty_batch_log_interval = src.training_empty_batch_log_interval;
    dst.training_empty_batch_metrics_interval = src.training_empty_batch_metrics_interval;
    dst.train_queue_resync_discard_slack = src.train_queue_resync_discard_slack;
    dst.training_auto_resume_from_checkpoint = src.training_auto_resume_from_checkpoint;
    dst.acquisition_threads = src.acquisition_threads;
    dst.perturbation_threads = src.perturbation_threads;
    dst.max_acquisition_queue_depth = src.max_acquisition_queue_depth;
    dst.max_raw_queue_depth = src.max_raw_queue_depth;
    dst.max_train_queue_depth = src.max_train_queue_depth;
    dst.prefill_poll_ms = src.prefill_poll_ms;
    dst.prefill_timeout_ms = src.prefill_timeout_ms;
    dst.prefill_target_samples = src.prefill_target_samples;
}

void write_pipeline_config(std::ostream& os, const PipelineConfig& c) {
    wr_sz(os, c.acquisition_threads);
    wr_sz(os, c.perturbation_threads);
    wr_sz(os, c.ff_num_layers);
    wr_sz(os, c.ff_layer_width);
    wr_u32(os, c.ff_learning_rate_step);
    wr_f32(os, c.learning_rate);
    wr_sz(os, c.moe_num_experts);
    wr_sz(os, c.moe_top_k);
    wr_sz(os, c.moe_input_dim);
    wr_sz(os, c.moe_output_dim);
    wr_sz(os, c.moe_hidden_dim);
    wr_sz(os, c.moe_expert_internal_layers);
    wr_sz(os, c.routing_qutrits);
    wr_sz(os, c.graph_initial_nodes);
    wr_sz(os, c.graph_initial_edges);
    wr_sz(os, c.betti_max_qutrits);
    wr_u32(os, c.betti_guidance_threshold);
    wr_u32(os, c.shadow_dim);
    wr_sz(os, c.batch_size);
    wr_sz(os, c.num_epochs);
    wr_sz(os, c.samples_per_epoch);
    wr_sz(os, c.topology_evaluation_interval);
    wr_sz(os, c.checkpoint_interval);
    wr_u8(os, c.enable_prefill_ring_buffer ? uint8_t{1} : uint8_t{0});
    wr_sz(os, c.prefill_target_samples);
    wr_u32(os, c.prefill_timeout_ms);
    wr_u32(os, c.prefill_poll_ms);
    wr_sz(os, c.max_acquisition_queue_depth);
    wr_sz(os, c.max_raw_queue_depth);
    wr_sz(os, c.max_train_queue_depth);
    wr_u8(os, c.enable_betti_guidance ? uint8_t{1} : uint8_t{0});
    wr_u8(os, c.enable_knowledge_engine ? uint8_t{1} : uint8_t{0});
    wr_u8(os, c.enable_checkpoints ? uint8_t{1} : uint8_t{0});
    wr_u8(os, c.enable_wui_streaming ? uint8_t{1} : uint8_t{0});
    wr_u8(os, c.enable_continuous_mode ? uint8_t{1} : uint8_t{0});
    wr_u8(os, c.enable_steane_correction ? uint8_t{1} : uint8_t{0});
    wr_u8(os, c.enable_error_correction ? uint8_t{1} : uint8_t{0});
    wr_u8(os, c.enable_flash_cim ? uint8_t{1} : uint8_t{0});
    wr_u8(os, c.lazy_moe_experts ? uint8_t{1} : uint8_t{0});
    wr_sz(os, c.moe_ff_active_internal_layers);
    wr_str(os, c.data_path);
    wr_u8(os, c.prefer_local_data ? uint8_t{1} : uint8_t{0});
    wr_str(os, c.data_sources_toml_path);
    wr_sz(os, c.training_micro_batch_cap);
    wr_sz(os, c.training_collect_floor);
    wr_u8(os, c.training_timing_to_stderr ? uint8_t{1} : uint8_t{0});
    // Reserved: legacy pipeline blob field (former per-route expert serial mode; always 0 / ignored on read).
    wr_u8(os, uint8_t{0});
    wr_u8(os, static_cast<uint8_t>(c.training_sycl_route_mode));
    wr_sz(os, c.sycl_trit_quant_min_moe_dim);
    wr_u8(os, static_cast<uint8_t>(std::min<uint32_t>(2u, c.goodness_log_level)));
    wr_u8(os, c.allow_generated_negatives ? uint8_t{1} : uint8_t{0});
    wr_sz(os, c.directory_max_lines);
    wr_sz(os, c.max_jsonl_local_samples);
    wr_sz(os, c.max_text_length);
    wr_sz(os, c.min_text_length);
    wr_sz(os, c.checkpoint_async_queue_max);
    wr_u32(os, c.collect_empty_backoff_base_ms);
    wr_u32(os, c.collect_empty_backoff_max_shift);
    wr_u32(os, c.collect_empty_backoff_cap_ms);
    wr_u32(os, c.metrics_heartbeat_sec);
    wr_u8(os, c.training_parallel_contrastive_rows ? uint8_t{1} : uint8_t{0});
    wr_sz(os, c.training_parallel_batches);
    wr_sz(os, c.ff_expert_chunk_size);
    wr_sz(os, c.target_routes_per_batch);
    wr_u32(os, c.collect_window_ms);
    wr_u32(os, c.collect_min_rows_per_batch);
    wr_str(os, c.training_checkpoint_data_dir);
    wr_u32(os, c.sycl_prereserve_gib);
    wr_u32(os, static_cast<uint32_t>(c.sycl_gpu_device_index));
    wr_sz(os, c.gf3_sycl_min_weight_cells);
    wr_u8(os, c.ff_multi_row_batch ? uint8_t{1} : uint8_t{0});
    wr_u32(os, c.ff_multi_row_slots_chunk);
    wr_sz(os, c.lazy_moe_resident_cap);
    wr_sz(os, c.gf3_ff_batched_weight_mib);
    wr_u32(os, c.prefill_progress_log_interval_sec);
    wr_u32(os, c.prefill_stall_warn_sec);
    wr_u32(os, c.collect_first_sample_timeout_sec);
    wr_u32(os, c.collect_ff_coalesce_sleep_us);
    wr_u32(os, c.training_pause_poll_ms);
    wr_u32(os, c.training_idle_retry_ms);
    wr_sz(os, c.training_empty_batch_log_interval);
    wr_sz(os, c.training_empty_batch_metrics_interval);
    wr_sz(os, c.train_queue_resync_discard_slack);
    wr_u32(os, c.sycl_prereserve_chunk_mib);
    wr_u8(os, c.gf3_sycl_submit_grid_log ? uint8_t{1} : uint8_t{0});
    wr_sz(os, c.gf3_ff_multislot_slots_chunk_max);
    wr_u8(os, c.gf3_ff_multislot_ignore_host_slot_budget ? uint8_t{1} : uint8_t{0});
}

bool read_pipeline_config(std::istream& is, PipelineConfig& c, uint32_t checkpoint_fmt) {
    c.sycl_prereserve_gib = 0;
    c.sycl_gpu_device_index = -1;
    c.gf3_sycl_min_weight_cells = 0;
    c.ff_multi_row_batch = true;
    c.ff_multi_row_slots_chunk = 0;
    c.lazy_moe_resident_cap = 0;
    c.gf3_ff_batched_weight_mib = 0;
    c.training_parallel_batches = 1;
    c.training_auto_resume_from_checkpoint = true;
    c.gf3_sycl_submit_grid_log = false;
    c.gf3_ff_multislot_slots_chunk_max = 0;
    c.gf3_ff_multislot_ignore_host_slot_budget = false;

    if (!rd_sz(is, c.acquisition_threads)) {
        return false;
    }
    if (!rd_sz(is, c.perturbation_threads)) {
        return false;
    }
    if (!rd_sz(is, c.ff_num_layers)) {
        return false;
    }
    if (!rd_sz(is, c.ff_layer_width)) {
        return false;
    }
    if (!rd_u32(is, c.ff_learning_rate_step)) {
        return false;
    }
    if (!rd_f32(is, c.learning_rate)) {
        return false;
    }
    if (!rd_sz(is, c.moe_num_experts)) {
        return false;
    }
    if (!rd_sz(is, c.moe_top_k)) {
        return false;
    }
    if (!rd_sz(is, c.moe_input_dim)) {
        return false;
    }
    if (!rd_sz(is, c.moe_output_dim)) {
        return false;
    }
    if (!rd_sz(is, c.moe_hidden_dim)) {
        return false;
    }
    if (!rd_sz(is, c.moe_expert_internal_layers)) {
        return false;
    }
    if (!rd_sz(is, c.routing_qutrits)) {
        return false;
    }
    if (!rd_sz(is, c.graph_initial_nodes)) {
        return false;
    }
    if (!rd_sz(is, c.graph_initial_edges)) {
        return false;
    }
    if (!rd_sz(is, c.betti_max_qutrits)) {
        return false;
    }
    if (!rd_u32(is, c.betti_guidance_threshold)) {
        return false;
    }
    if (!rd_u32(is, c.shadow_dim)) {
        return false;
    }
    if (!rd_sz(is, c.batch_size)) {
        return false;
    }
    if (!rd_sz(is, c.num_epochs)) {
        return false;
    }
    if (!rd_sz(is, c.samples_per_epoch)) {
        return false;
    }
    if (!rd_sz(is, c.topology_evaluation_interval)) {
        return false;
    }
    if (!rd_sz(is, c.checkpoint_interval)) {
        return false;
    }
    uint8_t b = 0;
    if (!rd_u8(is, b)) {
        return false;
    }
    c.enable_prefill_ring_buffer = (b != 0);
    if (!rd_sz(is, c.prefill_target_samples)) {
        return false;
    }
    if (!rd_u32(is, c.prefill_timeout_ms)) {
        return false;
    }
    if (!rd_u32(is, c.prefill_poll_ms)) {
        return false;
    }
    if (!rd_sz(is, c.max_acquisition_queue_depth)) {
        return false;
    }
    if (!rd_sz(is, c.max_raw_queue_depth)) {
        return false;
    }
    if (!rd_sz(is, c.max_train_queue_depth)) {
        return false;
    }
    if (!rd_u8(is, b)) {
        return false;
    }
    c.enable_betti_guidance = (b != 0);
    if (!rd_u8(is, b)) {
        return false;
    }
    c.enable_knowledge_engine = (b != 0);
    if (!rd_u8(is, b)) {
        return false;
    }
    c.enable_checkpoints = (b != 0);
    if (!rd_u8(is, b)) {
        return false;
    }
    c.enable_wui_streaming = (b != 0);
    if (!rd_u8(is, b)) {
        return false;
    }
    c.enable_continuous_mode = (b != 0);
    if (!rd_u8(is, b)) {
        return false;
    }
    c.enable_steane_correction = (b != 0);
    if (!rd_u8(is, b)) {
        return false;
    }
    c.enable_error_correction = (b != 0);
    if (!rd_u8(is, b)) {
        return false;
    }
    c.enable_flash_cim = (b != 0);
    if (!rd_u8(is, b)) {
        return false;
    }
    c.lazy_moe_experts = (b != 0);
    if (!rd_sz(is, c.moe_ff_active_internal_layers)) {
        return false;
    }
    if (!rd_str(is, c.data_path)) {
        return false;
    }
    if (!rd_u8(is, b)) {
        return false;
    }
    c.prefer_local_data = (b != 0);
    if (!rd_str(is, c.data_sources_toml_path)) {
        return false;
    }
    c.training_micro_batch_cap = 0;
    c.training_collect_floor = 0;
    c.training_timing_to_stderr = false;
    c.training_sycl_route_mode = moe::SyclRouteMode::Auto;
    c.goodness_log_level = 0;
    c.allow_generated_negatives = true;
    if (!is.good()) {
        return true;
    }
    const int peeked = is.peek();
    if (peeked == std::istream::traits_type::eof()) {
        return true;
    }
    size_t mb_cap = 0;
    size_t coll_floor = 0;
    uint8_t tb = 0;
    uint8_t sb = 0;
    if (!rd_sz(is, mb_cap) || !rd_sz(is, coll_floor) || !rd_u8(is, tb) || !rd_u8(is, sb)) {
        return false;
    }
    c.training_micro_batch_cap = mb_cap;
    c.training_collect_floor = coll_floor;
    c.training_timing_to_stderr = (tb != 0);
    (void)sb; // legacy reserved byte; ignored
    if (!is.good()) {
        return true;
    }
    const int peek2 = is.peek();
    if (peek2 == std::istream::traits_type::eof()) {
        return true;
    }
    uint8_t rm = 0;
    if (!rd_u8(is, rm)) {
        return false;
    }
    if (rm <= 1u) {
        c.training_sycl_route_mode = static_cast<moe::SyclRouteMode>(rm);
    } else if (rm == 2u) {
        // Legacy checkpoints stored former "Off" as 2 — policy removed; normalize to Auto.
        c.training_sycl_route_mode = moe::SyclRouteMode::Auto;
    }
    c.sycl_trit_quant_min_moe_dim = 128;
    if (!is.good()) {
        return true;
    }
    {
        const int peek3 = is.peek();
        if (peek3 == std::istream::traits_type::eof()) {
            return true;
        }
        size_t sq_min = 0;
        if (!rd_sz(is, sq_min)) {
            return false;
        }
        c.sycl_trit_quant_min_moe_dim = std::max<size_t>(size_t{1}, sq_min);
    }
    if (!is.good()) {
        return true;
    }
    {
        const int peek4 = is.peek();
        if (peek4 == std::istream::traits_type::eof()) {
            return true;
        }
        uint8_t gl = 0;
        if (!rd_u8(is, gl)) {
            return false;
        }
        c.goodness_log_level = std::min<uint32_t>(2u, static_cast<uint32_t>(gl));
    }
    if (!is.good()) {
        return true;
    }
    {
        const int peek5 = is.peek();
        if (peek5 == std::istream::traits_type::eof()) {
            return true;
        }
        uint8_t agn = 1;
        if (!rd_u8(is, agn)) {
            return false;
        }
        c.allow_generated_negatives = (agn != 0);
    }
    if (!is.good()) {
        return true;
    }
    {
        const int peek6 = is.peek();
        if (peek6 == std::istream::traits_type::eof()) {
            return true;
        }
        size_t dmax = 0;
        size_t jmax = 0;
        size_t tmax = 0;
        if (!rd_sz(is, dmax) || !rd_sz(is, jmax) || !rd_sz(is, tmax)) {
            return false;
        }
        c.directory_max_lines = std::max<size_t>(size_t{1}, dmax);
        c.max_jsonl_local_samples = std::max<size_t>(size_t{1}, jmax);
        c.max_text_length = std::max<size_t>(size_t{1}, tmax);

        // Corpus tail: older checkpoints wrote three u64 (directory cap, jsonl cap, line/utf8 cap)
        // after allow_generated_negatives; newer writes a fourth (min_text_length). Reading a
        // fourth field when only three exist misaligns epoch/slots and corrupts deserialization.
        // fmt >= 6 always has four fields; fmt 3–5 may have three or four — disambiguate with
        // num_slots vs moe_num_experts.
        if (checkpoint_fmt >= 6) {
            size_t tmin = 0;
            if (!rd_sz(is, tmin)) {
                return false;
            }
            c.min_text_length = std::max<size_t>(size_t{1}, tmin);
        } else {
            const std::streampos pos_after_three = is.tellg();
            if (!is.good()) {
                return false;
            }
            const uint64_t expected_slots = static_cast<uint64_t>(c.moe_num_experts);

            uint64_t u0 = 0, u1 = 0, u2 = 0, u3 = 0, u4 = 0;
            is.seekg(pos_after_three);
            if (!rd_u64(is, u0) || !rd_u64(is, u1) || !rd_u64(is, u2) || !rd_u64(is, u3) || !rd_u64(is, u4)) {
                return false;
            }
            const std::streampos pos_after_h3 = is.tellg();
            const bool h3 = (u4 == expected_slots);

            uint64_t tmin_c = 0, s0 = 0, s1 = 0, s2 = 0, s3 = 0, s4 = 0;
            is.seekg(pos_after_three);
            if (!rd_u64(is, tmin_c) || !rd_u64(is, s0) || !rd_u64(is, s1) || !rd_u64(is, s2) || !rd_u64(is, s3) ||
                !rd_u64(is, s4)) {
                return false;
            }
            const std::streampos pos_after_h4 = is.tellg();
            const bool h4 = (s4 == expected_slots);

            const bool tmin_plausible =
                (tmin_c > 0u) && (tmin_c <= static_cast<uint64_t>(c.max_text_length)) && (tmin_c < (1ull << 24));

            if (h4 && (!h3 || tmin_plausible)) {
                c.min_text_length = std::max<size_t>(size_t{1}, static_cast<size_t>(tmin_c));
                is.seekg(pos_after_h4);
            } else if (h3) {
                c.min_text_length = size_t{1};
                is.seekg(pos_after_h3);
            } else if (h4) {
                c.min_text_length = std::max<size_t>(size_t{1}, static_cast<size_t>(tmin_c));
                is.seekg(pos_after_h4);
            } else {
                return false;
            }
            if (!is.good()) {
                return false;
            }
        }
        if (c.min_text_length > c.max_text_length) {
            std::swap(c.min_text_length, c.max_text_length);
        }
    }
    if (checkpoint_fmt >= 7) {
        size_t cq = 0;
        if (!rd_sz(is, cq) || !rd_u32(is, c.collect_empty_backoff_base_ms) ||
            !rd_u32(is, c.collect_empty_backoff_max_shift) || !rd_u32(is, c.collect_empty_backoff_cap_ms) ||
            !rd_u32(is, c.metrics_heartbeat_sec)) {
            return false;
        }
        c.checkpoint_async_queue_max = std::max<size_t>(size_t{1}, cq);
    }
    if (checkpoint_fmt >= 8) {
        uint8_t pr = 0;
        uint8_t cpu_fb_discard = 0;
        size_t chunk_sz = 0;
        size_t trb = 0;
        uint32_t cwm = static_cast<uint32_t>(default_pipeline_config().collect_window_ms);
        std::string cdd;
        if (checkpoint_fmt >= 17) {
            if (!rd_u8(is, pr) || !rd_sz(is, chunk_sz) || !rd_sz(is, trb) || !rd_u32(is, cwm)) {
                return false;
            }
        } else {
            if (!rd_u8(is, pr) || !rd_u8(is, cpu_fb_discard) || !rd_sz(is, chunk_sz) || !rd_sz(is, trb) ||
                !rd_u32(is, cwm)) {
                return false;
            }
            (void)cpu_fb_discard;
        }
        c.training_parallel_contrastive_rows = (pr != 0);
        if (checkpoint_fmt >= 12) {
            size_t pb = 1;
            if (!rd_sz(is, pb)) {
                return false;
            }
            c.training_parallel_batches = std::max<size_t>(size_t{1}, pb);
        }
        c.ff_expert_chunk_size = chunk_sz;
        c.target_routes_per_batch = std::max<size_t>(size_t{1}, trb);
        c.collect_window_ms = std::max(1u, cwm);
        if (checkpoint_fmt >= 9) {
            uint32_t cmr = default_pipeline_config().collect_min_rows_per_batch;
            if (!rd_u32(is, cmr)) {
                return false;
            }
            c.collect_min_rows_per_batch = std::max(1u, cmr);
        } else {
            c.collect_min_rows_per_batch = default_pipeline_config().collect_min_rows_per_batch;
        }
        if (!rd_str(is, cdd)) {
            return false;
        }
        c.training_checkpoint_data_dir = std::move(cdd);
        if (checkpoint_fmt >= 10) {
            uint32_t spr = 0;
            uint32_t sgdi = 0;
            size_t gf3mc = 0;
            uint8_t ffb = 0;
            uint32_t ffc = 0;
            size_t lrc = 0;
            if (!rd_u32(is, spr) || !rd_u32(is, sgdi) || !rd_sz(is, gf3mc) || !rd_u8(is, ffb) || !rd_u32(is, ffc) ||
                !rd_sz(is, lrc)) {
                return false;
            }
            c.sycl_prereserve_gib = spr;
            c.sycl_gpu_device_index = static_cast<int32_t>(sgdi);
            c.gf3_sycl_min_weight_cells = gf3mc;
            c.ff_multi_row_batch = (ffb != 0);
            c.ff_multi_row_slots_chunk = ffc;
            c.lazy_moe_resident_cap = lrc;
            // fmt 14+ blobs write gf3_ff_batched_weight_mib after lazy_moe_resident_cap (see write_pipeline_config).
            if (checkpoint_fmt >= 14) {
                size_t gf3wm = 0;
                if (!rd_sz(is, gf3wm)) {
                    return false;
                }
                c.gf3_ff_batched_weight_mib = gf3wm;
            }
        }
    }
    if (checkpoint_fmt >= 13) {
        uint32_t ppli = 0;
        uint32_t pss = 0;
        uint32_t cfts = 0;
        uint32_t cfcs = 0;
        uint32_t tppm = 0;
        uint32_t tirm = 0;
        size_t tebli = 0;
        size_t tebmi = 0;
        size_t tqrds = 0;
        if (!rd_u32(is, ppli) || !rd_u32(is, pss) || !rd_u32(is, cfts) || !rd_u32(is, cfcs) || !rd_u32(is, tppm) ||
            !rd_u32(is, tirm) || !rd_sz(is, tebli) || !rd_sz(is, tebmi) || !rd_sz(is, tqrds)) {
            return false;
        }
        c.prefill_progress_log_interval_sec = std::max<uint32_t>(1u, ppli);
        c.prefill_stall_warn_sec = std::max<uint32_t>(1u, pss);
        c.collect_first_sample_timeout_sec = std::max<uint32_t>(1u, cfts);
        c.collect_ff_coalesce_sleep_us = cfcs;
        c.training_pause_poll_ms = tppm;
        c.training_idle_retry_ms = tirm;
        c.training_empty_batch_log_interval = std::max<size_t>(size_t{1}, tebli);
        c.training_empty_batch_metrics_interval = std::max<size_t>(size_t{1}, tebmi);
        c.train_queue_resync_discard_slack = std::max<size_t>(size_t{1}, tqrds);
    } else {
        const PipelineConfig d = default_pipeline_config();
        c.prefill_progress_log_interval_sec = d.prefill_progress_log_interval_sec;
        c.prefill_stall_warn_sec = d.prefill_stall_warn_sec;
        c.collect_first_sample_timeout_sec = d.collect_first_sample_timeout_sec;
        c.collect_ff_coalesce_sleep_us = d.collect_ff_coalesce_sleep_us;
        c.training_pause_poll_ms = d.training_pause_poll_ms;
        c.training_idle_retry_ms = d.training_idle_retry_ms;
        c.training_empty_batch_log_interval = d.training_empty_batch_log_interval;
        c.training_empty_batch_metrics_interval = d.training_empty_batch_metrics_interval;
        c.train_queue_resync_discard_slack = d.train_queue_resync_discard_slack;
    }
    if (checkpoint_fmt >= 14) {
        uint32_t pcm = 0;
        if (!rd_u32(is, pcm)) {
            return false;
        }
        c.sycl_prereserve_chunk_mib = std::max<uint32_t>(1u, pcm);
    } else {
        c.sycl_prereserve_chunk_mib = default_pipeline_config().sycl_prereserve_chunk_mib;
    }
    if (checkpoint_fmt >= 15) {
        uint8_t gl = 0;
        if (!rd_u8(is, gl)) {
            return false;
        }
        c.gf3_sycl_submit_grid_log = (gl != 0);
    }
    if (checkpoint_fmt >= 16) {
        size_t sm = 0;
        uint8_t ign = 0;
        if (!rd_sz(is, sm) || !rd_u8(is, ign)) {
            return false;
        }
        c.gf3_ff_multislot_slots_chunk_max = sm;
        c.gf3_ff_multislot_ignore_host_slot_budget = (ign != 0);
    }
    return true;
}

} // namespace

AutonomousTrainingPipeline::AutonomousTrainingPipeline() : config_(default_pipeline_config()) {}

AutonomousTrainingPipeline::~AutonomousTrainingPipeline() {
    stop_requested_ = true;
    if (training_thread_.joinable()) {
        training_thread_.join();
    }
    shutdown_checkpoint_writer();
    if (synthesizer_) {
        synthesizer_->stop();
    }
    shutdown_components();
}

bool AutonomousTrainingPipeline::initialize(const PipelineConfig& config) {
    qmini_training_breadcrumb("pipeline:initialize:enter");
    std::lock_guard<std::mutex> lock(config_mutex_);

    shutdown_components();

    set_state(PipelineState::INITIALIZING);
    config_ = config;
    parallel_batches_ceiling_ = std::max<size_t>(size_t{1}, config_.training_parallel_batches);
    live_parallel_batches_.store(parallel_batches_ceiling_, std::memory_order_relaxed);

    if (!initialize_components()) {
        qmini_training_breadcrumb("pipeline:initialize:components_failed");
        set_state(PipelineState::FAILED);
        return false;
    }
    
    set_state(PipelineState::READY);
    qmini_training_breadcrumb("pipeline:initialize:ok");
    return true;
}

bool AutonomousTrainingPipeline::initialize_components() {
#if defined(USE_SYCL) && USE_SYCL
    ::q_mini_wasm_v2::sycl_kernels::gf3_sycl_apply_runtime_host_config(
        config_.sycl_gpu_device_index,
        static_cast<uint64_t>(config_.gf3_sycl_min_weight_cells),
        config_.gf3_sycl_submit_grid_log);
    {
        size_t ff_mib = config_.gf3_ff_batched_weight_mib;
        if (ff_mib == 0u) {
            ff_mib = default_pipeline_config().gf3_ff_batched_weight_mib;
        }
        q_mini_wasm_v2::core::moe::gf3_ff_set_batched_weight_host_budget_mib(ff_mib);
    }
    std::cout << "[TrainingPipeline] SYCL batched FF host weight budget: "
               << q_mini_wasm_v2::core::moe::gf3_ff_batched_weight_host_budget_effective_mib()
               << " MiB effective (training.gf3_ff_batched_weight_mib; 0 uses config/pipeline_defaults.toml)\n";
    // GPU-first reservation pass (opt-in via TOML training.sycl_prereserve_gib): pins USM with malloc_device on the
    // GF3 queue — Task Manager "Shared GPU memory" reflects these holds. This is NOT a suballocator for FF kernels;
    // GF3 still uses its own USM (see fused pos/neg scratch pool). Large pre-reserve + heavy per-step allocations can
    // still stress some iGPU drivers; set sycl_prereserve_gib=0 if you see UR OOR despite free system RAM.
    if (config_.moe_num_experts >= 8) {
        constexpr size_t kMiB = size_t{1024} * size_t{1024};
        constexpr size_t kGiB = size_t{1024} * kMiB;
        const size_t reserve_gib = static_cast<size_t>(config_.sycl_prereserve_gib);
        const size_t reserve_target = reserve_gib * kGiB;
        const size_t reserve_chunk =
            static_cast<size_t>(std::max<uint32_t>(1u, config_.sycl_prereserve_chunk_mib)) * kMiB;
        std::string reserve_note;
        // Always call (target may be 0): reserve_sycl_device_memory_bytes frees any prior process-global holds first.
        // Without this, switching TOML from sycl_prereserve_gib=32 to 0 in a long-lived qminiwasm.exe never released
        // g_sycl_reserved_ptrs — Task Manager kept showing ~30 GiB Shared GPU memory despite the new config.
        const size_t reserved = ::q_mini_wasm_v2::sycl_kernels::reserve_sycl_device_memory_bytes(
            reserve_target, reserve_chunk, reserve_target > 0, &reserve_note);
        if (reserve_gib > 0) {
            std::cout << "[TrainingPipeline] SYCL pre-reserve: requested_gib="
                      << static_cast<unsigned long long>(reserve_target / kGiB)
                      << " reserved_gib=" << static_cast<unsigned long long>(reserved / kGiB)
                      << " (" << reserve_note
                      << ") - separate USM holds (not the GF3 forward scratch pool); lowers headroom for new "
                         "malloc_device if set very high on iGPU"
                      << std::endl;
        } else {
            std::cout << "[TrainingPipeline] SYCL pre-reserve: disabled (training.sycl_prereserve_gib=0); "
                         "released any prior session USM holds (" << reserve_note << ")\n";
        }
    }
#endif

    // Initialize DataSynthesizer
    synthesizer_ = std::make_unique<DataSynthesizer>();
    synthesizer_->set_queue_limits(
        config_.max_raw_queue_depth,
        config_.max_train_queue_depth,
        config_.max_acquisition_queue_depth
    );
    synthesizer_->set_contrastive_resync_discard_slack(config_.train_queue_resync_discard_slack);
    synthesizer_->set_local_corpus_limits(
        config_.directory_max_lines,
        config_.max_jsonl_local_samples,
        config_.min_text_length,
        config_.max_text_length);

    // 1) Local corpus (disk) and 2) data_sources.toml can run in parallel: they touch disjoint
    // synthesizer state (local_samples_ vs acquisition_mgr_) and reduce wall-clock init wait.
    std::future<bool> local_future;
    const bool want_local = !config_.data_path.empty();
    if (want_local) {
        const std::string path_copy = config_.data_path;
        local_future = std::async(std::launch::async, [this, path_copy]() {
            return synthesizer_->load_local_data(path_copy);
        });
    }

    const std::string& ds_path = config_.data_sources_toml_path.empty()
        ? std::string("config/data_sources.toml")
        : config_.data_sources_toml_path;
    const bool have_config = synthesizer_->use_config(ds_path);
    if (have_config) {
        std::cout << "[TrainingPipeline] Data sources config loaded: " << ds_path << std::endl;
    } else {
        std::cerr << "[TrainingPipeline] WARN: Failed to load data_sources.toml at: " << ds_path << std::endl;
    }

    bool have_local = false;
    if (want_local) {
        have_local = local_future.get();
        if (have_local) {
            std::cout << "[TrainingPipeline] Local data loaded from: " << config_.data_path << std::endl;
        } else {
            std::cerr << "[TrainingPipeline] WARN: Could not load local data from: " << config_.data_path
                      << " (continuing if TOML/web sources load)" << std::endl;
        }
    }

    if (!have_local && !have_config) {
        std::cerr << "[TrainingPipeline] ERROR: No local samples and no data_sources.toml — cannot train"
                  << std::endl;
        return false;
    }
    if (have_local && have_config) {
        std::cout << "[TrainingPipeline] Hybrid feeds: interleaving local lines with config/web batches"
                  << std::endl;
    } else if (have_local) {
        std::cout << "[TrainingPipeline] Local-only (no usable data_sources.toml)" << std::endl;
    } else {
        std::cout << "[TrainingPipeline] Web/config-only (no local corpus)" << std::endl;
    }
    
    // Initialize Forward-Forward Learner
    learning::FFConfig ff_config;
    ff_config.num_layers = config_.ff_num_layers;
    ff_config.neurons_per_layer = config_.ff_layer_width;
    ff_config.learning_rate = static_cast<int>(config_.ff_learning_rate_step);
    ff_learner_ = std::make_unique<learning::ForwardForwardLearner>(ff_config);
    
    // Initialize MoE Router
    moe::ExpertConfig router_config;
    router_config.total_experts = config_.moe_num_experts;
    // Align router active experts with TOML-driven top-k (min(moe_top_k, moe_experts)).
    {
        const size_t route_k = std::max<size_t>(
            size_t{1},
            std::min(config_.moe_top_k, adaptive_route_topk_cap(config_)));
        router_config.active_experts = route_k;
        if (route_k < config_.moe_top_k) {
            std::cout << "[TrainingPipeline] Router active top-k aligned to training cap: "
                      << route_k << " (configured moe_top_k=" << config_.moe_top_k << ")"
                      << std::endl;
        }
    }
    router_config.routing_qutrits = std::max<size_t>(1u, config_.routing_qutrits);
    router_config.sycl_route_mode = config_.training_sycl_route_mode;
    {
        const char* pref = "auto";
        if (router_config.sycl_route_mode == moe::SyclRouteMode::On) {
            pref = "on";
        }
        bool expect_sycl = false;
#if defined(USE_SYCL) && USE_SYCL
        expect_sycl = moe::moe_routing_sycl_desired(router_config.total_experts, router_config.sycl_route_mode);
#endif
        std::cout << "[TrainingPipeline] MoE symplectic routing logits: sycl_route_mode=" << pref
                  << ", expect_sycl_device=" << (expect_sycl ? 1 : 0)
#if defined(USE_SYCL) && USE_SYCL
                  << " (GF3 FF + routing + trit quant use SYCL/GPU in this build)"
#else
                  << " (non-SYCL TU: GPU pipeline unavailable — use q_mini_wasm_v2_core with USE_SYCL=1)"
#endif
                  << std::endl;
    }
    router_ = std::make_unique<moe::MoERouter>(router_config);
    
    // Create experts
    moe::ExpertNetwork::ExpertConfig expert_config;
    expert_config.input_dim = config_.moe_input_dim;
    expert_config.output_dim = config_.moe_output_dim;
    expert_config.hidden_dim = config_.moe_hidden_dim;
    expert_config.num_layers = std::max<size_t>(1u, config_.moe_expert_internal_layers);
    expert_config.ff_active_internal_layers = config_.moe_ff_active_internal_layers;
    expert_template_ = expert_config;

    if (config_.lazy_moe_experts) {
        experts_.clear();
        experts_.resize(config_.moe_num_experts);
        expert_touch_generation_.assign(config_.moe_num_experts, 0);
        expert_active_pin_counts_.assign(config_.moe_num_experts, 0u);
        evicted_expert_weights_.clear();
        expert_touch_clock_ = 1;
        expert_resident_count_ = 0;
        std::cout << "[TrainingPipeline] Lazy MoE: " << config_.moe_num_experts
                  << " logical experts, top_k=" << config_.moe_top_k
                  << ", resident_cap=" << lazy_resident_expert_cap()
                  << ", materialize stacks on first route (internal_layers="
                  << expert_template_.num_layers << ", ff_active_internal_layers="
                  << expert_template_.ff_active_internal_layers << ")" << std::endl;
    } else {
        experts_ = create_experts(config_.moe_num_experts, expert_config);
        expert_touch_generation_.assign(config_.moe_num_experts, 0);
        expert_active_pin_counts_.assign(config_.moe_num_experts, 0u);
        evicted_expert_weights_.clear();
        expert_touch_clock_ = 1;
        expert_resident_count_ = experts_.size();
        std::cout << "[TrainingPipeline] Eager MoE: materialized " << experts_.size()
                  << " experts at init (high memory)" << std::endl;
    }

    expert_ff_mutexes_.clear();
    expert_ff_mutexes_.reserve(config_.moe_num_experts);
    for (size_t i = 0; i < config_.moe_num_experts; ++i) {
        expert_ff_mutexes_.emplace_back(std::make_unique<std::mutex>());
    }
    
    // Initialize BettiExtractor
    betti_extractor_ = std::make_unique<qgnn::BettiExtractor>(config_.betti_max_qutrits);
    
    // Initialize GraphTableau
    graph_tableau_ = std::make_unique<qgnn::GraphTableau>(config_.graph_initial_nodes);
    
    // Build initial graph edges
    size_t edges_to_add = config_.graph_initial_edges;
    size_t nodes = config_.graph_initial_nodes;
    for (size_t i = 0; i < edges_to_add && i < nodes * (nodes - 1) / 2; ++i) {
        size_t from = i % nodes;
        size_t to = (i + 1) % nodes;
        if (from != to) {
            graph_tableau_->add_edge(from, to);
        }
    }

    moe::gf3_sycl_probe_log_device();
#if defined(_OPENMP)
    // Match OpenMP team to logical CPUs unless OMP_NUM_THREADS overrides (reduces “idle cores” vs MSVC defaults).
    {
        const unsigned hc = std::thread::hardware_concurrency();
        if (hc > 0u) {
            omp_set_num_threads(static_cast<int>(hc));
        }
    }
    // Nested active levels (OpenMP 3.0+). MSVC's OpenMP import library does not provide these entry points.
#if (_OPENMP >= 200805) && !defined(_MSC_VER)
    omp_set_max_active_levels(8);
#endif
    std::cout << "[TrainingPipeline] OpenMP expert/layer parallelism: max_threads=" << omp_get_max_threads()
              << " (per-route expert parallelism: OpenMP when top_k>1)" << std::endl;
    std::cout << "[TrainingPipeline] OpenMP nested levels="
#if (_OPENMP >= 200805) && !defined(_MSC_VER)
              << omp_get_max_active_levels()
#else
              << "n/a"
#endif
              << " (parallel contrastive rows follow training.parallel_contrastive_rows in TOML)"
              << std::endl;
#endif
    
    return true;
}

std::vector<std::unique_ptr<moe::GF3MultiLayerExpert>> AutonomousTrainingPipeline::create_experts(
    size_t num_experts,
    const moe::ExpertNetwork::ExpertConfig& expert_config
) {
    std::vector<std::unique_ptr<moe::GF3MultiLayerExpert>> experts;
    experts.reserve(num_experts);
    
    for (size_t i = 0; i < num_experts; ++i) {
        // Create GF3MultiLayerExpert for each expert slot
        auto expert = std::make_unique<moe::GF3MultiLayerExpert>(expert_config);
        experts.push_back(std::move(expert));
    }
    
    return experts;
}

moe::GF3MultiLayerExpert* AutonomousTrainingPipeline::ensure_expert(size_t expert_idx, bool pin_for_use) {
    std::lock_guard<std::mutex> lock(experts_mutex_);
    if (expert_idx >= experts_.size()) {
        return nullptr;
    }
    if (experts_[expert_idx]) {
        touch_expert_locked(expert_idx);
        if (pin_for_use && expert_idx < expert_active_pin_counts_.size()) {
            ++expert_active_pin_counts_[expert_idx];
        }
        return experts_[expert_idx].get();
    }

    if (config_.lazy_moe_experts) {
        const size_t cap = lazy_resident_expert_cap();
        while (expert_resident_count_ >= cap) {
            if (!spill_one_expert_locked(expert_idx)) {
                break;
            }
        }
    }

    auto expert = std::make_unique<moe::GF3MultiLayerExpert>(expert_template_);
    auto blob_it = evicted_expert_weights_.find(expert_idx);
    if (blob_it != evicted_expert_weights_.end()) {
        std::string spill_blob;
        if (!read_spill_payload(blob_it->second, spill_blob)) {
            std::cerr << "[TrainingPipeline] WARN: failed to read spilled expert payload " << expert_idx
                      << "; starting from fresh weights." << std::endl;
        }
        std::istringstream iss(spill_blob, std::ios::binary);
        if (!expert->DeserializeWeights(iss)) {
            std::cerr << "[TrainingPipeline] WARN: failed to restore spilled expert " << expert_idx
                      << "; starting from fresh weights." << std::endl;
        } else {
            delete_spill_file_if_marker(blob_it->second);
            evicted_expert_weights_.erase(blob_it);
        }
    }
    experts_[expert_idx] = std::move(expert);
    ++expert_resident_count_;
    touch_expert_locked(expert_idx);
    if (pin_for_use && expert_idx < expert_active_pin_counts_.size()) {
        ++expert_active_pin_counts_[expert_idx];
    }
    return experts_[expert_idx].get();
}

void AutonomousTrainingPipeline::release_expert_pin(size_t expert_idx) {
    std::lock_guard<std::mutex> lock(experts_mutex_);
    if (expert_idx >= expert_active_pin_counts_.size()) {
        return;
    }
    uint32_t& pins = expert_active_pin_counts_[expert_idx];
    if (pins > 0u) {
        --pins;
    }
}

uint32_t AutonomousTrainingPipeline::pinned_expert_count() const {
    std::lock_guard<std::mutex> lock(experts_mutex_);
    uint32_t pinned = 0u;
    for (uint32_t pins : expert_active_pin_counts_) {
        if (pins > 0u) {
            ++pinned;
        }
    }
    return pinned;
}

size_t AutonomousTrainingPipeline::lazy_resident_expert_cap() const {
    if (!config_.lazy_moe_experts) {
        return experts_.size();
    }
    if (config_.lazy_moe_resident_cap > 0) {
        return std::max(size_t{1}, std::min(experts_.size(), config_.lazy_moe_resident_cap));
    }
    const size_t k = adaptive_route_topk_cap(config_);
    const size_t e = std::max<size_t>(size_t{1}, experts_.size());
    // Per-batch route budget × concurrent process_batch workers matches Go's derived default
    // target_routes ≈ collect_limit × moe_top_k × training.parallel_batches when target_routes_per_batch = 0.
    const uint64_t per_batch_routes_u64 =
        static_cast<uint64_t>(effective_target_routes_per_batch(config_));
    const uint64_t pb_u64 =
        static_cast<uint64_t>(std::max<size_t>(size_t{1}, effective_parallel_batches()));
    uint64_t combined_routes_u64 = per_batch_routes_u64;
    if (pb_u64 > 1u &&
        per_batch_routes_u64 <= std::numeric_limits<uint64_t>::max() / pb_u64) {
        combined_routes_u64 = per_batch_routes_u64 * pb_u64;
    }
    const double routes_budget = static_cast<double>(combined_routes_u64);
    const double e_d = static_cast<double>(e);
    // Expected unique experts touched under random top-k routing:
    // E[unique] = E * (1 - exp(-routes / E))
    const size_t expected_unique = static_cast<size_t>(
        std::ceil(e_d * (1.0 - std::exp(-routes_budget / e_d))));
    // Keep headroom above expected unique touches to avoid spill/reload churn.
    const size_t route_pressure_goal = static_cast<size_t>(std::ceil(static_cast<double>(expected_unique) * 1.25));
    const size_t goal = std::min(e, std::max(route_pressure_goal, k));
    return std::max(size_t{1}, goal);
}

int AutonomousTrainingPipeline::train_contrastive_row_impl(
    size_t row_index,
    const TrainingSample& positive_in,
    const std::optional<TrainingSample>& prepared_negative,
    bool trace_phases,
    bool kTiming,
    unsigned goodness_log_level,
    size_t effective_top_k,
    RowTrainMerge& merge,
    std::string& fatal_message
) {
    fatal_message.clear();
    merge = RowTrainMerge{};

    TernaryRouteInput pos_in = extract_ternary_route_input(positive_in);
    std::vector<ternary::Trit> positive_sample;
    if (pos_in.route_packed_t5.empty()) {
        positive_sample = std::move(pos_in.trits);
    }

    if (positive_sample.empty() && pos_in.route_packed_t5.empty()) {
        fatal_message = "DATA_ERROR: empty positive sample after conversion";
        return 2;
    }

    TernaryRouteInput neg_in;
    std::vector<ternary::Trit> negative_sample;
    if (prepared_negative) {
        neg_in = extract_ternary_route_input(*prepared_negative);
        if (neg_in.route_packed_t5.empty()) {
            negative_sample = std::move(neg_in.trits);
        }
        if (!negative_sample.empty() || !neg_in.route_packed_t5.empty()) {
            merge.neg_prepared_inc = 1;
        }
    }
    if (!prepared_negative || (negative_sample.empty() && neg_in.route_packed_t5.empty())) {
        merge.neg_missing_inc = 1;
        return 1;
    }

    const size_t route_trit_n = config_.moe_input_dim;
    if (trace_phases && row_index == 0) {
        const size_t pos_trace_n = pos_in.route_packed_t5.empty() ? positive_sample.size() : route_trit_n;
        const size_t neg_trace_n = neg_in.route_packed_t5.empty() ? negative_sample.size() : route_trit_n;
        std::cerr << "[TrainingPipeline] process_batch: phase=row0_contrastive_ready pos_trits=" << pos_trace_n
                  << " neg_trits=" << neg_trace_n << std::endl;
    }
    if (trace_phases && row_index == 0) {
        const size_t pos_trace_n = pos_in.route_packed_t5.empty() ? positive_sample.size() : route_trit_n;
        std::cerr << "[TrainingPipeline] process_batch: phase=before_first_route_topk trits=" << pos_trace_n
                  << " route_t5_bytes=" << pos_in.route_packed_t5.size() << std::endl;
    }

    std::vector<size_t> selected_experts;
    {
        std::lock_guard<std::mutex> rlk(router_route_mutex_);
        const auto timing_route_t0 =
            kTiming ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
        if (pos_in.route_packed_t5.empty()) {
            qmini_training_breadcrumb("pipeline:row:route_topk_unpacked");
            selected_experts = router_->route_topk(positive_sample);
        } else {
            qmini_training_breadcrumb("pipeline:row:route_topk_tritpack5");
            selected_experts = router_->route_topk_from_tritpack5(pos_in.route_packed_t5, route_trit_n);
        }
        merge.used_tritpack5_input_route = !pos_in.route_packed_t5.empty();
        merge.router_sycl_path = router_->symplectic_logits_last_used_sycl();
        if (kTiming) {
            merge.route_ms = elapsed_ms(timing_route_t0);
        }
    }

    std::vector<uint8_t> pos_ff_pack;
    std::vector<uint8_t> neg_ff_pack;
    ff_route_inputs_to_pack5(pos_in, positive_sample, route_trit_n, pos_ff_pack);
    ff_route_inputs_to_pack5(neg_in, negative_sample, route_trit_n, neg_ff_pack);

    if (selected_experts.size() > effective_top_k) {
        selected_experts.resize(effective_top_k);
    }
    if (trace_phases && row_index == 0) {
        std::cerr << "[TrainingPipeline] process_batch: phase=first_route_done topk=" << selected_experts.size()
                  << " sycl_used=" << (router_->symplectic_logits_last_used_sycl() ? 1 : 0)
                  << " sycl_note=\"" << router_->symplectic_logits_last_sycl_note() << "\""
                  << std::endl;
    }
    if (selected_experts.empty() && !experts_.empty()) {
        fatal_message = "ROUTER_ERROR: route_topk returned no experts";
        return 2;
    }

    std::vector<size_t> route_expert_idx;
    std::vector<moe::GF3MultiLayerExpert*> route_expert_ptr;
    std::vector<size_t> pinned_expert_idx;
    struct PinReleaseGuard final {
        AutonomousTrainingPipeline* self;
        std::vector<size_t>* pinned;
        ~PinReleaseGuard() {
            if (!self || !pinned) {
                return;
            }
            for (size_t idx : *pinned) {
                self->release_expert_pin(idx);
            }
            pinned->clear();
        }
    } pin_release_guard{this, &pinned_expert_idx};
    route_expert_idx.reserve(selected_experts.size());
    route_expert_ptr.reserve(selected_experts.size());
    for (size_t expert_idx : selected_experts) {
        if (expert_idx >= experts_.size()) {
            continue;
        }
        moe::GF3MultiLayerExpert* expert = nullptr;
        if (config_.lazy_moe_experts) {
            expert = ensure_expert(expert_idx, true);
        } else {
            expert = experts_[expert_idx].get();
        }
        if (!expert) {
            continue;
        }
        if (expert_idx >= expert_ff_mutexes_.size()) {
            fatal_message = "INTERNAL: expert_ff_mutexes_ not sized for expert index";
            return 2;
        }
        route_expert_idx.push_back(expert_idx);
        route_expert_ptr.push_back(expert);
        if (config_.lazy_moe_experts) {
            pinned_expert_idx.push_back(expert_idx);
        }
    }

    const int nr = static_cast<int>(route_expert_ptr.size());
    std::vector<uint32_t> pos_good(static_cast<size_t>(nr), 0);
    std::vector<uint32_t> neg_good(static_cast<size_t>(nr), 0);

    qmini_training_breadcrumb("pipeline:row:ff_experts");
    const auto timing_ff_experts_t0 =
        kTiming ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    const bool use_parallel_experts = nr > 1;
    const size_t ff_chunk = ff_expert_chunk_count(config_, nr);
    for (size_t base = 0; base < static_cast<size_t>(nr); base += ff_chunk) {
        const int chunk_lo = static_cast<int>(base);
        const int chunk_end = static_cast<int>(std::min(base + ff_chunk, static_cast<size_t>(nr)));
        const int chunk_nr = chunk_end - chunk_lo;

        bool batched_done = false;
        if (chunk_nr > 1) {
            std::vector<moe::GF3MultiLayerExpert*> gf3_chunk;
            gf3_chunk.reserve(static_cast<size_t>(chunk_nr));
            for (int k = 0; k < chunk_nr; ++k) {
                const int ri = chunk_lo + k;
                gf3_chunk.push_back(route_expert_ptr[static_cast<size_t>(ri)]);
            }
            {
                std::vector<size_t> lock_order;
                lock_order.reserve(static_cast<size_t>(chunk_nr));
                for (int k = 0; k < chunk_nr; ++k) {
                    lock_order.push_back(route_expert_idx[static_cast<size_t>(chunk_lo + k)]);
                }
                std::sort(lock_order.begin(), lock_order.end());
                {
                    std::vector<std::unique_lock<std::mutex>> ff_locks;
                    ff_locks.reserve(lock_order.size());
                    for (size_t eidx : lock_order) {
                        ff_locks.emplace_back(*expert_ff_mutexes_[eidx]);
                    }
                    if (moe::GF3MultiLayerExpert::TryTrainForwardForwardBatched(
                            gf3_chunk, pos_ff_pack, neg_ff_pack)) {
                        for (int k = 0; k < chunk_nr; ++k) {
                            const int ri = chunk_lo + k;
                            const auto route_g = gf3_chunk[static_cast<size_t>(k)]->last_forward_forward_route_goodness();
                            pos_good[static_cast<size_t>(ri)] = route_g.first;
                            neg_good[static_cast<size_t>(ri)] = route_g.second;
                        }
                        batched_done = true;
                    }
                }
            }
        }

        if (!batched_done) {
#if defined(_OPENMP)
            std::atomic<bool> ff_ex_fail{false};
            std::string ff_ex_msg;
            std::mutex ff_ex_mu;
#pragma omp parallel for schedule(static) if(use_parallel_experts && chunk_nr > 1)
            for (int k = 0; k < chunk_nr; ++k) {
                if (ff_ex_fail.load(std::memory_order_relaxed)) {
                    continue;
                }
                try {
                    const int ri = chunk_lo + k;
                    const size_t expert_idx = route_expert_idx[static_cast<size_t>(ri)];
                    moe::GF3MultiLayerExpert* expert = route_expert_ptr[static_cast<size_t>(ri)];
                    std::lock_guard<std::mutex> flk(*expert_ff_mutexes_[expert_idx]);
                    expert->TrainForwardForward(pos_ff_pack, neg_ff_pack);
                    const auto route_g = expert->last_forward_forward_route_goodness();
                    pos_good[static_cast<size_t>(ri)] = route_g.first;
                    neg_good[static_cast<size_t>(ri)] = route_g.second;
                } catch (const std::exception& ex) {
                    std::lock_guard<std::mutex> lk(ff_ex_mu);
                    if (!ff_ex_fail.exchange(true)) {
                        ff_ex_msg = ex.what();
                    }
                }
            }
            if (ff_ex_fail.load()) {
                fatal_message = std::move(ff_ex_msg);
                return 2;
            }
#else
            try {
                run_parallel_indices(static_cast<size_t>(chunk_nr), use_parallel_experts && chunk_nr > 1, [&](size_t ku) {
                    const int ri = chunk_lo + static_cast<int>(ku);
                    const size_t expert_idx = route_expert_idx[static_cast<size_t>(ri)];
                    moe::GF3MultiLayerExpert* expert = route_expert_ptr[static_cast<size_t>(ri)];
                    std::lock_guard<std::mutex> flk(*expert_ff_mutexes_[expert_idx]);
                    expert->TrainForwardForward(pos_ff_pack, neg_ff_pack);
                    const auto route_g = expert->last_forward_forward_route_goodness();
                    pos_good[static_cast<size_t>(ri)] = route_g.first;
                    neg_good[static_cast<size_t>(ri)] = route_g.second;
                });
            } catch (const std::exception& ex) {
                fatal_message = ex.what();
                return 2;
            }
#endif
        }
        std::cerr << "[TrainingPipeline] ff_train_progress row=" << row_index << " experts_done=" << chunk_end << "/"
                  << nr << " chunk=" << ff_chunk << std::endl;
    }
    if (kTiming && nr > 0) {
        merge.ff_ms = elapsed_ms(timing_ff_experts_t0);
    }

    merge.route_expert_idx = std::move(route_expert_idx);
    merge.pos_good = std::move(pos_good);
    merge.neg_good = std::move(neg_good);

    if (trace_phases && row_index == 0) {
        std::cerr << "[TrainingPipeline] process_batch: phase=first_row_ff_train_done route_experts=" << nr
                  << std::endl;
    }

    if (goodness_log_level >= 2u && row_index < 3 && nr > 0 && !route_expert_ptr.empty()) {
        const size_t out_dim = route_expert_ptr[0]->GetConfig().output_dim;
        std::fprintf(stderr,
                     "[TrainingPipeline][Goodness] row=%zu MoE_input_trits=%zu expert_last_layer_dim=%zu | "
                     "per_route (#nonzero_out_pos #nonzero_out_neg delta): ",
                     row_index,
                     route_trit_n,
                     out_dim);
        for (int ri = 0; ri < nr; ++ri) {
            const uint32_t pg = merge.pos_good[static_cast<size_t>(ri)];
            const uint32_t ng = merge.neg_good[static_cast<size_t>(ri)];
            const int32_t d = static_cast<int32_t>(pg) - static_cast<int32_t>(ng);
            std::fprintf(stderr,
                         "e%zu(%u/%u/%d)%s",
                         merge.route_expert_idx[static_cast<size_t>(ri)],
                         static_cast<unsigned>(pg),
                         static_cast<unsigned>(ng),
                         static_cast<int>(d),
                         (ri + 1 < nr) ? " " : "\n");
        }
    }

    return 0;
}

bool AutonomousTrainingPipeline::try_ff_multi_row_slot_batch_(
    const std::vector<CollectedContrastiveRow>& batch_work,
    bool kTiming,
    size_t effective_top_k,
    size_t wave_parallel_batches,
    std::vector<RowTrainMerge>& out_merges,
    int64_t& out_ff_ms_total,
    std::string& fatal_message) {
    fatal_message.clear();
    out_ff_ms_total = 0;
#if !defined(USE_SYCL) || !USE_SYCL
    (void)batch_work;
    (void)kTiming;
    (void)effective_top_k;
    (void)wave_parallel_batches;
    (void)out_merges;
    return false;
#else
    if (!config_.ff_multi_row_batch) {
        return false;
    }
    if (batch_work.empty()) {
        return false;
    }

    out_merges.assign(batch_work.size(), RowTrainMerge{});

    struct SlotMeta {
        size_t row;
        int ri;
        size_t expert_idx;
    };
    std::vector<moe::GF3FfTrainingSlot> slots;
    std::vector<SlotMeta> metas;
    std::vector<std::vector<uint8_t>> row_pos_packs;
    std::vector<std::vector<uint8_t>> row_neg_packs;
    std::vector<size_t> pinned_expert_idx;
    struct PinReleaseGuard {
        AutonomousTrainingPipeline* self = nullptr;
        std::vector<size_t>* pinned = nullptr;
        ~PinReleaseGuard() {
            if (!self || !pinned) {
                return;
            }
            for (size_t idx : *pinned) {
                self->release_expert_pin(idx);
            }
            pinned->clear();
        }
    } pin_release_guard{this, &pinned_expert_idx};
    slots.reserve(batch_work.size() * std::max<size_t>(size_t{1}, effective_top_k));
    row_pos_packs.resize(batch_work.size());
    row_neg_packs.resize(batch_work.size());

    const size_t route_trit_n = config_.moe_input_dim;
    const size_t need_route_bytes = (route_trit_n + 4u) / 5u;

    struct MultiRowRouteCtx {
        TernaryRouteInput pos_in;
        TernaryRouteInput neg_in;
        std::vector<ternary::Trit> positive_sample;
        std::vector<ternary::Trit> negative_sample;
    };
    std::vector<MultiRowRouteCtx> row_ctx;
    std::vector<std::vector<uint8_t>> route_packs;
    row_ctx.reserve(batch_work.size());
    route_packs.reserve(batch_work.size());

    for (size_t row_index = 0; row_index < batch_work.size(); ++row_index) {
        row_ctx.emplace_back();
        MultiRowRouteCtx& ctx = row_ctx.back();
        ctx.pos_in = extract_ternary_route_input(batch_work[row_index].positive);
        if (ctx.pos_in.route_packed_t5.empty()) {
            ctx.positive_sample = std::move(ctx.pos_in.trits);
        }

        if (ctx.positive_sample.empty() && ctx.pos_in.route_packed_t5.empty()) {
            fatal_message = "DATA_ERROR: empty positive sample after conversion";
            out_merges.clear();
            return false;
        }

        if (batch_work[row_index].prepared_negative) {
            ctx.neg_in = extract_ternary_route_input(*batch_work[row_index].prepared_negative);
            if (ctx.neg_in.route_packed_t5.empty()) {
                ctx.negative_sample = std::move(ctx.neg_in.trits);
            }
            if (!ctx.negative_sample.empty() || !ctx.neg_in.route_packed_t5.empty()) {
                out_merges[row_index].neg_prepared_inc = 1;
            }
        }
        if (!batch_work[row_index].prepared_negative ||
            (ctx.negative_sample.empty() && ctx.neg_in.route_packed_t5.empty())) {
            fatal_message =
                "TRAINING_ERROR: missing prepared negative for a contrastive row (multi-row SYCL batch)";
            out_merges.clear();
            return false;
        }

        std::vector<uint8_t> rp;
        if (!ctx.pos_in.route_packed_t5.empty()) {
            rp.assign(ctx.pos_in.route_packed_t5.begin(),
                      ctx.pos_in.route_packed_t5.begin() +
                          std::min(need_route_bytes, ctx.pos_in.route_packed_t5.size()));
            if (rp.size() < need_route_bytes) {
                rp.resize(need_route_bytes, 0);
            }
        } else {
            std::vector<int8_t> lanes(route_trit_n, 0);
            for (size_t i = 0; i < ctx.positive_sample.size() && i < route_trit_n; ++i) {
                lanes[i] = static_cast<int8_t>(ctx.positive_sample[i]);
            }
#if defined(USE_SYCL) && USE_SYCL
            rp = ::q_mini_wasm_v2::sycl_kernels::pack_int8_lanes_to_tritpack5_sycl(lanes, route_trit_n);
#else
            q::ternary::pack_batch_t5(lanes, rp);
#endif
            if (rp.size() < need_route_bytes) {
                rp.resize(need_route_bytes, 0);
            }
        }

        route_packs.emplace_back(std::move(rp));
    }

    std::vector<std::vector<size_t>> all_routes;
    {
        std::lock_guard<std::mutex> rlk(router_route_mutex_);
        const auto timing_route_t0 =
            kTiming ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
        all_routes = router_->route_topk_many_from_tritpack5(route_packs, route_trit_n);
        const int64_t ms_batch = kTiming ? elapsed_ms(timing_route_t0) : int64_t{0};
        const int64_t per_row_ms = batch_work.empty() ? int64_t{0}
                                                      : (ms_batch / static_cast<int64_t>(batch_work.size()));
        for (size_t ri = 0; ri < batch_work.size(); ++ri) {
            out_merges[ri].used_tritpack5_input_route = !row_ctx[ri].pos_in.route_packed_t5.empty();
            out_merges[ri].router_sycl_path = router_->symplectic_logits_last_used_sycl();
            if (kTiming) {
                out_merges[ri].route_ms = per_row_ms;
            }
        }
    }

    if (all_routes.size() != batch_work.size()) {
        fatal_message = "INTERNAL: batched route count mismatch";
        out_merges.clear();
        return false;
    }

    for (size_t row_index = 0; row_index < batch_work.size(); ++row_index) {
        MultiRowRouteCtx& ctx = row_ctx[row_index];
        std::vector<size_t> selected_experts = std::move(all_routes[row_index]);
        if (selected_experts.size() > effective_top_k) {
            selected_experts.resize(effective_top_k);
        }
        if (selected_experts.empty() && !experts_.empty()) {
            fatal_message = "ROUTER_ERROR: route_topk returned no experts";
            out_merges.clear();
            return false;
        }

        auto& row_pos_pack = row_pos_packs[row_index];
        auto& row_neg_pack = row_neg_packs[row_index];
        ff_route_inputs_to_pack5(ctx.pos_in, ctx.positive_sample, route_trit_n, row_pos_pack);
        ff_route_inputs_to_pack5(ctx.neg_in, ctx.negative_sample, route_trit_n, row_neg_pack);

        std::vector<size_t> route_expert_idx;
        std::vector<moe::GF3MultiLayerExpert*> route_expert_ptr;
        route_expert_idx.reserve(selected_experts.size());
        route_expert_ptr.reserve(selected_experts.size());
        for (size_t expert_idx : selected_experts) {
            if (expert_idx >= experts_.size()) {
                continue;
            }
            moe::GF3MultiLayerExpert* expert = nullptr;
            if (config_.lazy_moe_experts) {
                expert = ensure_expert(expert_idx, true);
            } else {
                expert = experts_[expert_idx].get();
            }
            if (!expert) {
                continue;
            }
            if (expert_idx >= expert_ff_mutexes_.size()) {
                fatal_message = "INTERNAL: expert_ff_mutexes_ not sized for expert index";
                out_merges.clear();
                return false;
            }
            route_expert_idx.push_back(expert_idx);
            route_expert_ptr.push_back(expert);
            if (config_.lazy_moe_experts) {
                pinned_expert_idx.push_back(expert_idx);
            }
        }

        const int nr = static_cast<int>(route_expert_ptr.size());
        out_merges[row_index].route_expert_idx = std::move(route_expert_idx);
        out_merges[row_index].pos_good.assign(static_cast<size_t>(nr), 0);
        out_merges[row_index].neg_good.assign(static_cast<size_t>(nr), 0);

        for (int ri = 0; ri < nr; ++ri) {
            moe::GF3MultiLayerExpert* g3 = route_expert_ptr[static_cast<size_t>(ri)];
            const size_t eidx = out_merges[row_index].route_expert_idx[static_cast<size_t>(ri)];
            moe::GF3FfTrainingSlot slot{};
            slot.expert = g3;
            slot.positive_pack5_ref = &row_pos_packs[row_index];
            slot.negative_pack5_ref = &row_neg_packs[row_index];
            slots.push_back(std::move(slot));
            metas.push_back(SlotMeta{row_index, ri, eidx});
        }
    }

    if (slots.empty()) {
        fatal_message = "TRAINING_ERROR: multi-row SYCL batch produced zero MoE slots (check routes/top-k)";
        out_merges.clear();
        return false;
    }

    const size_t budget_mib = moe::gf3_ff_batched_weight_host_budget_effective_mib();
    // Slot ceiling from training.gf3_ff_batched_weight_mib and the bytes-for-B staging model (not VRAM fill).
    size_t budget_slots_cap = moe::gf3_ff_multislot_host_budget_slots_cap(slots[0].expert, budget_mib);
    if (config_.gf3_ff_multislot_ignore_host_slot_budget) {
        // Legacy name: previously this bypassed the MiB-derived cap entirely (budget_slots_cap = max), so chunks
        // could hit gf3_ff_multislot_slots_chunk_max (e.g. 8192) with staging far above the host budget model — not
        // "32 GiB VRAM", just oversized host/SYCL staging. We only apply a modest uplift; raise MiB to go larger.
        constexpr size_t kRelaxedNumer = 5u;
        constexpr size_t kRelaxedDenom = 4u;
        if (budget_slots_cap < std::numeric_limits<size_t>::max() / kRelaxedNumer) {
            budget_slots_cap = (budget_slots_cap * kRelaxedNumer) / kRelaxedDenom;
        }
    }
    if (config_.gf3_ff_multislot_slots_chunk_max > 0u) {
        budget_slots_cap =
            std::min<size_t>(budget_slots_cap, static_cast<size_t>(config_.gf3_ff_multislot_slots_chunk_max));
    }
    {
        static std::atomic<int> s_budget_warn_once{0};
        if (budget_slots_cap < 128u && s_budget_warn_once.fetch_add(1) == 0) {
            std::cerr << "[TrainingPipeline] multi-row FF: budget_slots_cap=" << budget_slots_cap
                      << " is low (wide MoE × top_k ⇒ many slots). Throughput stalls on tiny SYCL chunks. "
                         "Raise training.gf3_ff_batched_weight_mib and/or set ff_multi_row_slots_chunk; "
                         "see prior multi-row FF chunk line for MiB effective.\n";
        }
    }
    size_t chunk_cap = std::numeric_limits<size_t>::max();
    if (config_.ff_multi_row_slots_chunk >= 2u) {
        chunk_cap = std::min<size_t>(static_cast<size_t>(config_.ff_multi_row_slots_chunk), budget_slots_cap);
    } else {
        chunk_cap = budget_slots_cap;
    }
    // Throughput: avoid tiny SYCL submits when the host budget allows larger B. Do not override an explicit
    // training.ff_multi_row_slots_chunk (user chose chunk size). Keep floor modest — 2048×slots staging is not
    // "the full job", only one GF3 wave; a huge floor inflated buffers vs tensor needs.
    const size_t min_gpu_chunk_floor = 512u;
    if (config_.ff_multi_row_slots_chunk < 2u && chunk_cap < min_gpu_chunk_floor && slots.size() > chunk_cap) {
        const size_t target = std::min(min_gpu_chunk_floor, slots.size());
        chunk_cap = std::min(target, budget_slots_cap);
    }

    const bool cap_from_toml_slots =
        (config_.ff_multi_row_slots_chunk >= 2u) &&
        (chunk_cap == static_cast<size_t>(config_.ff_multi_row_slots_chunk));
    static std::atomic<unsigned> s_multirow_chunk_diag_count{0};
    const unsigned diag_i = s_multirow_chunk_diag_count.fetch_add(1u, std::memory_order_relaxed);
    if (diag_i < 4u) {
        std::cerr << "[TrainingPipeline] multi-row FF chunk: rows=" << batch_work.size() << " slots=" << slots.size()
                  << " chunk_cap=" << chunk_cap << " = min(ff_multi_row_slots_chunk=" << config_.ff_multi_row_slots_chunk
                  << ", budget_slots_cap=" << budget_slots_cap << ")"
                  << " binding=" << (cap_from_toml_slots ? "ff_multi_row_slots_chunk" : "other")
                  << " ignore_host_slot_budget=" << (config_.gf3_ff_multislot_ignore_host_slot_budget ? 1 : 0)
                  << " gf3_ff_multislot_slots_chunk_max=" << config_.gf3_ff_multislot_slots_chunk_max
                  << " gf3_ff_batched_weight_mib_effective=" << budget_mib
                  << " pipeline.gf3_ff_batched_weight_mib=" << config_.gf3_ff_batched_weight_mib << std::endl;
    }
    if (kTiming) {
        static std::atomic<uint64_t> s_mr_ff_timing_log_i{0};
        const uint64_t li = s_mr_ff_timing_log_i.fetch_add(1u, std::memory_order_relaxed);
        if (li < 8u || (li % 512u) == 0u) {
            std::cout << "[TrainingPipeline] multi-row FF: rows=" << batch_work.size() << " slots=" << slots.size()
                      << " slots_chunk_cap=" << chunk_cap << " gf3_ff_batched_weight_mib=" << budget_mib
                      << " budget_slots_cap=" << budget_slots_cap << " (timing log: first 8 + every 512th batch)\n";
        }
    }

    std::vector<uint32_t> row_route_hits(batch_work.size(), 0u);
    if (metas.size() != slots.size()) {
        fatal_message = "INTERNAL: slot/meta cardinality mismatch";
        out_merges.clear();
        return false;
    }

    bool ok_all = true;
    int64_t ff_ms_accum = 0;

    for (size_t base = 0; base < slots.size() && ok_all;) {
        const size_t remain = slots.size() - base;
        size_t cnt = std::min(chunk_cap, remain);
        // Avoid a trailing singleton chunk when we can fold it into the previous submit (fewer queue passes).
        if (remain > cnt && (remain - cnt) == 1) {
            cnt = remain;
        }

        const auto tchunk0 =
            kTiming ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};

        const auto sub = std::span<const moe::GF3FfTrainingSlot>(slots).subspan(base, cnt);
        std::vector<SlotMeta> chunk_metas;
        chunk_metas.reserve(cnt);
        for (size_t i = 0; i < cnt; ++i) {
            const size_t midx = base + i;
            if (midx >= metas.size()) {
                fatal_message = "INTERNAL: meta index out of range while snapshotting chunk";
                out_merges.clear();
                return false;
            }
            chunk_metas.push_back(metas[midx]);
        }

        std::vector<size_t> lock_order_chunk;
        lock_order_chunk.reserve(cnt);
        std::vector<uint8_t> seen_expert(expert_ff_mutexes_.size(), uint8_t{0});
        for (size_t i = 0; i < cnt; ++i) {
            const size_t eidx = chunk_metas[i].expert_idx;
            if (eidx >= expert_ff_mutexes_.size()) {
                fatal_message = "INTERNAL: expert index out of range while building lock set";
                out_merges.clear();
                return false;
            }
            if (!seen_expert[eidx]) {
                seen_expert[eidx] = uint8_t{1};
                lock_order_chunk.push_back(eidx);
            }
        }

        std::vector<uint32_t> out_pg;
        std::vector<uint32_t> out_ng;
        bool ok = false;
        {
            std::vector<std::unique_lock<std::mutex>> ff_locks;
            ff_locks.reserve(lock_order_chunk.size());
            for (size_t eidx : lock_order_chunk) {
                if (eidx >= expert_ff_mutexes_.size() || !expert_ff_mutexes_[eidx]) {
                    fatal_message = "INTERNAL: expert_ff_mutex null or out of range (multi-row chunk)";
                    ok = false;
                    break;
                }
                ff_locks.emplace_back(*expert_ff_mutexes_[eidx]);
            }
            if (fatal_message.empty()) {
                ok = moe::GF3MultiLayerExpert::TryTrainForwardForwardMultiSlot(sub, &out_pg, &out_ng);
            }
        }
        if (!ok) {
            if (fatal_message.empty()) {
                fatal_message =
                    "SYCL ForwardForward failed: TryTrainForwardForwardMultiSlot returned false for a multi-row chunk";
            }
            ok_all = false;
            break;
        }
        if (out_pg.size() != cnt || out_ng.size() != cnt) {
            fatal_message = "INTERNAL: TryTrainForwardForwardMultiSlot output length mismatch";
            ok_all = false;
            break;
        }
        for (size_t i = 0; i < cnt; ++i) {
            const SlotMeta& meta = chunk_metas[i];
            if (meta.row >= out_merges.size()) {
                fatal_message = "INTERNAL: meta.row out of range (multi-row chunk)";
                ok_all = false;
                break;
            }
            if (meta.ri < 0 || static_cast<size_t>(meta.ri) >= out_merges[meta.row].pos_good.size() ||
                static_cast<size_t>(meta.ri) >= out_merges[meta.row].neg_good.size()) {
                fatal_message = "INTERNAL: meta.ri out of range (multi-row chunk)";
                ok_all = false;
                break;
            }
            out_merges[meta.row].pos_good[static_cast<size_t>(meta.ri)] = out_pg[i];
            out_merges[meta.row].neg_good[static_cast<size_t>(meta.ri)] = out_ng[i];
            const size_t need_routes = out_merges[meta.row].route_expert_idx.size();
            if (wave_parallel_batches <= 1 && need_routes > 0u &&
                ++row_route_hits[meta.row] == need_routes) {
                intrabatch_contrastive_rows_done_.fetch_add(1u, std::memory_order_relaxed);
            }
        }
        if (kTiming) {
            ff_ms_accum += elapsed_ms(tchunk0);
        }
        if (kTiming && (base == 0 || (base + cnt) >= slots.size())) {
            static std::atomic<uint64_t> s_mr_ff_prog_log_i{0};
            const uint64_t pi = s_mr_ff_prog_log_i.fetch_add(1u, std::memory_order_relaxed);
            if (pi < 4u || (pi % 128u) == 0u) {
                const uint32_t rows_done =
                    (wave_parallel_batches <= 1)
                        ? intrabatch_contrastive_rows_done_.load(std::memory_order_relaxed)
                        : 0u;
                std::cerr << "[TrainingPipeline] multi-row FF progress: slots_done=" << (base + cnt)
                          << "/" << slots.size() << " rows_done="
                          << (wave_parallel_batches <= 1 ? std::to_string(rows_done) : std::string("n/a"))
                          << "/" << batch_work.size() << " (throttled)\n";
            }
        }
        base += cnt;
    }

    if (!ok_all) {
        if (fatal_message.empty()) {
            fatal_message = "SYCL ForwardForward multi-row batch aborted";
        }
        out_merges.clear();
        return false;
    }
    if (kTiming) {
        out_ff_ms_total = ff_ms_accum;
        const int64_t total_slots_ll = static_cast<int64_t>(slots.size());
        for (size_t r = 0; r < out_merges.size(); ++r) {
            const size_t nr = out_merges[r].route_expert_idx.size();
            out_merges[r].ff_ms =
                (total_slots_ll > 0) ? (out_ff_ms_total * static_cast<int64_t>(nr)) / total_slots_ll : 0;
        }
    }

    return true;
#endif
}

bool AutonomousTrainingPipeline::try_ff_multi_row_slot_batch_invoke_(
    const std::vector<CollectedContrastiveRow>& batch_work,
    bool kTiming,
    size_t effective_top_k,
    size_t wave_parallel_batches,
    std::vector<RowTrainMerge>& out_merges,
    int64_t& out_ff_ms_total,
    std::string& fatal_message) {
    // Do not use MSVC __try/__except here: C++ exceptions (including from SYCL) may be mapped to SEH 0xE06D7363
    // and mis-reported as a GPU "fault" with no useful message. Use normal C++ exception propagation instead.
    return try_ff_multi_row_slot_batch_(batch_work, kTiming, effective_top_k, wave_parallel_batches, out_merges,
                                        out_ff_ms_total, fatal_message);
}

void AutonomousTrainingPipeline::touch_expert_locked(size_t expert_idx) {
    if (expert_idx >= expert_touch_generation_.size()) {
        return;
    }
    expert_touch_generation_[expert_idx] = expert_touch_clock_++;
}

bool AutonomousTrainingPipeline::spill_one_expert_locked(size_t protected_idx) {
    size_t victim = experts_.size();
    uint64_t oldest = std::numeric_limits<uint64_t>::max();
    const uint64_t cooldown =
        static_cast<uint64_t>(std::max<size_t>(size_t{1}, adaptive_route_topk_cap(config_)) * size_t{64});
    bool found_outside_cooldown = false;
    uint64_t cooldown_skipped_here = 0;
    for (size_t i = 0; i < experts_.size(); ++i) {
        if (i == protected_idx || !experts_[i]) {
            continue;
        }
        if (i < expert_active_pin_counts_.size() && expert_active_pin_counts_[i] > 0u) {
            continue;
        }
        const uint64_t gen = (i < expert_touch_generation_.size()) ? expert_touch_generation_[i] : 0;
        const bool outside_cooldown = (expert_touch_clock_ > gen + cooldown);
        if (!found_outside_cooldown && !outside_cooldown) {
            ++cooldown_skipped_here;
            continue;
        }
        if (outside_cooldown && !found_outside_cooldown) {
            found_outside_cooldown = true;
            oldest = std::numeric_limits<uint64_t>::max();
            victim = experts_.size();
        }
        if (gen < oldest) {
            oldest = gen;
            victim = i;
        }
    }
    if (victim >= experts_.size()) {
        for (size_t i = 0; i < experts_.size(); ++i) {
            if (i == protected_idx || !experts_[i]) {
                continue;
            }
            if (i < expert_active_pin_counts_.size() && expert_active_pin_counts_[i] > 0u) {
                continue;
            }
            const uint64_t gen = (i < expert_touch_generation_.size()) ? expert_touch_generation_[i] : 0;
            if (gen < oldest) {
                oldest = gen;
                victim = i;
            }
        }
    }
    if (victim >= experts_.size()) {
        expert_eviction_cooldown_skips_total_.fetch_add(cooldown_skipped_here, std::memory_order_relaxed);
        return false;
    }
    moe::GF3MultiLayerExpert* gf3 = experts_[victim].get();
    std::ostringstream oss(std::ios::binary);
    gf3->SerializeWeights(oss);
    const std::string blob = std::move(oss).str();
    std::error_code ec;
    const std::string spill_dir = expert_spill_dir(config_);
    std::filesystem::create_directories(spill_dir, ec);
    const std::string spill_path =
        (std::filesystem::path(spill_dir) / ("expert_" + std::to_string(victim) + ".bin")).string();
    std::ofstream spill(spill_path, std::ios::binary | std::ios::trunc);
    if (spill.is_open()) {
        spill.write(blob.data(), static_cast<std::streamsize>(blob.size()));
    }
    if (spill && static_cast<bool>(spill.flush())) {
        auto prev = evicted_expert_weights_.find(victim);
        if (prev != evicted_expert_weights_.end()) {
            delete_spill_file_if_marker(prev->second);
        }
        evicted_expert_weights_[victim] = spill_marker_for_path(spill_path);
    } else {
        evicted_expert_weights_[victim] = blob;
        std::cerr << "[TrainingPipeline] WARN: spill-to-disk failed for expert " << victim
                  << " (using RAM spill fallback)." << std::endl;
    }
    experts_[victim].reset();
    expert_touch_generation_[victim] = 0;
    if (expert_resident_count_ > 0) {
        --expert_resident_count_;
    }
    expert_eviction_cooldown_skips_total_.fetch_add(cooldown_skipped_here, std::memory_order_relaxed);
    expert_evictions_total_.fetch_add(1u, std::memory_order_relaxed);
    return true;
}

void AutonomousTrainingPipeline::shutdown_components() {
    if (synthesizer_) {
        synthesizer_->stop();
    }
    for (const auto& kv : evicted_expert_weights_) {
        delete_spill_file_if_marker(kv.second);
    }

    experts_.clear();
    expert_ff_mutexes_.clear();
    evicted_expert_weights_.clear();
    expert_touch_generation_.clear();
    expert_active_pin_counts_.clear();
    expert_touch_clock_ = 1;
    expert_resident_count_ = 0;
    router_.reset();
    ff_learner_.reset();
    synthesizer_.reset();
    betti_extractor_.reset();
    graph_tableau_.reset();
}

bool AutonomousTrainingPipeline::start_training() {
    qmini_training_breadcrumb("pipeline:start_training:enter");
    PipelineState warm = state_.load();
    if (warm == PipelineState::COMPLETE) {
        set_state(PipelineState::READY);
    }

    bool resumed_from_checkpoint = false;
    if (config_.training_auto_resume_from_checkpoint && state_.load() == PipelineState::READY &&
        current_epoch_.load() == 0 && current_batch_.load() == 0) {
        const PipelineConfig requested_config = config_;
        if (auto latest = latest_checkpoint_path(requested_config)) {
            std::cout << "[TrainingPipeline] Auto-resume: found checkpoint " << *latest << std::endl;
            if (auto why = checkpoint_incompatibility_reason(*latest, requested_config)) {
                std::cout << "[TrainingPipeline] Auto-resume: skipped checkpoint (config mismatch: "
                          << *why << ")" << std::endl;
            } else {
                if (import_model(*latest, &requested_config)) {
                    resumed_from_checkpoint = true;
                    std::cout << "[TrainingPipeline] Auto-resume: loaded checkpoint epoch="
                              << current_epoch_.load() << " batch=" << current_batch_.load()
                              << " samples_total=" << samples_processed_total_.load() << std::endl;
                } else {
                    std::cerr << "[TrainingPipeline] WARN: auto-resume failed for " << *latest
                              << " (starting fresh)" << std::endl;
                }
            }
        }
    } else if (!config_.training_auto_resume_from_checkpoint) {
        std::cout << "[TrainingPipeline] Auto-resume disabled (training.auto_resume_from_checkpoint=false); "
                     "starting from InitSession state only\n";
    }

    PipelineState expected = PipelineState::READY;
    if (!state_.compare_exchange_strong(expected, PipelineState::ACQUIRING_DATA)) {
        return false;  // Not in READY state
    }
    
    stop_requested_ = false;
    pause_requested_ = false;
    if (!resumed_from_checkpoint) {
        current_epoch_ = 0;
        current_batch_ = 0;
    }
    batch_inflight_active_.store(false);
    batch_inflight_done_.store(0);
    batch_inflight_target_.store(0);

    if (config_.moe_input_dim == 0) {
        std::cerr << "[TrainingPipeline] ERROR: moe_input_dim must be >= 1." << std::endl;
        {
            std::lock_guard<std::mutex> lock(metrics_mutex_);
            cached_metrics_.status_message = "CONFIG_ERROR: moe_input_dim must be >= 1";
        }
        set_state(PipelineState::FAILED);
        return false;
    }
    if (config_.moe_top_k == 0) {
        std::cerr << "[TrainingPipeline] ERROR: moe_top_k must be >= 1." << std::endl;
        {
            std::lock_guard<std::mutex> lock(metrics_mutex_);
            cached_metrics_.status_message = "CONFIG_ERROR: moe_top_k must be >= 1";
        }
        set_state(PipelineState::FAILED);
        return false;
    }
    
    // Start DataSynthesizer — thread counts come from PipelineConfig (TOML training.acquisition_threads /
    // training.perturbation_threads via Training_InitSession).
    std::cout << "[TrainingPipeline] DataSynthesizer threads: acquisition=" << config_.acquisition_threads
              << " perturbation=" << config_.perturbation_threads << " (training.acquisition_threads / "
                 "training.perturbation_threads)"
              << std::endl;
    synthesizer_->start(config_.acquisition_threads, config_.perturbation_threads);
    const size_t collect_limit = adaptive_collect_limit(config_);
    const size_t hard_cap = std::min(config_.batch_size, training_micro_batch_cap_for(config_));
    if (collect_limit < hard_cap && !pipeline_collect_hint_logged_.exchange(true)) {
        std::cout << "[TrainingPipeline] collect_limit=" << collect_limit << " < hard_cap=" << hard_cap
                  << " (unexpected: adaptive_collect_limit should equal min(batch_size, micro_batch_cap))"
                  << std::endl;
    }
    const size_t route_cap = adaptive_route_topk_cap(config_);
    if (route_cap < std::max<size_t>(size_t{1}, config_.moe_top_k)) {
        std::cout << "[TrainingPipeline] effective_top_k=" << route_cap << " < model.moe_top_k="
                  << config_.moe_top_k << " (capped by model.moe_experts=" << config_.moe_num_experts << ")"
                  << std::endl;
    }
    if (synthesizer_) {
        const bool local_mode = synthesizer_->using_local_data();
        const bool config_mode = !config_.data_sources_toml_path.empty();
        std::cout << "[TrainingPipeline] Prefill start: mode local=" << (local_mode ? 1 : 0)
                  << " config=" << (config_mode ? 1 : 0)
                  << " target=" << config_.prefill_target_samples
                  << " timeout_ms=" << config_.prefill_timeout_ms
                  << " poll_ms=" << config_.prefill_poll_ms
                  << " queue_caps(acq/raw/train)="
                  << config_.max_acquisition_queue_depth << "/"
                  << config_.max_raw_queue_depth << "/"
                  << config_.max_train_queue_depth
                  << std::endl;
    }

    // Async acquisition ring-buffer warmup before first FF step.
    if (config_.enable_prefill_ring_buffer && synthesizer_) {
        qmini_training_breadcrumb("pipeline:prefill:wait");
        // prefill_target_samples==0 from TOML/InitSession => target 1 pair (fast start; full buffers use large N).
        const size_t target = std::max<size_t>(size_t{1}, config_.prefill_target_samples);
        if (config_.prefill_target_samples == 0u) {
            std::cout << "[TrainingPipeline] Prefill: training.prefill_target_samples=0 => waiting for first pair only "
                         "(raise prefill_target_samples to fill the ring before first batch)\n"
                      << std::flush;
        }
        const auto timeout = std::chrono::milliseconds(std::max<uint32_t>(1u, config_.prefill_timeout_ms));
        const auto poll = std::chrono::milliseconds(std::max<uint32_t>(1u, config_.prefill_poll_ms));
        const auto started = std::chrono::steady_clock::now();
        auto last_log_tp = started;
        auto last_progress_tp = started;
        size_t last_log_current = 0;
        size_t stagnant_polls = 0;
        bool reached = false;
        size_t current = 0;
        bool first_sample_logged = false;
        bool stall_warn_logged = false;
        while (!stop_requested_) {
            auto s = synthesizer_->get_stats();
            // train_queue_ stores one TrainingSample per entry; perturbation pushes pos+neg as two
            // entries per contrastive pair. Count pairs, not queue slots, so prefill matches samples.
            current = s.raw_queue_depth + (s.queue_depth / 2);
            if (!first_sample_logged && current > 0) {
                auto ms_since_start = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - started).count();
                std::cout << "[TrainingPipeline] Prefill first sample emitted at "
                          << ms_since_start << "ms (current=" << current << ")" << std::endl;
                first_sample_logged = true;
            }
            if (current >= target) {
                reached = true;
                break;
            }
            if (std::chrono::steady_clock::now() - started >= timeout) {
                break;
            }
            if (current > last_log_current) {
                last_progress_tp = std::chrono::steady_clock::now();
                stagnant_polls = 0;
            } else {
                ++stagnant_polls;
            }
            const auto now = std::chrono::steady_clock::now();
            if (now - last_log_tp >=
                std::chrono::seconds(std::max<uint32_t>(1u, config_.prefill_progress_log_interval_sec))) {
                const double sec =
                    std::max(0.001, std::chrono::duration<double>(now - last_log_tp).count());
                const double pairs_per_sec =
                    static_cast<double>(current >= last_log_current ? current - last_log_current : 0u) / sec;
                std::cout << "[TrainingPipeline] Prefill ring buffer: " << current
                          << "/" << target << " samples"
                          << " (+"
                          << (current >= last_log_current ? current - last_log_current : 0u)
                          << " in " << static_cast<int>(sec * 1000.0) << "ms"
                          << ", rate=" << pairs_per_sec << " pairs/s)"
                          << " q(train/raw/acq)=" << s.queue_depth << "/" << s.raw_queue_depth << "/"
                          << s.acquisition_queue_depth
                          << " blocked(raw/train/acq)="
                          << s.blocked_raw_pushes << "/" << s.blocked_train_pushes << "/"
                          << s.acquisition_blocked_pushes
                          << std::endl;
                last_log_tp = now;
                last_log_current = current;
            }
            if (!stall_warn_logged &&
                (now - last_progress_tp) >=
                    std::chrono::seconds(std::max<uint32_t>(1u, config_.prefill_stall_warn_sec))) {
                std::cerr << "[TrainingPipeline] WARN: prefill progress stalled for "
                          << std::chrono::duration_cast<std::chrono::seconds>(now - last_progress_tp).count()
                          << "s (current=" << current << "/" << target
                          << ", stagnant_polls=" << stagnant_polls
                          << ", q(train/raw/acq)=" << s.queue_depth << "/" << s.raw_queue_depth
                          << "/" << s.acquisition_queue_depth << ")" << std::endl;
                stall_warn_logged = true;
            }
            std::this_thread::sleep_for(poll);
        }

        {
            std::lock_guard<std::mutex> lock(metrics_mutex_);
            cached_metrics_.prefill_target_samples = target;
            cached_metrics_.prefill_current_samples = current;
            cached_metrics_.prefill_reached = reached;
            cached_metrics_.prefill_timeout_ms = config_.prefill_timeout_ms;
        }
        if (reached) {
            std::cout << "[TrainingPipeline] Prefill reached: " << current << "/" << target << std::endl;
        } else {
            std::cout << "[TrainingPipeline] Prefill timeout: " << current << "/" << target
                      << " (continuing startup)" << std::endl;
            if (current == 0) {
                std::string starvation_reason =
                    "PREFILL_STARVATION: no samples emitted before timeout (" +
                    std::to_string(config_.prefill_timeout_ms) +
                    "ms). Check source type aliases, extension filters, and min-length gating.";
                std::cerr << "[TrainingPipeline] " << starvation_reason << std::endl;
                std::lock_guard<std::mutex> lock(metrics_mutex_);
                cached_metrics_.status_message = starvation_reason;
            }
        }
    }
    
    // Start training thread
    {
        const size_t pending = pending_live_parallel_start_.exchange(0, std::memory_order_acq_rel);
        if (pending > 0) {
            const size_t v = std::max(size_t{1}, std::min(pending, parallel_batches_ceiling_));
            live_parallel_batches_.store(v, std::memory_order_relaxed);
            std::cout << "[TrainingPipeline] realtime live_parallel_batches initial=" << v << " (ceiling="
                      << parallel_batches_ceiling_ << ")" << std::endl;
        }
    }

    qmini_training_breadcrumb("pipeline:training_thread:spawn");
    training_thread_ = std::thread(&AutonomousTrainingPipeline::training_loop, this);
    
    return true;
}

void AutonomousTrainingPipeline::stop_training() {
    stop_requested_ = true;

    if (training_thread_.joinable()) {
        training_thread_.join();
    }

    if (synthesizer_) {
        synthesizer_->stop();
    }

    const PipelineState st = state_.load();
    if (st != PipelineState::COMPLETE && st != PipelineState::FAILED) {
        set_state(PipelineState::READY);
    }
}

void AutonomousTrainingPipeline::pause_training() {
    pause_requested_ = true;
}

void AutonomousTrainingPipeline::resume_training() {
    pause_requested_ = false;
    if (state_.load() == PipelineState::PAUSED) {
        set_state(PipelineState::TRAINING);
    }
}

void AutonomousTrainingPipeline::training_loop() {
    qmini_training_breadcrumb("pipeline:training_loop:enter");
    set_state(PipelineState::TRAINING);
    bool traced_first_process_batch = false;

    while (!stop_requested_) {
        // Check for pause
        if (pause_requested_) {
            set_state(PipelineState::PAUSED);
            while (pause_requested_ && !stop_requested_) {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(std::max<uint32_t>(1u, config_.training_pause_poll_ms)));
            }
            if (stop_requested_) break;
            set_state(PipelineState::TRAINING);
        }

        if (!traced_first_process_batch) {
            std::cerr << "[TrainingPipeline] training_loop: entering first process_batch() "
                         "(native path after Training_StartTraining returned)\n"
                      << std::flush;
        }

        // One or more concurrent process_batch() workers per tick (SYCL queue per OS thread when >1).
        // Scale train-queue / target_routes / lazy resident budgets with parallel_batches so pressure ratios stay sane.
        const size_t parallel_batches_requested =
            std::max<size_t>(size_t{1}, effective_parallel_batches());
        const size_t parallel_batches = parallel_batches_requested;
        size_t samples_trained_total = 0;
        size_t productive_batches = 0;
        try {
            if (parallel_batches == 1) {
                qmini_training_breadcrumb("pipeline:training_loop:process_batch");
                const size_t one = process_batch(parallel_batches);
                if (config_.training_timing_to_stderr) {
                    std::cerr << "[TrainingPipeline] process_batch returned: " << one << std::endl;
                }
                samples_trained_total = one;
                productive_batches = (one > 0) ? 1u : 0u;
            } else {
                std::vector<std::future<size_t>> fut;
                fut.reserve(parallel_batches);
                for (size_t bi = 0; bi < parallel_batches; ++bi) {
                    fut.emplace_back(std::async(std::launch::async, [this, parallel_batches]() {
                        qmini_training_breadcrumb("pipeline:training_loop:process_batch_parallel");
                        return process_batch(parallel_batches);
                    }));
                }
                for (auto& f : fut) {
                    const size_t got = f.get();
                    samples_trained_total += got;
                    if (got > 0) {
                        ++productive_batches;
                    }
                }
            }
        } catch (const std::exception& ex) {
            std::cerr << "[TrainingPipeline] FATAL (SYCL/FF): " << ex.what() << std::endl;
            {
                std::lock_guard<std::mutex> lock(metrics_mutex_);
                cached_metrics_.status_message = ex.what();
            }
            stop_requested_ = true;
            set_state(PipelineState::FAILED);
            break;
        } catch (...) {
            std::cerr << "[TrainingPipeline] FATAL (SYCL/FF): unknown exception" << std::endl;
            {
                std::lock_guard<std::mutex> lock(metrics_mutex_);
                cached_metrics_.status_message = "unknown exception";
            }
            stop_requested_ = true;
            set_state(PipelineState::FAILED);
            break;
        }

        if (!traced_first_process_batch) {
            traced_first_process_batch = true;
            std::cerr << "[TrainingPipeline] training_loop: first process_batch returned, samples_trained="
                      << samples_trained_total << " productive_batches=" << productive_batches
                      << "/" << parallel_batches << std::endl;
        }

        if (samples_trained_total == 0) {
            if (parallel_batches_requested > 1) {
                intrabatch_contrastive_rows_done_.store(0u, std::memory_order_relaxed);
            }
            // No samples available - DataSynthesizer not producing data
            // Log periodically but don't fail - APIs might recover
            ++consecutive_empty_batches_;
            if (consecutive_empty_batches_ %
                    std::max<size_t>(size_t{1}, config_.training_empty_batch_log_interval) ==
                1u) {
                std::cerr << "[TrainingPipeline] Waiting for DataSynthesizer to produce samples... "
                          << "(empty batches: " << consecutive_empty_batches_ << ")" << std::endl;
            }
            // Push live queue/epoch hints to MCP even when idle (GetProgress reads last_metrics).
            if (consecutive_empty_batches_ %
                    std::max<size_t>(size_t{1}, config_.training_empty_batch_metrics_interval) ==
                1u) {
                update_metrics();
                if (config_.enable_wui_streaming) {
                    emit_metrics();
                }
            }
            // Retry quickly: long sleeps here made training look idle while queues refilled.
            std::this_thread::yield();
            std::this_thread::sleep_for(
                std::chrono::milliseconds(std::max<uint32_t>(1u, config_.training_idle_retry_ms)));
            continue;  // Retry without incrementing counters
        }
        
        consecutive_empty_batches_ = 0;
        
        // Update batch counter only for productive process_batch calls.
        for (size_t i = 0; i < productive_batches; ++i) {
            ++current_batch_;

            // Topology evaluation
            if (config_.enable_betti_guidance &&
                current_batch_ % config_.topology_evaluation_interval == 0) {
                evaluate_topology();
            }

            // Frequent checkpoints prevent long runs from losing hours of progress.
            if (config_.enable_checkpoints && config_.checkpoint_interval > 0) {
                const uint64_t batch_now = current_batch_.load();
                if (batch_now > 0 && (batch_now % config_.checkpoint_interval) == 0 &&
                    last_checkpoint_batch_.load() != batch_now) {
                    checkpoint_if_needed();
                    last_checkpoint_batch_.store(batch_now);
                }
            }
        }
        
        // samples_trained_total = contrastive rows trained by all in-flight batches (not MoE route fan-out).
        if (config_.training_timing_to_stderr) {
            std::cerr << "[TrainingPipeline] updating samples: samples_trained_total=" << samples_trained_total
                      << " before=" << samples_processed_.load()
                      << " after=" << (samples_processed_.load() + samples_trained_total) << std::endl;
        }
        samples_processed_ += samples_trained_total;
        samples_processed_total_ += samples_trained_total;
        // Mid-batch progress adds train_batch_rows_done / intrabatch_contrastive_rows_done_ to GetProgress;
        // clear after commit to avoid double-count.
        {
            std::lock_guard<std::mutex> lock(metrics_mutex_);
            cached_metrics_.train_batch_rows_done = 0;
        }
        if (parallel_batches_requested > 1) {
            intrabatch_contrastive_rows_done_.store(0u, std::memory_order_relaxed);
        }
        bool training_complete = false;
        while (samples_processed_ >= config_.samples_per_epoch) {
            ++current_epoch_;
            samples_processed_ -= config_.samples_per_epoch;
            current_batch_ = 0;
            
            if (config_.enable_checkpoints && config_.checkpoint_interval > 0 &&
                (current_epoch_ % config_.checkpoint_interval) == 0) {
                checkpoint_if_needed();
            }
            
            if (current_epoch_ >= config_.num_epochs) {
                if (config_.enable_continuous_mode) {
                    // Continuous mode: auto-restart from epoch 1
                    ++loop_count_;
                    current_epoch_ = 0;
                    current_batch_ = 0;
                    // Log the restart
                    printf("[CONTINUOUS] Loop %u completed. Auto-restarting training...\n", loop_count_.load());
                    // Continue training without breaking
                } else {
                    // Normal mode: complete training
                    set_state(PipelineState::COMPLETE);
                    training_complete = true;
                    break;
                }
            }
        }
        if (training_complete) {
            break;
        }
        
        update_metrics();
        if (config_.enable_wui_streaming) {
            emit_metrics();
        }

        // Productive ticks: yield only (empty batches continue above with their own pacing).
        std::this_thread::yield();
    }
    
    if (state_.load() != PipelineState::COMPLETE && 
        state_.load() != PipelineState::FAILED) {
        set_state(PipelineState::STOPPING);
    }
}

size_t AutonomousTrainingPipeline::process_batch(size_t wave_parallel_batches) {
    const size_t wave_pb = std::max<size_t>(size_t{1}, wave_parallel_batches);
    qmini_training_breadcrumb("pipeline:process_batch:enter");
    // DEFENSIVE: if core components are missing, fail loudly (avoids opaque AV in collect / route).
    if (!synthesizer_) {
        std::fprintf(stderr, "[TrainingPipeline] FATAL: process_batch with null synthesizer_ (initialize() incomplete?)\n");
        std::fflush(stderr);
        return 0;
    }
    if (!router_) {
        std::fprintf(stderr, "[TrainingPipeline] FATAL: process_batch with null router_ (initialize() incomplete?)\n");
        std::fflush(stderr);
        return 0;
    }
#if defined(USE_SYCL) && USE_SYCL
    static std::atomic<bool> s_logged_sycl_ff_build{false};
    if (!s_logged_sycl_ff_build.exchange(true)) {
        std::fprintf(stderr,
                     "[TrainingPipeline] Build: USE_SYCL=1 - GF3 Forward-Forward is SYCL-only (no CPU FF path).\n");
        std::fflush(stderr);
    }
#endif
    const uint64_t evictions_start = expert_evictions_total_.load(std::memory_order_relaxed);
    const uint64_t cooldown_skips_start =
        expert_eviction_cooldown_skips_total_.load(std::memory_order_relaxed);
    // Only trace the first process_batch completion site — use exchange so parallel waves (512×)
    // do not all observe trace=true before any destructor cleared it (was stderr spam).
    const bool trace_phases =
        trace_first_process_batch_phases_.exchange(false, std::memory_order_acq_rel);
    if (trace_phases) {
        std::fprintf(stderr, "[TrainingPipeline] process_batch: this=%p phase=enter\n", static_cast<void*>(this));
        std::fflush(stderr);
    }

    if (config_.moe_input_dim == 0) {
        std::cerr << "[TrainingPipeline] moe_input_dim is 0; cannot form training vectors." << std::endl;
        return 0;
    }
    const bool kTiming = training_timing_enabled_for(config_);
    // Use wave_pb from training_loop (not live_parallel_batches_) so scaler cannot flip mid-wave and leave
    // some workers thinking they own single-writer inflight / intrabatch atomics.
    const bool kPublishBatchInflight = (wave_pb <= 1);
    const bool kPublishIntrabatchRowProgress = (wave_pb <= 1);
    if (kPublishBatchInflight) {
        std::lock_guard<std::mutex> lock(metrics_mutex_);
        cached_metrics_.train_batch_collect_limit = 0;
        cached_metrics_.train_batch_rows_done = 0;
        cached_metrics_.ff_route_steps_current_batch = 0;
    }
    const auto timing_batch_t0 = kTiming ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    int64_t timing_collect_ms = 0;
    int64_t timing_route_ms = 0;
    int64_t timing_ff_ms = 0;

    // Get samples from DataSynthesizer (prefer async pos+neg pairs from perturbation_worker).
    std::vector<CollectedContrastiveRow> batch_work;
    // Do not wait for full training.batch_size before first MoE step (see TOML collect_limit).
    const size_t collect_limit = std::max(size_t{1}, adaptive_collect_limit(config_));
    const size_t effective_top_k = adaptive_route_topk_cap(config_);
    uint32_t collect_window_ms = std::max(1u, config_.collect_window_ms);
    uint32_t min_rows_cfg = config_.collect_min_rows_per_batch;
    if (min_rows_cfg == 0u) {
        min_rows_cfg = static_cast<uint32_t>(std::min<size_t>(collect_limit, static_cast<size_t>(std::numeric_limits<uint32_t>::max())));
    } else {
        min_rows_cfg = std::min<uint32_t>(min_rows_cfg, static_cast<uint32_t>(std::min<size_t>(collect_limit, static_cast<size_t>(std::numeric_limits<uint32_t>::max()))));
    }
    // Do not cap collect_limit by a one-time queue_depth snapshot: it races the producer and can
    // force collect_limit=1 (one for-loop iteration) even when more pairs arrive milliseconds later.
    // The collect loop below naturally stops after draining the queue or hitting collect_limit pops.
    batch_work.reserve(collect_limit);
    const size_t collect_min_rows_effective = std::min(static_cast<size_t>(min_rows_cfg), collect_limit);

    // Timeout for acquiring the first row (seconds from PipelineConfig). After the first row, the loop
    // may still pop up to collect_limit without waiting on this wall clock.
    const auto max_wait_time =
        std::chrono::seconds(std::max<uint32_t>(1u, config_.collect_first_sample_timeout_sec));
    const auto start_time = std::chrono::steady_clock::now();
    auto last_heartbeat = start_time;
    size_t consecutive_empty_checks = 0;
    if (kPublishBatchInflight) {
        batch_inflight_active_.store(true);
        batch_inflight_done_.store(0);
        const uint64_t max_train_routes =
            static_cast<uint64_t>(collect_limit) * static_cast<uint64_t>(effective_top_k);
        const uint64_t total_inflight =
            static_cast<uint64_t>(collect_limit) + max_train_routes;
        uint32_t inflight_target = (total_inflight > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()))
            ? std::numeric_limits<uint32_t>::max()
            : static_cast<uint32_t>(total_inflight);
        if (inflight_target == 0u) {
            inflight_target = 1u;
        }
        batch_inflight_target_.store(inflight_target);
    }

    const auto timing_collect_t0 = kTiming ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    for (size_t i = 0; i < collect_limit; ++i) {
        // Check for timeout
        auto elapsed = std::chrono::steady_clock::now() - start_time;
        if (elapsed > max_wait_time) {
            std::cerr << "[TrainingPipeline] TIMEOUT: Failed to acquire sample " << (i + 1)
                      << "/" << collect_limit << " within " << config_.collect_first_sample_timeout_sec
                      << "s (training.collect_first_sample_timeout_sec). "
                      << "DataSynthesizer may not be producing samples." << std::endl;
            if (kPublishBatchInflight) {
                batch_inflight_active_.store(false);
                batch_inflight_done_.store(0);
                batch_inflight_target_.store(0);
            }
            return 0;  // Return 0 samples trained - no progress possible
        }
        
        TrainingSample pos_pair;
        TrainingSample neg_pair;
        if (synthesizer_->try_pop_contrastive_pair(pos_pair, neg_pair)) {
            batch_work.push_back({std::move(pos_pair), std::move(neg_pair)});
            if (kPublishBatchInflight) {
                batch_inflight_done_.store(static_cast<uint32_t>(batch_work.size()));
            }
            consecutive_empty_checks = 0;
            const auto now = std::chrono::steady_clock::now();
            const bool window_elapsed =
                (now - start_time >= std::chrono::milliseconds(collect_window_ms));
            // Do not flush a micro-batch on the timer until we have enough rows to amortize GPU work
            // (or we hit collect_limit). Otherwise every ~collect_window_ms we train on 1 row.
            if (batch_work.size() >= 1u && window_elapsed) {
                if (batch_work.size() >= collect_limit || batch_work.size() >= collect_min_rows_effective) {
                    break;
                }
            }
        } else {
            if (!batch_work.empty()) {
                // Async acquisition is decoupled from training: if we already have work,
                // do not block the training path waiting for a full collect_limit.
                break;
            }
            // No rows yet: wait for initial sample with exponential backoff (TOML-driven).
            consecutive_empty_checks++;
            const uint32_t base_ms = config_.collect_empty_backoff_base_ms;
            const uint32_t cap_ms = config_.collect_empty_backoff_cap_ms;
            const size_t max_shift = static_cast<size_t>(config_.collect_empty_backoff_max_shift);
            const size_t shift = std::min(consecutive_empty_checks, max_shift);
            uint64_t mult = static_cast<uint64_t>(base_ms);
            if (shift < 63) {
                mult <<= shift;
            } else {
                mult = std::numeric_limits<uint64_t>::max();
            }
            uint64_t wait_u64 = std::min(mult, static_cast<uint64_t>(cap_ms));
            const auto wait_ms = static_cast<unsigned long>(wait_u64);
            std::this_thread::sleep_for(std::chrono::milliseconds(wait_ms));
            --i;  // Retry this sample
        }

        const auto now = std::chrono::steady_clock::now();
        const uint32_t hb_sec = config_.metrics_heartbeat_sec;
        if (hb_sec == 0u || now - last_heartbeat >= std::chrono::seconds(static_cast<long long>(hb_sec))) {
            update_metrics();
            if (config_.enable_wui_streaming) {
                emit_metrics();
            }
            last_heartbeat = now;
        }
    }
    if (kTiming) {
        timing_collect_ms = elapsed_ms(timing_collect_t0);
    }
    
    if (batch_work.empty()) {
        std::cerr << "[TrainingPipeline] No training samples available - DataSynthesizer queue empty" << std::endl;
        if (kPublishBatchInflight) {
            batch_inflight_active_.store(false);
            batch_inflight_done_.store(0);
            batch_inflight_target_.store(0);
        }
        return 0;  // Return 0 samples trained - no progress was made
    }

    if (trace_phases) {
        std::fprintf(stderr,
                     "[TrainingPipeline] process_batch: phase=collect_done rows=%zu collect_limit=%zu\n",
                     batch_work.size(),
                     collect_limit);
        std::fflush(stderr);
    }
    qmini_training_breadcrumb("pipeline:process_batch:after_collect");

    {
        std::lock_guard<std::mutex> lock(metrics_mutex_);
        cached_metrics_.train_batch_collect_limit = static_cast<uint32_t>(batch_work.size());
        cached_metrics_.train_batch_rows_done = 0;
        cached_metrics_.ff_route_steps_current_batch = 0;
    }

    const unsigned goodness_log_level =
        static_cast<unsigned>(std::min<uint32_t>(2u, config_.goodness_log_level));
    goodness_log_banner_once(goodness_log_level);

    // Forward-Forward batch metrics: arithmetic mean over routes (tropical max/subtract is not a useful UI average).
    uint64_t sum_pos_goodness = 0;
    uint64_t sum_neg_goodness = 0;
    int64_t sum_delta = 0;
    
    // MoE metrics
    uint32_t total_routes = 0;
    std::vector<uint32_t> expert_counts(config_.moe_num_experts, 0);
    std::vector<int32_t> expert_deltas(config_.moe_num_experts, 0);
    uint32_t batch_negatives_prepared = 0;
    uint32_t batch_negatives_generated = 0;
    uint32_t batch_negatives_missing = 0;
    bool batch_used_tritpack5_input_route = false;
    bool batch_router_sycl_path_used = false;
    auto last_train_hb = std::chrono::steady_clock::now();
    auto last_timing_hb = std::chrono::steady_clock::now();
    const std::chrono::seconds pipeline_train_emit_period{static_cast<std::chrono::seconds::rep>(
        std::max<uint32_t>(1u, config_.metrics_heartbeat_sec))};
    // Allow an early [Pipeline] emit once FF routes exist (do not wait a full emit period from cold start).
    auto last_streaming_pipeline_emit =
        std::chrono::steady_clock::now() - pipeline_train_emit_period;

    const bool use_parallel_rows =
#if defined(_OPENMP)
        training_parallel_rows_enabled_for(config_) && batch_work.size() > 1;
#else
        false;
#endif

    auto merge_row_into_batch = [&](const RowTrainMerge& m, size_t row_index_for_metrics,
                                    bool increment_intrabatch_progress) {
        batch_used_tritpack5_input_route =
            batch_used_tritpack5_input_route || m.used_tritpack5_input_route;
        batch_router_sycl_path_used = batch_router_sycl_path_used || m.router_sycl_path;
        batch_negatives_prepared += m.neg_prepared_inc;
        timing_route_ms += m.route_ms;
        timing_ff_ms += m.ff_ms;
        const int nr = static_cast<int>(m.route_expert_idx.size());
        for (int ri = 0; ri < nr; ++ri) {
            const size_t expert_idx = m.route_expert_idx[static_cast<size_t>(ri)];
            if (expert_idx >= expert_counts.size()) {
                std::cerr << "[TrainingPipeline] merge_row_into_batch: dropping out-of-range expert_idx=" << expert_idx
                          << " (moe_num_experts=" << expert_counts.size() << ")\n";
                continue;
            }
            const uint32_t pos_goodness = m.pos_good[static_cast<size_t>(ri)];
            const uint32_t neg_goodness = m.neg_good[static_cast<size_t>(ri)];
            const int32_t delta = static_cast<int32_t>(pos_goodness) - static_cast<int32_t>(neg_goodness);

            sum_pos_goodness += static_cast<uint64_t>(pos_goodness);
            sum_neg_goodness += static_cast<uint64_t>(neg_goodness);
            sum_delta += static_cast<int64_t>(delta);
            expert_counts[expert_idx]++;
            expert_deltas[expert_idx] += delta;
            total_routes++;
            if (kPublishBatchInflight) {
                const uint64_t sum = static_cast<uint64_t>(collect_limit) + static_cast<uint64_t>(total_routes);
                batch_inflight_done_.store(static_cast<uint32_t>(
                    std::min<uint64_t>(sum, static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()))));
            }
            const auto hb_now = std::chrono::steady_clock::now();
            if (hb_now - last_train_hb >= pipeline_train_emit_period) {
                update_metrics();
                if (config_.enable_wui_streaming) {
                    emit_metrics();
                }
                last_train_hb = hb_now;
            }
        }
        {
            std::lock_guard<std::mutex> lock(metrics_mutex_);
            cached_metrics_.train_batch_rows_done =
                std::max(cached_metrics_.train_batch_rows_done,
                         static_cast<uint32_t>(row_index_for_metrics + 1));
            cached_metrics_.ff_route_steps_current_batch = total_routes;
            if (total_routes > 0) {
                cached_metrics_.ff_positive_goodness =
                    static_cast<uint32_t>(sum_pos_goodness / static_cast<uint64_t>(total_routes));
                cached_metrics_.ff_negative_goodness =
                    static_cast<uint32_t>(sum_neg_goodness / static_cast<uint64_t>(total_routes));
                cached_metrics_.ff_goodness_delta =
                    static_cast<int32_t>(sum_delta / static_cast<int64_t>(total_routes));
            }
        }

        if (config_.enable_wui_streaming && total_routes > 0) {
            const auto stream_now = std::chrono::steady_clock::now();
            if (stream_now - last_streaming_pipeline_emit >= pipeline_train_emit_period) {
                update_metrics();
                emit_metrics();
                last_streaming_pipeline_emit = stream_now;
            }
        }
        if (kPublishIntrabatchRowProgress && increment_intrabatch_progress) {
            intrabatch_contrastive_rows_done_.fetch_add(1u, std::memory_order_relaxed);
        } else if (!kPublishIntrabatchRowProgress) {
            // Concurrent process_batch workers: rows_done cache fights across threads; use atomic row count only.
            intrabatch_contrastive_rows_done_.fetch_add(1u, std::memory_order_relaxed);
        }
        if (kTiming && total_routes > 0) {
            const auto now = std::chrono::steady_clock::now();
            if (now - last_timing_hb >= pipeline_train_emit_period) {
                const int64_t partial_total_ms = elapsed_ms(timing_batch_t0);
                std::ostringstream partial_line;
                partial_line << "[TrainingTimingPartial] total_ms=" << partial_total_ms
                             << " collect_ms=" << timing_collect_ms
                             << " route_ms=" << timing_route_ms
                             << " ff_train_ms=" << timing_ff_ms
                             << " routes=" << total_routes
                             << " rows_done=" << (row_index_for_metrics + 1)
                             << "/" << batch_work.size()
                             << " collect_limit=" << collect_limit
                             << " top_k_cap=" << effective_top_k;
                if (use_parallel_rows) {
                    partial_line << " parallel_rows=1";
                }
                std::cerr << partial_line.str() << std::endl;
                std::cout << partial_line.str() << std::endl;
                last_timing_hb = now;
            }
        }
    };

    bool multi_row_slot_done = false;
    std::string fatal_multi_row;
    if (!batch_work.empty()) {
        qmini_training_breadcrumb("pipeline:process_batch:ff_multi_row_try");
        std::vector<RowTrainMerge> multi_merges;
        int64_t multi_ff_ms = 0;
        bool multi_invoke_ok = false;
        try {
            multi_invoke_ok = try_ff_multi_row_slot_batch_invoke_(
                batch_work, kTiming, effective_top_k, wave_pb, multi_merges, multi_ff_ms, fatal_multi_row);
        } catch (const std::exception& ex) {
            fatal_multi_row = std::string("SYCL multi-row ForwardForward exception: ") +
                              augment_ur_sycl_exception_text_for_log(ex);
        } catch (...) {
            fatal_multi_row = "SYCL multi-row ForwardForward: unknown exception";
        }
        if (multi_invoke_ok) {
            multi_row_slot_done = true;
            size_t slot_count = 0;
            for (const auto& mm : multi_merges) {
                slot_count += mm.route_expert_idx.size();
            }
            std::cout << "[TrainingPipeline] ForwardForward: multi-row SYCL batch ok rows=" << batch_work.size()
                      << " slots=" << slot_count << std::endl;
            std::cerr << "[TrainingPipeline] ForwardForward: multi-row SYCL batch ok rows=" << batch_work.size()
                      << " slots=" << slot_count << std::endl;
            for (size_t r = 0; r < batch_work.size(); ++r) {
                merge_row_into_batch(multi_merges[r], r, false);
            }
        } else if (!fatal_multi_row.empty()) {
            static std::atomic<unsigned> s_sycl_mrif_stderr_lines{0};
            const unsigned ln = s_sycl_mrif_stderr_lines.fetch_add(1);
            if (ln < 4u) {
                std::cerr << "[TrainingPipeline] ERROR: " << fatal_multi_row << std::endl;
            } else if (ln == 4u) {
                std::cerr << "[TrainingPipeline] ERROR: further SYCL multi-row ForwardForward stderr lines suppressed "
                             "(same failure mode; see messages above).\n";
            }
            {
                std::lock_guard<std::mutex> lock(metrics_mutex_);
                cached_metrics_.status_message = fatal_multi_row;
            }
            if (kPublishBatchInflight) {
                batch_inflight_active_.store(false);
                batch_inflight_done_.store(0);
                batch_inflight_target_.store(0);
            }
            if (kPublishIntrabatchRowProgress) {
                intrabatch_contrastive_rows_done_.store(0u, std::memory_order_relaxed);
            }
            stop_requested_ = true;
            set_state(PipelineState::FAILED);
            return 0;
        }
#if defined(USE_SYCL) && USE_SYCL
        if (!multi_row_slot_done && !batch_work.empty()) {
            const std::string msg =
                fatal_multi_row.empty()
                    ? std::string(
                          "SYCL multi-row ForwardForward did not complete — fix SYCL/kernel or adjust "
                          "training.gf3_ff_batched_weight_mib / ff_multi_row_slots_chunk / "
                          "gf3_ff_multislot_slots_chunk_max.")
                    : fatal_multi_row;
            std::cerr << "[TrainingPipeline] ERROR: " << msg << std::endl;
            {
                std::lock_guard<std::mutex> lock(metrics_mutex_);
                cached_metrics_.status_message = msg;
            }
            if (kPublishBatchInflight) {
                batch_inflight_active_.store(false);
                batch_inflight_done_.store(0);
                batch_inflight_target_.store(0);
            }
            if (kPublishIntrabatchRowProgress) {
                intrabatch_contrastive_rows_done_.store(0u, std::memory_order_relaxed);
            }
            stop_requested_ = true;
            set_state(PipelineState::FAILED);
            return 0;
        }
#endif
    }

#if !(defined(USE_SYCL) && USE_SYCL)
    if (!multi_row_slot_done && use_parallel_rows) {
#if defined(_OPENMP)
        qmini_training_breadcrumb("pipeline:process_batch:omp_train_rows");
        std::atomic<bool> fatal_batch{false};
        std::string fatal_msg_store;
        std::mutex fatal_store_mu;
        std::cout << "[TrainingPipeline] Contrastive rows: parallel over " << batch_work.size()
                  << " rows (training.parallel_contrastive_rows=true in TOML)" << std::endl;
#pragma omp parallel for schedule(dynamic, 1)
        for (long long r = 0; r < static_cast<long long>(batch_work.size()); ++r) {
            if (fatal_batch.load(std::memory_order_relaxed)) {
                continue;
            }
            const size_t row_index = static_cast<size_t>(r);
            RowTrainMerge local;
            std::string fatal;
            const int code = train_contrastive_row_impl(
                row_index,
                batch_work[row_index].positive,
                batch_work[row_index].prepared_negative,
                trace_phases,
                kTiming,
                goodness_log_level,
                effective_top_k,
                local,
                fatal);
            if (code == 2) {
                std::lock_guard<std::mutex> flk(fatal_store_mu);
                if (!fatal_batch.exchange(true)) {
                    fatal_msg_store = std::move(fatal);
                }
                continue;
            }
            if (code == 1) {
                const uint32_t inc = local.neg_missing_inc;
#pragma omp atomic
                batch_negatives_missing += inc;
                continue;
            }
#pragma omp critical(row_train_merge)
            merge_row_into_batch(local, row_index, true);
        }
        if (fatal_batch.load()) {
            std::cerr << "[TrainingPipeline] ERROR: " << fatal_msg_store << std::endl;
            {
                std::lock_guard<std::mutex> lock(metrics_mutex_);
                cached_metrics_.status_message = fatal_msg_store;
            }
            batch_inflight_active_.store(false);
            batch_inflight_done_.store(0);
            batch_inflight_target_.store(0);
            if (kPublishIntrabatchRowProgress) {
                intrabatch_contrastive_rows_done_.store(0u, std::memory_order_relaxed);
            }
            stop_requested_ = true;
            set_state(PipelineState::FAILED);
            return 0;
        }
#endif
    } else if (!multi_row_slot_done) {
        qmini_training_breadcrumb("pipeline:process_batch:serial_train_rows");
        for (size_t row_index = 0; row_index < batch_work.size(); ++row_index) {
            RowTrainMerge local;
            std::string fatal;
            const int code = train_contrastive_row_impl(
                row_index,
                batch_work[row_index].positive,
                batch_work[row_index].prepared_negative,
                trace_phases,
                kTiming,
                goodness_log_level,
                effective_top_k,
                local,
                fatal);
            if (code == 2) {
                std::cerr << "[TrainingPipeline] ERROR: " << fatal << std::endl;
                {
                    std::lock_guard<std::mutex> lock(metrics_mutex_);
                    cached_metrics_.status_message = fatal;
                }
                if (kPublishBatchInflight) {
                    batch_inflight_active_.store(false);
                    batch_inflight_done_.store(0);
                    batch_inflight_target_.store(0);
                }
                if (kPublishIntrabatchRowProgress) {
                    intrabatch_contrastive_rows_done_.store(0u, std::memory_order_relaxed);
                }
                stop_requested_ = true;
                set_state(PipelineState::FAILED);
                return 0;
            }
            if (code == 1) {
                batch_negatives_missing += local.neg_missing_inc;
                continue;
            }
            merge_row_into_batch(local, row_index, true);
        }
    }
#else
    if (!multi_row_slot_done && !batch_work.empty()) {
        std::cerr << "[TrainingPipeline] ERROR: USE_SYCL build requires multi-row ForwardForward for every non-empty "
                     "batch; per-row FF paths are not compiled in.\n";
        std::lock_guard<std::mutex> lock(metrics_mutex_);
        cached_metrics_.status_message =
            "SYCL throughput: multi-row FF mandatory (per-row path unavailable in this build)";
        stop_requested_ = true;
        set_state(PipelineState::FAILED);
        return 0;
    }
#endif

    // Update cached metrics (arithmetic batch averages over expert routes)
    if (total_routes > 0) {
        std::lock_guard<std::mutex> lock(metrics_mutex_);
        cached_metrics_.ff_positive_goodness = static_cast<uint32_t>(sum_pos_goodness / static_cast<uint64_t>(total_routes));
        cached_metrics_.ff_negative_goodness = static_cast<uint32_t>(sum_neg_goodness / static_cast<uint64_t>(total_routes));
        cached_metrics_.ff_goodness_delta = static_cast<int32_t>(sum_delta / static_cast<int64_t>(total_routes));
        cached_metrics_.ff_total_train_calls += total_routes;
        cached_metrics_.expert_utilization.resize(config_.moe_num_experts);
        cached_metrics_.expert_deltas = expert_deltas;
        
        // Per-expert route counts this batch (Betti / diagnostics expect integer counts, not tropical a-b).
        for (size_t i = 0; i < config_.moe_num_experts; ++i) {
            cached_metrics_.expert_utilization[i] = expert_counts[i];
        }
        cached_metrics_.used_tritpack5_input_route = batch_used_tritpack5_input_route;
        cached_metrics_.router_sycl_path_used = batch_router_sycl_path_used;
    }

    if (goodness_log_level >= 1u && total_routes > 0) {
        const uint32_t avg_pos = static_cast<uint32_t>(sum_pos_goodness / static_cast<uint64_t>(total_routes));
        const uint32_t avg_neg = static_cast<uint32_t>(sum_neg_goodness / static_cast<uint64_t>(total_routes));
        const int32_t avg_delta = static_cast<int32_t>(sum_delta / static_cast<int64_t>(total_routes));
        std::fprintf(stderr,
                     "[TrainingPipeline][Goodness] batch_summary contrastive_rows=%zu routes=%u "
                     "avg_pos_out=%u avg_neg_out=%u avg_delta=%d moe_output_dim=%zu\n",
                     batch_work.size(),
                     static_cast<unsigned>(total_routes),
                     static_cast<unsigned>(avg_pos),
                     static_cast<unsigned>(avg_neg),
                     static_cast<int>(avg_delta),
                     static_cast<size_t>(config_.moe_output_dim));
    }

    if (total_routes > 0 &&
        moe::moe_routing_sycl_desired(config_.moe_num_experts, config_.training_sycl_route_mode) &&
        !batch_router_sycl_path_used) {
        const char* fatal_gpu_route =
            "ROUTER_GPU_REQUIRED: symplectic routing must use SYCL for this config but no SYCL path was used for this batch";
        {
            std::lock_guard<std::mutex> lock(metrics_mutex_);
            cached_metrics_.status_message = fatal_gpu_route;
        }
        if (kPublishBatchInflight) {
            batch_inflight_active_.store(false);
            batch_inflight_done_.store(0);
            batch_inflight_target_.store(0);
        }
        if (kPublishIntrabatchRowProgress) {
            intrabatch_contrastive_rows_done_.store(0u, std::memory_order_relaxed);
        }
        std::cerr << "[TrainingPipeline] " << fatal_gpu_route << std::endl;
        stop_requested_ = true;
        set_state(PipelineState::FAILED);
        return 0;
    }

    // Return contrastive rows trained (one per batch_work entry), not per-expert route count.
    if (kPublishBatchInflight) {
        batch_inflight_active_.store(false);
        batch_inflight_done_.store(0);
        batch_inflight_target_.store(0);
    }
    if (kTiming) {
        const int64_t total_ms = elapsed_ms(timing_batch_t0);
        std::ostringstream timing_line;
        timing_line << "[TrainingTiming] total_ms=" << total_ms
                    << " collect_ms=" << timing_collect_ms
                    << " route_ms=" << timing_route_ms
                    << " ff_train_ms=" << timing_ff_ms
                    << " routes=" << total_routes
                    << " rows=" << batch_work.size()
                    << " collect_limit=" << collect_limit
                    << " top_k_cap=" << effective_top_k
                    << " neg_prepared=" << batch_negatives_prepared
                    << " neg_generated=" << batch_negatives_generated
                    << " neg_missing=" << batch_negatives_missing;
        std::cerr << timing_line.str() << std::endl;
        std::cout << timing_line.str() << std::endl;
    }
    const uint64_t evictions_end = expert_evictions_total_.load(std::memory_order_relaxed);
    const uint64_t cooldown_skips_end =
        expert_eviction_cooldown_skips_total_.load(std::memory_order_relaxed);
    const uint64_t evictions_this_batch = (evictions_end >= evictions_start) ? (evictions_end - evictions_start) : 0ull;
    const uint64_t cooldown_skips_this_batch =
        (cooldown_skips_end >= cooldown_skips_start) ? (cooldown_skips_end - cooldown_skips_start) : 0ull;
    const uint32_t pinned_now = pinned_expert_count();
    std::ostringstream residency_line;
    residency_line << "[TrainingPipeline][Residency] pinned_experts=" << pinned_now
                   << " cooldown_skipped_candidates=" << cooldown_skips_this_batch
                   << " evictions_per_batch=" << evictions_this_batch;
    std::cerr << residency_line.str() << std::endl;
    std::cout << residency_line.str() << std::endl;
    if (kPublishIntrabatchRowProgress) {
        intrabatch_contrastive_rows_done_.store(0u, std::memory_order_relaxed);
    }
    std::cerr << "[TrainingPipeline] process_batch RETURNING: " << batch_work.size() << std::endl;
    return batch_work.size();
}

/**
 * @brief Extract ternary vector and optional TritPack5 buffer for SYCL MoE routing (@c pack_batch_t5 / polynomial byte).
 */
TernaryRouteInput AutonomousTrainingPipeline::extract_ternary_route_input(const TrainingSample& sample) {
    TernaryRouteInput out;

    std::visit([&out, this](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;

        if constexpr (std::is_same_v<T, std::vector<ternary::Trit>>) {
            out.trits = arg;
            out.route_packed_t5.clear();
        } else if constexpr (std::is_same_v<T, std::vector<float>>) {
#if defined(USE_SYCL) && USE_SYCL
            if (pipeline_use_sycl_trit_quant(config_.moe_input_dim, config_.sycl_trit_quant_min_moe_dim)) {
                std::vector<uint8_t> packed = ::q_mini_wasm_v2::sycl_kernels::quantize_float_buffer_to_trits_sycl(
                    arg.data(), arg.size(), config_.moe_input_dim);
                out.route_packed_t5 = std::move(packed);
            } else
#endif
            {
                out.trits.reserve(arg.size());
                size_t count = 0;
                for (float val : arg) {
                    if (val > 0.33f) {
                        out.trits.push_back(ternary::Trit::POSITIVE);
                    } else if (val < -0.33f) {
                        out.trits.push_back(ternary::Trit::NEGATIVE);
                    } else {
                        out.trits.push_back(ternary::Trit::ZERO);
                    }
                    count++;
                    if (count >= static_cast<size_t>(config_.moe_input_dim)) {
                        break;
                    }
                }
                out.route_packed_t5.clear();
            }
        } else if constexpr (std::is_same_v<T, std::vector<int32_t>>) {
#if defined(USE_SYCL) && USE_SYCL
            if (pipeline_use_sycl_trit_quant(config_.moe_input_dim, config_.sycl_trit_quant_min_moe_dim)) {
                std::vector<uint8_t> packed = ::q_mini_wasm_v2::sycl_kernels::quantize_i32_buffer_to_trits_sycl(
                    arg.data(), arg.size(), config_.moe_input_dim);
                out.route_packed_t5 = std::move(packed);
            } else
#endif
            {
                out.trits.reserve(arg.size());
                for (int32_t v : arg) {
                    int im = static_cast<int>(v % 3);
                    if (im < 0) {
                        im += 3;
                    }
                    const int8_t mapped = static_cast<int8_t>(im - 1);
                    out.trits.push_back(static_cast<ternary::Trit>(mapped));
                    if (out.trits.size() >= static_cast<size_t>(config_.moe_input_dim)) {
                        break;
                    }
                }
                out.route_packed_t5.clear();
            }
        } else if constexpr (std::is_same_v<T, std::vector<int8_t>>) {
            out.trits.reserve(arg.size());
            for (int8_t v : arg) {
                out.trits.push_back(static_cast<ternary::Trit>(v));
                if (out.trits.size() >= static_cast<size_t>(config_.moe_input_dim)) {
                    break;
                }
            }
            out.route_packed_t5.clear();
        } else if constexpr (std::is_same_v<T, std::vector<std::vector<int32_t>>>) {
#if defined(USE_SYCL) && USE_SYCL
            if (pipeline_use_sycl_trit_quant(config_.moe_input_dim, config_.sycl_trit_quant_min_moe_dim)) {
                std::vector<int32_t> scratch;
                collect_i32_rowmajor_upto(arg, config_.moe_input_dim, scratch);
                std::vector<uint8_t> packed = ::q_mini_wasm_v2::sycl_kernels::quantize_i32_buffer_to_trits_sycl(
                    scratch.empty() ? nullptr : scratch.data(), scratch.size(), config_.moe_input_dim);
                out.route_packed_t5 = std::move(packed);
            } else
#endif
            {
                for (const auto& row : arg) {
                    for (int32_t v : row) {
                        int im = static_cast<int>(v % 3);
                        if (im < 0) {
                            im += 3;
                        }
                        out.trits.push_back(static_cast<ternary::Trit>(static_cast<int8_t>(im - 1)));
                        if (out.trits.size() >= static_cast<size_t>(config_.moe_input_dim)) {
                            break;
                        }
                    }
                    if (out.trits.size() >= static_cast<size_t>(config_.moe_input_dim)) {
                        break;
                    }
                }
                out.route_packed_t5.clear();
            }
        } else if constexpr (std::is_same_v<T, std::vector<std::vector<float>>>) {
#if defined(USE_SYCL) && USE_SYCL
            if (pipeline_use_sycl_trit_quant(config_.moe_input_dim, config_.sycl_trit_quant_min_moe_dim)) {
                std::vector<float> scratch;
                collect_floats_rowmajor_upto(arg, config_.moe_input_dim, scratch);
                std::vector<uint8_t> packed = ::q_mini_wasm_v2::sycl_kernels::quantize_float_buffer_to_trits_sycl(
                    scratch.empty() ? nullptr : scratch.data(), scratch.size(), config_.moe_input_dim);
                out.route_packed_t5 = std::move(packed);
            } else
#endif
            {
                for (const auto& row : arg) {
                    for (float val : row) {
                        if (val > 0.33f) {
                            out.trits.push_back(ternary::Trit::POSITIVE);
                        } else if (val < -0.33f) {
                            out.trits.push_back(ternary::Trit::NEGATIVE);
                        } else {
                            out.trits.push_back(ternary::Trit::ZERO);
                        }
                        if (out.trits.size() >= static_cast<size_t>(config_.moe_input_dim)) {
                            break;
                        }
                    }
                    if (out.trits.size() >= static_cast<size_t>(config_.moe_input_dim)) {
                        break;
                    }
                }
                out.route_packed_t5.clear();
            }
        } else if constexpr (std::is_same_v<T, std::string>) {
            if (arg.empty()) {
                return;
            }
#if defined(USE_SYCL) && USE_SYCL
            if (pipeline_use_sycl_trit_quant(config_.moe_input_dim, config_.sycl_trit_quant_min_moe_dim)) {
                std::vector<uint8_t> packed = ::q_mini_wasm_v2::sycl_kernels::quantize_string_bytes_to_trits_sycl(
                    arg.data(), arg.size(), config_.moe_input_dim);
                out.route_packed_t5 = std::move(packed);
            } else
#endif
            {
                out.trits.reserve(config_.moe_input_dim);
                for (size_t i = 0; i < config_.moe_input_dim; ++i) {
                    const size_t char_idx = i % arg.size();
                    const char c = arg[char_idx];
                    const int8_t val = static_cast<int8_t>((static_cast<int>(c) % 3) - 1);
                    out.trits.push_back(static_cast<ternary::Trit>(val));
                }
                out.route_packed_t5.clear();
            }
        }
    }, sample.data);

    if (config_.moe_input_dim == 0) {
        return {};
    }
    if (!out.trits.empty()) {
        if (out.trits.size() < config_.moe_input_dim) {
            out.trits.resize(config_.moe_input_dim, ternary::Trit::ZERO);
        } else if (out.trits.size() > config_.moe_input_dim) {
            out.trits.resize(config_.moe_input_dim);
        }
    }
    // Canonical route wire length: pad with zero bytes or repack from trits (SYCL quant may already match).
    const size_t need_route_bytes = (config_.moe_input_dim + 4u) / 5u;
    if (out.route_packed_t5.size() < need_route_bytes) {
        if (!out.trits.empty()) {
            std::vector<int8_t> lanes(config_.moe_input_dim, 0);
            for (size_t i = 0; i < out.trits.size() && i < config_.moe_input_dim; ++i) {
                lanes[i] = static_cast<int8_t>(out.trits[i]);
            }
#if defined(USE_SYCL) && USE_SYCL
            out.route_packed_t5 =
                ::q_mini_wasm_v2::sycl_kernels::pack_int8_lanes_to_tritpack5_sycl(lanes, config_.moe_input_dim);
#else
            q::ternary::pack_batch_t5(lanes, out.route_packed_t5);
#endif
        } else {
            out.route_packed_t5.resize(need_route_bytes, static_cast<uint8_t>(0));
        }
    } else if (out.route_packed_t5.size() > need_route_bytes) {
        out.route_packed_t5.resize(need_route_bytes);
    }

    return out;
}

/**
 * @brief Generate negative sample by corrupting positive sample
 */
std::vector<ternary::Trit> AutonomousTrainingPipeline::generate_negative_sample(
    const std::vector<ternary::Trit>& positive
) {
    if (positive.empty()) {
        return {};
    }
#if !defined(USE_SYCL) || !USE_SYCL
    throw std::runtime_error(
        "[TrainingPipeline] Contrastive negatives require USE_SYCL=1 (gf3_generate_negative_sycl); no host substitute.");
#else
    static std::atomic<uint32_t> neg_seed{1u};
    std::vector<int8_t> pos_lanes(positive.size(), 0);
    for (size_t i = 0; i < positive.size(); ++i) {
        pos_lanes[i] = static_cast<int8_t>(positive[i]);
    }
    std::vector<uint8_t> pos_packed =
        ::q_mini_wasm_v2::sycl_kernels::pack_int8_lanes_to_tritpack5_sycl(pos_lanes, positive.size());
    const size_t n_trits = positive.size();
    const size_t n_pack = (n_trits + 4u) / 5u;

    std::vector<uint8_t> neg_packed;
    const uint32_t seed = neg_seed.fetch_add(1u, std::memory_order_relaxed);
    if (::q_mini_wasm_v2::sycl_kernels::gf3_generate_negative_sycl(pos_packed, n_trits, seed, neg_packed) &&
        neg_packed.size() == n_pack) {
        std::vector<ternary::Trit> negative;
        negative.reserve(n_trits);
        for (size_t i = 0; i < n_trits; ++i) {
            negative.push_back(static_cast<ternary::Trit>(q::ternary::read_trit_t5_at(neg_packed.data(), i)));
        }
        return negative;
    }
    throw std::runtime_error(
        "[TrainingPipeline] gf3_generate_negative_sycl failed; GPU-mandatory contrastive negatives.");
#endif
}

bool AutonomousTrainingPipeline::evaluate_topology() {
    qmini_training_breadcrumb("pipeline:topology:betti");
    set_state(PipelineState::EVALUATING_TOPOLOGY);
    
    // Build simplicial complex from current graph state
    auto complex = build_simplicial_complex();
    
    // Load into BettiExtractor
    betti_extractor_->load_complex(complex);
    
    // Compute Betti numbers
    auto betti = betti_extractor_->compute_betti();
    
    // Store in cached metrics
    {
        std::lock_guard<std::mutex> lock(metrics_mutex_);
        cached_metrics_.betti_beta_0 = betti.beta_0;
        cached_metrics_.betti_beta_1 = betti.beta_1;
        cached_metrics_.betti_beta_2 = betti.beta_2;
        cached_metrics_.euler_characteristic = betti.euler_characteristic();
        cached_metrics_.last_betti_eval_batch = current_batch_.load();
    }
    
    // Apply guidance if enabled
    if (config_.enable_betti_guidance && betti.beta_1 > config_.betti_guidance_threshold) {
        adjust_topology_based_on_betti(betti);
    }
    
    set_state(PipelineState::TRAINING);
    return true;
}

qgnn::BettiExtractor::SimplicialComplex AutonomousTrainingPipeline::build_simplicial_complex() {
    qgnn::BettiExtractor::SimplicialComplex complex;
    
    size_t nodes = graph_tableau_->num_qutrits();
    
    // Add vertices
    complex.vertices.reserve(nodes);
    for (size_t i = 0; i < nodes; ++i) {
        complex.vertices.push_back(static_cast<uint32_t>(i));
    }
    
    // Extract edges from graph_tableau state using stabilizer entanglement
    auto edges = graph_tableau_->get_edges();
    
    // Add extracted edges to simplicial complex
    // Encode vertex pairs (i, j) into one ternary::TritPack5 byte (same polynomial layout as q::ternary::pack_batch_t5)
    for (const auto& [i, j] : edges) {
        if (i < nodes && j < nodes && i < 27 && j < 9) {  // Limits: 3^3=27, 3^2=9
            ternary::TritPack5 pack{};
            // Encode i in first 3 trits (base-3, values 0-26)
            uint32_t ii = static_cast<uint32_t>(i);
            pack.set(0, static_cast<ternary::Trit>(ii % 3 - 1)); ii /= 3;
            pack.set(1, static_cast<ternary::Trit>(ii % 3 - 1)); ii /= 3;
            pack.set(2, static_cast<ternary::Trit>(ii % 3 - 1));
            // Encode j in next 2 trits (base-3, values 0-8)
            uint32_t jj = static_cast<uint32_t>(j);
            pack.set(3, static_cast<ternary::Trit>(jj % 3 - 1)); jj /= 3;
            pack.set(4, static_cast<ternary::Trit>(jj % 3 - 1));
            complex.edges.push_back(pack);
        }
    }

    // If no edges found, create initial edges based on config
    if (complex.edges.empty()) {
        size_t num_edges = std::min(config_.graph_initial_edges, nodes * (nodes - 1) / 2);
        for (size_t e = 0, i = 0; e < num_edges && i < nodes; ++i) {
            for (size_t j = i + 1; j < nodes && e < num_edges; ++j, ++e) {
                // Encode edge (i, j) into TritPack5
                if (i < 27 && j < 9) {
                    ternary::TritPack5 pack{};
                    uint32_t ii = static_cast<uint32_t>(i);
                    pack.set(0, static_cast<ternary::Trit>(ii % 3 - 1)); ii /= 3;
                    pack.set(1, static_cast<ternary::Trit>(ii % 3 - 1)); ii /= 3;
                    pack.set(2, static_cast<ternary::Trit>(ii % 3 - 1));
                    uint32_t jj = static_cast<uint32_t>(j);
                    pack.set(3, static_cast<ternary::Trit>(jj % 3 - 1)); jj /= 3;
                    pack.set(4, static_cast<ternary::Trit>(jj % 3 - 1));
                    complex.edges.push_back(pack);
                }
                // Also add to graph_tableau
                graph_tableau_->add_edge(i, j);
            }
        }
    }
    
    return complex;
}

void AutonomousTrainingPipeline::adjust_topology_based_on_betti(
    const qgnn::BettiExtractor::BettiNumbers& betti
) {
    set_state(PipelineState::OPTIMIZING_GRAPH);
    
    // High β₁ indicates many cycles - simplify topology by removing edges
    if (betti.beta_1 > config_.betti_guidance_threshold * 2) {
        // Strategy: Remove edges that participate in most cycles
        // For now: remove high-degree nodes' excess edges
        
        size_t nodes = graph_tableau_->num_qutrits();
        auto edges = graph_tableau_->get_edges();
        
        // Count degree of each node
        std::vector<size_t> node_degrees(nodes, 0);
        for (const auto& [i, j] : edges) {
            if (i < nodes) node_degrees[i]++;
            if (j < nodes) node_degrees[j]++;
        }
        
        // Calculate target edges for a tree-like structure
        // A tree has n-1 edges, so we want to reduce toward that
        size_t target_edges = std::min(nodes - 1 + config_.betti_guidance_threshold, edges.size());
        size_t edges_to_remove = edges.size() > target_edges ? edges.size() - target_edges : 0;
        
        // Remove edges from high-degree nodes first
        std::vector<std::pair<size_t, size_t>> edges_to_remove_list;
        for (const auto& [i, j] : edges) {
            // Score edges by sum of node degrees (higher = more likely to be in cycles)
            size_t score = node_degrees[i] + node_degrees[j];
            edges_to_remove_list.push_back({score, edges_to_remove_list.size()});
        }
        
        // Sort by score descending (highest degree nodes first)
        std::sort(edges_to_remove_list.rbegin(), edges_to_remove_list.rend());
        
        // Remove highest-scoring edges
        for (size_t r = 0; r < edges_to_remove && r < edges_to_remove_list.size(); ++r) {
            size_t edge_idx = edges_to_remove_list[r].second;
            if (edge_idx < edges.size()) {
                const auto& [i, j] = edges[edge_idx];
                graph_tableau_->remove_edge(i, j);
            }
        }
    }
    
    // Low β₀ indicates poor connectivity - add edges
    if (betti.beta_0 > 1) {
        // Add edges to connect components
        size_t nodes = graph_tableau_->num_qutrits();
        for (size_t i = 0; i < betti.beta_0 - 1; ++i) {
            // Add edges between components
            if (i + 1 < nodes) {
                graph_tableau_->add_edge(i, i + 1);
            }
        }
    }
    
    set_state(PipelineState::TRAINING);
}

void AutonomousTrainingPipeline::trigger_topology_evaluation() {
    if (state_.load() == PipelineState::TRAINING || 
        state_.load() == PipelineState::PAUSED) {
        evaluate_topology();
    }
}

void AutonomousTrainingPipeline::apply_betti_guidance() {
    if (state_.load() == PipelineState::TRAINING || 
        state_.load() == PipelineState::PAUSED) {
        evaluate_topology();
    }
}

PipelineMetrics AutonomousTrainingPipeline::get_metrics() const {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    PipelineMetrics metrics = cached_metrics_;
    
    // Add current state
    metrics.is_running = (state_.load() == PipelineState::TRAINING);
    metrics.current_epoch = current_epoch_.load();
    metrics.current_batch = current_batch_.load();
    
    // Calculate progress as integer percentage (0-10000 for 0.00% - 100.00% precision)
    if (config_.num_epochs > 0) {
        uint32_t epoch_progress = (static_cast<uint32_t>(current_epoch_) * 10000) / config_.num_epochs;
        metrics.training_progress = epoch_progress;  // Now in basis points (0-10000)
    }
    
    // Add status message with detailed DataSynthesizer health
    std::string status = state_to_string(state_.load());
    metrics.train_collect_limit_effective = static_cast<uint32_t>(adaptive_collect_limit(config_));
    metrics.route_topk_effective = static_cast<uint32_t>(adaptive_route_topk_cap(config_));
    
    // Get DataSynthesizer stats
    if (synthesizer_) {
        auto ds_stats = synthesizer_->get_stats();
        metrics.ds_total_acquired = ds_stats.total_acquired;
        metrics.ds_total_perturbed = ds_stats.total_perturbed;
        metrics.ds_api_failures = ds_stats.api_failures;
        metrics.ds_queue_depth = ds_stats.queue_depth;
        metrics.ds_raw_queue_depth = ds_stats.raw_queue_depth;
        metrics.ds_raw_queue_max = ds_stats.raw_queue_max;
        metrics.ds_train_queue_max = ds_stats.train_queue_max;
        metrics.ds_blocked_raw_pushes = ds_stats.blocked_raw_pushes;
        metrics.ds_blocked_train_pushes = ds_stats.blocked_train_pushes;
        metrics.ds_blocked_wait_ms = ds_stats.blocked_wait_ms;
        metrics.ds_dropped_payloads = ds_stats.dropped_payloads;
        metrics.ds_dropped_payload_string_view = ds_stats.dropped_payload_string_view;
        metrics.ds_dropped_payload_other = ds_stats.dropped_payload_other;
        metrics.ds_acquisition_queue_depth = ds_stats.acquisition_queue_depth;
        metrics.ds_acquisition_queue_max = ds_stats.acquisition_queue_max;
        metrics.ds_acquisition_blocked_pushes = ds_stats.acquisition_blocked_pushes;
        metrics.ds_acquisition_blocked_wait_ms = ds_stats.acquisition_blocked_wait_ms;
        metrics.ds_acquisition_dropped_too_short = ds_stats.acquisition_dropped_too_short;
        metrics.ds_topic_frontier_size = ds_stats.topic_frontier_size;
        metrics.ds_topic_frontier_max = ds_stats.topic_frontier_max;
        metrics.ds_topic_frontier_evictions = ds_stats.topic_frontier_evictions;
        metrics.prefill_current_samples = ds_stats.raw_queue_depth + (ds_stats.queue_depth / 2);
        metrics.prefill_target_samples = config_.prefill_target_samples;
        metrics.prefill_timeout_ms = config_.prefill_timeout_ms;
        {
            const size_t tgt = std::max<size_t>(size_t{1}, config_.prefill_target_samples);
            metrics.prefill_reached = (metrics.prefill_current_samples >= tgt);
        }
        
        // Add diagnostic info to status message
        if (ds_stats.queue_depth == 0 && state_.load() == PipelineState::TRAINING) {
            status += " | WAITING: DataSynthesizer queue empty";
            if (ds_stats.api_failures > 0) {
                status += " (API failures: " + std::to_string(ds_stats.api_failures) + ")";
            }
            if (ds_stats.blocked_wait_ms > 0) {
                status += " blocked_ms=" + std::to_string(ds_stats.blocked_wait_ms);
            }
        } else if (ds_stats.total_acquired == 0 && state_.load() == PipelineState::TRAINING) {
            status += " | WAITING: No data acquired from APIs yet";
        } else {
            status += " | queue_depth=" + std::to_string(ds_stats.queue_depth) +
                      " raw=" + std::to_string(ds_stats.raw_queue_depth) +
                      " acq_q=" + std::to_string(ds_stats.acquisition_queue_depth) +
                      " acquired=" + std::to_string(ds_stats.total_acquired) +
                      " blocked=" + std::to_string(ds_stats.blocked_raw_pushes + ds_stats.blocked_train_pushes + ds_stats.acquisition_blocked_pushes);
            if (ds_stats.acquisition_dropped_too_short > 0) {
                status += " acq_drop_short=" + std::to_string(ds_stats.acquisition_dropped_too_short);
            }
        }
        if (state_.load() == PipelineState::ACQUIRING_DATA && config_.enable_prefill_ring_buffer) {
            status += " | PREFILL " + std::to_string(metrics.prefill_current_samples) + "/" +
                      std::to_string(metrics.prefill_target_samples);
        }
    }
    if (batch_inflight_active_.load()) {
        const uint32_t done = batch_inflight_done_.load();
        const uint32_t target = batch_inflight_target_.load();
        if (target > 0) {
            status += " | batch_inflight " + std::to_string(done) + "/" + std::to_string(target);
        }
    }
    size_t resident_count = 0;
    size_t spilled_count = 0;
    size_t resident_cap = 0;
    {
        std::lock_guard<std::mutex> lk(experts_mutex_);
        resident_count = expert_resident_count_;
        spilled_count = evicted_expert_weights_.size();
        resident_cap = lazy_resident_expert_cap();
    }
    metrics.experts_resident = static_cast<uint32_t>(
        std::min<size_t>(resident_count, static_cast<size_t>(std::numeric_limits<uint32_t>::max())));
    metrics.experts_spilled = static_cast<uint32_t>(
        std::min<size_t>(spilled_count, static_cast<size_t>(std::numeric_limits<uint32_t>::max())));
    metrics.experts_resident_cap = static_cast<uint32_t>(
        std::min<size_t>(resident_cap, static_cast<size_t>(std::numeric_limits<uint32_t>::max())));

    status += " | collect_limit=" + std::to_string(adaptive_collect_limit(config_)) +
              " top_k=" + std::to_string(adaptive_route_topk_cap(config_)) +
              " resident_experts=" + std::to_string(metrics.experts_resident) +
              " spilled_experts=" + std::to_string(metrics.experts_spilled) +
              " resident_cap=" + std::to_string(metrics.experts_resident_cap) +
              " sycl_route_mode=" + (config_.training_sycl_route_mode == moe::SyclRouteMode::On ? "on" : "auto") +
              " live_parallel_batches=" + std::to_string(metrics.live_parallel_batches) + "/" +
              std::to_string(metrics.parallel_batches_ceiling);
    
    metrics.status_message = status;
    metrics.gf3_hebbian_weight_cell_updates = moe::gf3_hebbian_weight_cell_updates_total();
    // Epoch totals advance in training_loop when process_batch returns; mid-batch UI uses intrabatch rows
    // and/or train_batch_rows_done (SYCL multi-row merge uses increment_intrabatch_progress=false).
    // With parallel process_batch workers, intrabatch_contrastive_rows_done_ is the safe row counter;
    // train_batch_rows_done races when each worker resets after collect.
    // Do not gate on live_parallel_batches for row counters (workers may reset batch metrics independently).
    {
        uint64_t intra =
            static_cast<uint64_t>(intrabatch_contrastive_rows_done_.load(std::memory_order_relaxed));
        if (intra == 0u) {
            intra = static_cast<uint64_t>(metrics.train_batch_rows_done);
        }
        metrics.samples_processed = samples_processed_.load(std::memory_order_relaxed) + intra;
        metrics.samples_processed_total = samples_processed_total_.load(std::memory_order_relaxed) + intra;
    }
    metrics.live_parallel_batches = effective_parallel_batches();
    metrics.parallel_batches_ceiling = parallel_batches_ceiling_;

    return metrics;
}

void AutonomousTrainingPipeline::update_metrics() {
    // Update internal metrics cache
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    
    // Pipeline state
    cached_metrics_.current_epoch = current_epoch_.load();
    cached_metrics_.current_batch = current_batch_.load();
    cached_metrics_.live_parallel_batches = effective_parallel_batches();
    cached_metrics_.parallel_batches_ceiling = parallel_batches_ceiling_;
    cached_metrics_.is_running = (state_ == PipelineState::TRAINING);

    // Match get_metrics(): committed rows in atomics + mid-batch progress.
    {
        uint64_t intra =
            static_cast<uint64_t>(intrabatch_contrastive_rows_done_.load(std::memory_order_relaxed));
        if (intra == 0u) {
            intra = static_cast<uint64_t>(cached_metrics_.train_batch_rows_done);
        }
        cached_metrics_.samples_processed = samples_processed_.load(std::memory_order_relaxed) + intra;
        cached_metrics_.samples_processed_total = samples_processed_total_.load(std::memory_order_relaxed) + intra;
    }
    
    // Graph state
    if (graph_tableau_) {
        cached_metrics_.graph_nodes = graph_tableau_->num_qutrits();
        cached_metrics_.graph_topology = "graph_tableau";
    }
    
    // Continuous mode tracking
    cached_metrics_.loop_count = loop_count_.load();
}

void AutonomousTrainingPipeline::emit_metrics() {
    if (metrics_callback_) {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        auto metrics = get_metrics();
        metrics_callback_(metrics);
    }
}

void AutonomousTrainingPipeline::checkpoint_if_needed() {
    if (!config_.enable_checkpoints) {
        return;
    }

    ensure_checkpoint_writer_started();

    {
        std::lock_guard<std::mutex> lk(checkpoint_queue_mutex_);
        if (!checkpoint_queue_.empty()) {
            // Keep training hot: avoid serializing another large checkpoint blob while
            // previous async writes are still draining to disk.
            return;
        }
    }

    set_state(PipelineState::CHECKPOINTING);
    const std::string checkpoint_dir = checkpoint_output_dir(config_);
    std::error_code ec;
    std::filesystem::create_directories(checkpoint_dir, ec);
    if (ec) {
        std::cerr << "[TrainingPipeline] ERROR: checkpoint mkdir failed: " << checkpoint_dir
                  << " ec=" << ec.message() << std::endl;
    }
    std::string checkpoint_path = (std::filesystem::path(checkpoint_dir) /
                                   ("checkpoint_epoch_" + std::to_string(current_epoch_) + "_batch_" +
                                    std::to_string(current_batch_) + ".qmini"))
                                      .string();

    enqueue_checkpoint_job(std::move(checkpoint_path));

    if (state_.load() != PipelineState::FAILED && state_.load() != PipelineState::STOPPING) {
        set_state(PipelineState::TRAINING);
    }
}

void AutonomousTrainingPipeline::apply_run_overrides(const std::string& data_path, uint32_t num_epochs) {
    std::lock_guard<std::mutex> lock(config_mutex_);
    config_.data_path = data_path;
    config_.num_epochs = static_cast<size_t>(num_epochs);
}

bool AutonomousTrainingPipeline::serialize_checkpoint_blob(std::ostream& os) {
    std::scoped_lock lock(config_mutex_, experts_mutex_);

    os.write(kFileMagicV3, 8);
    const uint32_t fmt = 17; // v17: removed obsolete allow_cpu_negative_fallback byte from pipeline blob
    wr_u32(os, fmt);
    write_pipeline_config(os, config_);

    const uint64_t epoch = current_epoch_.load();
    const uint64_t batch = current_batch_.load();
    const uint64_t samples_total = samples_processed_total_.load();
    wr_u64(os, epoch);
    wr_u64(os, batch);
    wr_u64(os, samples_total);

    const uint64_t graph_nodes = graph_tableau_ ? static_cast<uint64_t>(graph_tableau_->num_qutrits()) : 0ull;
    wr_u64(os, graph_nodes);

    const uint64_t num_slots = static_cast<uint64_t>(experts_.size());
    wr_u64(os, num_slots);

    for (size_t i = 0; i < experts_.size(); ++i) {
        const bool has_spill = evicted_expert_weights_.find(i) != evicted_expert_weights_.end();
        const uint8_t present = (experts_[i] || has_spill) ? uint8_t{1} : uint8_t{0};
        wr_u8(os, present);
        if (!present) {
            continue;
        }
        if (experts_[i]) {
            experts_[i]->SerializeWeights(os);
            continue;
        }
        auto tmp = std::make_unique<moe::GF3MultiLayerExpert>(expert_template_);
        std::string spill_blob;
        if (!read_spill_payload(evicted_expert_weights_[i], spill_blob)) {
            return false;
        }
        std::istringstream iss(spill_blob, std::ios::binary);
        if (!tmp->DeserializeWeights(iss)) {
            return false;
        }
        tmp->SerializeWeights(os);
    }

    if (router_) {
        router_->SerializeRouterState(os);
    }

    return static_cast<bool>(os);
}

bool AutonomousTrainingPipeline::export_model(const std::string& path) {
    const PipelineState st0 = state_.load();
    if (st0 == PipelineState::TRAINING || st0 == PipelineState::ACQUIRING_DATA) {
        return false;
    }

    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    if (!serialize_checkpoint_blob(file)) {
        return false;
    }
    return static_cast<bool>(file);
}

void AutonomousTrainingPipeline::ensure_checkpoint_writer_started() {
    std::call_once(checkpoint_writer_once_, [this]() {
        checkpoint_writer_stop_.store(false);
        checkpoint_writer_thread_ = std::thread(&AutonomousTrainingPipeline::checkpoint_writer_loop, this);
    });
}

void AutonomousTrainingPipeline::enqueue_checkpoint_job(std::string path) {
    std::unique_lock<std::mutex> lk(checkpoint_queue_mutex_);
    checkpoint_queue_slots_cv_.wait(lk, [this] {
        return checkpoint_queue_.size() < config_.checkpoint_async_queue_max || checkpoint_writer_stop_.load();
    });
    if (checkpoint_writer_stop_.load()) {
        return;
    }
    checkpoint_queue_.emplace_back(std::move(path));
    lk.unlock();
    checkpoint_queue_cv_.notify_one();
}

void AutonomousTrainingPipeline::checkpoint_writer_loop() {
    while (true) {
        std::unique_lock<std::mutex> lk(checkpoint_queue_mutex_);
        checkpoint_queue_cv_.wait(lk, [this] {
            return checkpoint_writer_stop_.load() || !checkpoint_queue_.empty();
        });
        if (checkpoint_writer_stop_.load() && checkpoint_queue_.empty()) {
            return;
        }
        if (checkpoint_queue_.empty()) {
            continue;
        }
        std::string checkpoint_path = std::move(checkpoint_queue_.front());
        checkpoint_queue_.pop_front();
        lk.unlock();
        checkpoint_queue_slots_cv_.notify_all();

        std::ostringstream oss;
        if (!serialize_checkpoint_blob(oss)) {
            std::cerr << "[TrainingPipeline] ERROR: checkpoint serialization failed (async): " << checkpoint_path << std::endl;
            continue;
        }
        std::string payload = oss.str();

        std::ofstream file(checkpoint_path, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "[TrainingPipeline] ERROR: checkpoint open failed (async): " << checkpoint_path << std::endl;
            continue;
        }
        file.write(payload.data(), static_cast<std::streamsize>(payload.size()));
        if (file && static_cast<bool>(file.flush())) {
            std::cout << "[TrainingPipeline] Checkpoint written (async): " << checkpoint_path << " bytes=" << payload.size()
                      << std::endl;
        } else {
            std::cerr << "[TrainingPipeline] ERROR: checkpoint write failed (async): " << checkpoint_path << std::endl;
        }
    }
}

void AutonomousTrainingPipeline::shutdown_checkpoint_writer() {
    if (!checkpoint_writer_thread_.joinable()) {
        return;
    }
    {
        std::lock_guard<std::mutex> lk(checkpoint_queue_mutex_);
        checkpoint_writer_stop_.store(true);
    }
    checkpoint_queue_cv_.notify_all();
    checkpoint_queue_slots_cv_.notify_all();
    checkpoint_writer_thread_.join();
    checkpoint_writer_stop_.store(false);
}

bool AutonomousTrainingPipeline::import_model(const std::string& path, const PipelineConfig* merge_runtime_from) {
    std::scoped_lock lock(config_mutex_, experts_mutex_);

    const PipelineState st = state_.load();
    if (st == PipelineState::TRAINING || st == PipelineState::ACQUIRING_DATA) {
        return false;
    }

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    char magic[8] = {};
    file.read(magic, 8);
    if (!file || std::memcmp(magic, kFileMagicV3, 8) != 0) {
        return false;
    }

    uint32_t fmt = 0;
    if (!rd_u32(file, fmt) ||
        (fmt != 3 && fmt != 4 && fmt != 5 && fmt != 6 && fmt != 7 && fmt != 8 && fmt != 9 && fmt != 10 &&
         fmt != 11 && fmt != 12 && fmt != 13 && fmt != 14 && fmt != 15 && fmt != 16)) {
        return false;
    }

    PipelineConfig imported{};
    if (!read_pipeline_config(file, imported, fmt)) {
        return false;
    }

    uint64_t epoch = 0;
    uint64_t batch = 0;
    uint64_t samples_total = 0;
    uint64_t graph_nodes = 0;
    uint64_t num_slots = 0;
    if (!rd_u64(file, epoch) || !rd_u64(file, batch) || !rd_u64(file, samples_total) ||
        !rd_u64(file, graph_nodes) || !rd_u64(file, num_slots)) {
        return false;
    }

    shutdown_components();
    config_ = imported;
    if (merge_runtime_from) {
        merge_session_runtime_throughput(config_, *merge_runtime_from);
        std::cout << "[TrainingPipeline] import_model: merged live session throughput over checkpoint "
                      "(ff_multi_row_slots_chunk="
                   << config_.ff_multi_row_slots_chunk << " training_micro_batch_cap=" << config_.training_micro_batch_cap
                   << " gf3_ff_batched_weight_mib=" << config_.gf3_ff_batched_weight_mib << ")\n";
    }

    if (!initialize_components()) {
        set_state(PipelineState::FAILED);
        return false;
    }

    if (num_slots != static_cast<uint64_t>(experts_.size())) {
        set_state(PipelineState::FAILED);
        return false;
    }

    for (size_t i = 0; i < experts_.size(); ++i) {
        uint8_t present = 0;
        if (!rd_u8(file, present)) {
            set_state(PipelineState::FAILED);
            return false;
        }
        if (!present) {
            if (!config_.lazy_moe_experts) {
                set_state(PipelineState::FAILED);
                return false;
            }
            experts_[i].reset();
            continue;
        }
        if (!experts_[i]) {
            experts_[i] = std::make_unique<moe::GF3MultiLayerExpert>(expert_template_);
        }
        if (!experts_[i]->DeserializeWeights(file)) {
            set_state(PipelineState::FAILED);
            return false;
        }
    }

    if (fmt >= 4) {
        if (!router_ || !router_->DeserializeRouterState(file, fmt)) {
            set_state(PipelineState::FAILED);
            return false;
        }
    }

    current_epoch_.store(epoch);
    current_batch_.store(batch);
    samples_processed_total_.store(samples_total);
    samples_processed_.store(0);
    evicted_expert_weights_.clear();
    expert_touch_generation_.assign(experts_.size(), 0);
    expert_active_pin_counts_.assign(experts_.size(), 0u);
    expert_touch_clock_ = 1;
    expert_resident_count_ = 0;
    for (size_t i = 0; i < experts_.size(); ++i) {
        if (experts_[i]) {
            ++expert_resident_count_;
            touch_expert_locked(i);
        }
    }

    set_state(PipelineState::READY);
    return true;
}

bool AutonomousTrainingPipeline::update_config(const PipelineConfig& config) {
    PipelineState current = state_.load();
    if (current != PipelineState::IDLE && current != PipelineState::PAUSED) {
        return false;  // Can only update when idle or paused
    }
    
    std::lock_guard<std::mutex> lock(config_mutex_);
    config_ = config;
    parallel_batches_ceiling_ = std::max<size_t>(size_t{1}, config_.training_parallel_batches);
    live_parallel_batches_.store(
        std::max(size_t{1}, std::min(live_parallel_batches_.load(std::memory_order_relaxed), parallel_batches_ceiling_)),
        std::memory_order_relaxed);
    return true;
}

void AutonomousTrainingPipeline::set_pending_live_parallel_start(size_t pb) noexcept {
    pending_live_parallel_start_.store(pb, std::memory_order_relaxed);
}

bool AutonomousTrainingPipeline::set_live_parallel_batches(size_t pb) noexcept {
    const size_t c = parallel_batches_ceiling_;
    const size_t v = std::max(size_t{1}, std::min(pb, c));
    (void)live_parallel_batches_.exchange(v, std::memory_order_relaxed);
    // Intentionally quiet here: cmd/qminiwasm/training_live_scale.go already logs throttled transitions.
    return true;
}

size_t AutonomousTrainingPipeline::effective_parallel_batches() const noexcept {
    const size_t live =
        std::max<size_t>(size_t{1}, live_parallel_batches_.load(std::memory_order_relaxed));
    const size_t cap = config_.training_parallel_batches_runtime_cap;
    if (cap == 0) {
        return live;
    }
    return std::max(size_t{1}, std::min(live, cap));
}

size_t AutonomousTrainingPipeline::live_parallel_batches_value() const noexcept {
    return live_parallel_batches_.load(std::memory_order_relaxed);
}

size_t AutonomousTrainingPipeline::parallel_batches_ceiling_value() const noexcept {
    return parallel_batches_ceiling_;
}

void AutonomousTrainingPipeline::set_state(PipelineState new_state) {
    PipelineState old_state = state_.exchange(new_state);
    if (old_state != new_state && state_callback_) {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        state_callback_(old_state, new_state);
    }
}

void AutonomousTrainingPipeline::on_state_change(
    std::function<void(PipelineState, PipelineState)> callback
) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    state_callback_ = callback;
}

void AutonomousTrainingPipeline::on_metrics_update(
    std::function<void(const PipelineMetrics&)> callback
) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    metrics_callback_ = callback;
}

std::string AutonomousTrainingPipeline::state_to_string(PipelineState state) {
    switch (state) {
        case PipelineState::IDLE: return "idle";
        case PipelineState::INITIALIZING: return "initializing";
        case PipelineState::READY: return "ready";
        case PipelineState::ACQUIRING_DATA: return "acquiring_data";
        case PipelineState::TRAINING: return "training";
        case PipelineState::EVALUATING_TOPOLOGY: return "evaluating_topology";
        case PipelineState::OPTIMIZING_GRAPH: return "optimizing_graph";
        case PipelineState::CHECKPOINTING: return "checkpointing";
        case PipelineState::PAUSED: return "paused";
        case PipelineState::STOPPING: return "stopping";
        case PipelineState::COMPLETE: return "complete";
        case PipelineState::FAILED: return "error";
        default: return "unknown";
    }
}

std::unique_ptr<AutonomousTrainingPipeline> create_training_pipeline() {
    return std::make_unique<AutonomousTrainingPipeline>();
}

} // namespace q_mini_wasm_v2::core::training
