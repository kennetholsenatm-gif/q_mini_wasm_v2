#include "autonomous_training_pipeline.hpp"
#include "../gf3/gf3_types.hpp"
#include "../moe/gf3_layers.hpp"
#include "../moe/unified_config.hpp"
#include "../moe/runtime_orchestrator.hpp"
#include "../moe/gf3_sycl_probe.hpp"
#ifdef _OPENMP
#include <omp.h>
#endif
#include "../ternary/trit.hpp"
#include "../ternary/packing.hpp"
#include <fstream>
#include <iostream>
#include <algorithm>
#include <numeric>
#include <variant>
#include <unordered_map>
#include <chrono>
#include <optional>
#include <future>
#include <random>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <limits>
#if defined(USE_SYCL) && USE_SYCL
#include "../../sycl/tableau_kernels.hpp"
#endif

namespace q_mini_wasm_v2::core::training {

namespace {

constexpr char kFileMagicV3[8] = {'Q', 'M', 'I', 'N', 'I', '_', 'V', '3'};

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

/** Upper bound on samples collected per training batch (bounded by batch_size). Set only from TOML via InitSession (must be > 0). */
size_t training_micro_batch_cap_for(const PipelineConfig& c) {
    return std::min(c.training_micro_batch_cap, size_t{4096});
}

/** Minimum micro-batch size after adaptive clamp. Set only from TOML (must be > 0). */
size_t training_collect_floor_for(const PipelineConfig& c) {
    return std::min(c.training_collect_floor, size_t{512});
}

bool training_timing_enabled_for(const PipelineConfig& c) {
    return c.training_timing_to_stderr;
}

/** When OpenMP is enabled, run independent expert TrainForwardForward in parallel unless TOML requests serial. */
bool training_parallel_experts_enabled_for(const PipelineConfig& c) {
#if !defined(_OPENMP)
    (void)c;
    return false;
#else
    return !c.training_serial_experts;
#endif
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
    const size_t floor_v = training_collect_floor_for(c);
    if (hard_cap <= 1) {
        return std::max(size_t{1}, std::min(hard_cap, floor_v));
    }
    size_t tier = std::min(hard_cap, size_t{32});
    if (c.moe_num_experts >= 4096) {
        tier = std::min(hard_cap, size_t{1});
    } else if (c.moe_num_experts >= 2048) {
        tier = std::min(hard_cap, size_t{2});
    } else {
        const uint64_t work = router_work_estimate(c);
        if (work >= 200000000ull) {
            tier = std::min(hard_cap, size_t{1});
        } else if (work >= 100000000ull) {
            tier = std::min(hard_cap, size_t{2});
        } else if (work >= 40000000ull) {
            tier = std::min(hard_cap, size_t{4});
        } else if (work >= 10000000ull) {
            tier = std::min(hard_cap, size_t{8});
        } else if (work >= 3000000ull) {
            tier = std::min(hard_cap, size_t{16});
        }
    }
    return std::max(size_t{1}, std::min(hard_cap, std::max(floor_v, tier)));
}

size_t adaptive_route_topk_cap(const PipelineConfig& c) {
    const size_t requested = std::max<size_t>(size_t{1}, c.moe_top_k);
    if (c.moe_num_experts >= 4096) {
        return std::min(requested, size_t{1});
    }
    if (c.moe_num_experts >= 2048) {
        return std::min(requested, size_t{2});
    }
    const uint64_t route_work =
        static_cast<uint64_t>(c.moe_num_experts) *
        static_cast<uint64_t>(c.routing_qutrits) *
        static_cast<uint64_t>(requested);

    if (route_work >= 300000000ull) {
        return std::min(requested, size_t{4});
    }
    if (route_work >= 120000000ull) {
        return std::min(requested, size_t{8});
    }
    if (route_work >= 40000000ull) {
        return std::min(requested, size_t{16});
    }
    if (route_work >= 12000000ull) {
        return std::min(requested, size_t{32});
    }
    return requested;
}

#if defined(USE_SYCL) && USE_SYCL
/** When @p min_moe_dim is 0, SYCL quantization for this path is disabled. */
inline bool pipeline_use_sycl_trit_quant(size_t moe_dim, size_t min_moe_dim) noexcept {
    return min_moe_dim > 0 && moe_dim >= min_moe_dim;
}

inline void assign_trits_from_i8_pack(const std::vector<int8_t>& packed, std::vector<ternary::Trit>& result) {
    result.clear();
    result.reserve(packed.size());
    for (int8_t v : packed) {
        result.push_back(static_cast<ternary::Trit>(v));
    }
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
    if (!rd_u64(is, n) || n > 64ull * 1024ull * 1024ull) {
        return false;
    }
    s.resize(static_cast<size_t>(n));
    if (n > 0) {
        is.read(s.data(), static_cast<std::streamsize>(n));
    }
    return static_cast<bool>(is);
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
    wr_u8(os, c.training_serial_experts ? uint8_t{1} : uint8_t{0});
    wr_u8(os, static_cast<uint8_t>(c.training_sycl_route_mode));
    wr_sz(os, c.sycl_trit_quant_min_moe_dim);
    wr_u8(os, static_cast<uint8_t>(std::min<uint32_t>(2u, c.goodness_log_level)));
    wr_u8(os, c.allow_generated_negatives ? uint8_t{1} : uint8_t{0});
}

bool read_pipeline_config(std::istream& is, PipelineConfig& c) {
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
    c.training_serial_experts = false;
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
    c.training_serial_experts = (sb != 0);
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
    if (rm <= static_cast<uint8_t>(moe::SyclRouteMode::Off)) {
        c.training_sycl_route_mode = static_cast<moe::SyclRouteMode>(rm);
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
    return true;
}

} // namespace

AutonomousTrainingPipeline::AutonomousTrainingPipeline() = default;

AutonomousTrainingPipeline::~AutonomousTrainingPipeline() {
    stop_requested_ = true;
    if (training_thread_.joinable()) {
        training_thread_.join();
    }
    if (synthesizer_) {
        synthesizer_->stop();
    }
    shutdown_components();
}

bool AutonomousTrainingPipeline::initialize(const PipelineConfig& config) {
    std::lock_guard<std::mutex> lock(config_mutex_);

    shutdown_components();

    set_state(PipelineState::INITIALIZING);
    config_ = config;
    
    if (!initialize_components()) {
        set_state(PipelineState::FAILED);
        return false;
    }
    
    set_state(PipelineState::READY);
    return true;
}

bool AutonomousTrainingPipeline::initialize_components() {
    // Initialize DataSynthesizer
    synthesizer_ = std::make_unique<DataSynthesizer>();
    synthesizer_->set_queue_limits(
        config_.max_raw_queue_depth,
        config_.max_train_queue_depth,
        config_.max_acquisition_queue_depth
    );

    // 1) Local corpus first (lines under dataset_dir / acquired, etc.)
    bool have_local = false;
    if (!config_.data_path.empty()) {
        have_local = synthesizer_->load_local_data(config_.data_path);
        if (have_local) {
            std::cout << "[TrainingPipeline] Local data loaded from: " << config_.data_path << std::endl;
        } else {
            std::cerr << "[TrainingPipeline] WARN: Could not load local data from: " << config_.data_path
                      << " (continuing if TOML/web sources load)" << std::endl;
        }
    }

    // 2) Config / web acquisition (always attempted when TOML path is known)
    const std::string& ds_path = config_.data_sources_toml_path.empty()
        ? std::string("config/data_sources.toml")
        : config_.data_sources_toml_path;
    const bool have_config = synthesizer_->use_config(ds_path);
    if (have_config) {
        std::cout << "[TrainingPipeline] Data sources config loaded: " << ds_path << std::endl;
    } else {
        std::cerr << "[TrainingPipeline] WARN: Failed to load data_sources.toml at: " << ds_path << std::endl;
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
    // Align router Top-K with process_batch guardrails: scoring partial_sort used to
    // use full moe_top_k even when training only applies adaptive_route_topk_cap(),
    // which wasted CPU and looked like a stall (metrics frozen for long intervals).
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
        } else if (router_config.sycl_route_mode == moe::SyclRouteMode::Off) {
            pref = "off";
        }
        bool expect_sycl = false;
#if defined(USE_SYCL) && USE_SYCL
        if (router_config.total_experts >= 8) {
            if (router_config.sycl_route_mode == moe::SyclRouteMode::On) {
                expect_sycl = true;
            } else if (router_config.sycl_route_mode == moe::SyclRouteMode::Auto) {
                expect_sycl = router_config.total_experts >= 128;
            }
        }
#endif
        std::cout << "[TrainingPipeline] MoE symplectic routing logits: sycl_route_mode=" << pref
                  << ", expect_sycl_device=" << (expect_sycl ? 1 : 0)
                  << " (expert Forward–Forward stays on CPU unless further kernels are added)" << std::endl;
    }
    router_ = std::make_unique<moe::MoERouter>(router_config);
    
    // Create experts
    moe::ExpertNetwork::ExpertConfig expert_config;
    expert_config.input_dim = config_.moe_input_dim;
    expert_config.output_dim = config_.moe_output_dim;
    expert_config.hidden_dim = config_.moe_hidden_dim;
    {
        size_t internal_layers = config_.moe_expert_internal_layers;
        if (config_.moe_ff_active_internal_layers > 0) {
            internal_layers = std::min(internal_layers, config_.moe_ff_active_internal_layers);
        }
        expert_config.num_layers = std::max<size_t>(1u, internal_layers);
    }
    expert_template_ = expert_config;

    if (config_.lazy_moe_experts) {
        experts_.clear();
        experts_.resize(config_.moe_num_experts);
        std::cout << "[TrainingPipeline] Lazy MoE: " << config_.moe_num_experts
                  << " logical experts, top_k=" << config_.moe_top_k
                  << ", materialize stacks on first route (internal_layers="
                  << expert_template_.num_layers << ")" << std::endl;
    } else {
        experts_ = create_experts(config_.moe_num_experts, expert_config);
        std::cout << "[TrainingPipeline] Eager MoE: materialized " << experts_.size()
                  << " experts at init (high memory)" << std::endl;
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
    std::cout << "[TrainingPipeline] OpenMP expert/layer parallelism: max_threads=" << omp_get_max_threads()
              << " (per-route expert parallelism follows training.serial_expert_train in TOML)" << std::endl;
#endif
    
    return true;
}

std::vector<std::unique_ptr<moe::ExpertNetwork>> AutonomousTrainingPipeline::create_experts(
    size_t num_experts,
    const moe::ExpertNetwork::ExpertConfig& expert_config
) {
    std::vector<std::unique_ptr<moe::ExpertNetwork>> experts;
    experts.reserve(num_experts);
    
    for (size_t i = 0; i < num_experts; ++i) {
        // Create GF3MultiLayerExpert for each expert slot
        auto expert = std::make_unique<moe::GF3MultiLayerExpert>(expert_config);
        experts.push_back(std::move(expert));
    }
    
    return experts;
}

moe::ExpertNetwork* AutonomousTrainingPipeline::ensure_expert(size_t expert_idx) {
    std::lock_guard<std::mutex> lock(experts_mutex_);
    if (expert_idx >= experts_.size()) {
        return nullptr;
    }
    if (!experts_[expert_idx]) {
        experts_[expert_idx] = std::make_unique<moe::GF3MultiLayerExpert>(expert_template_);
    }
    return experts_[expert_idx].get();
}

void AutonomousTrainingPipeline::shutdown_components() {
    if (synthesizer_) {
        synthesizer_->stop();
    }
    
    experts_.clear();
    router_.reset();
    ff_learner_.reset();
    synthesizer_.reset();
    betti_extractor_.reset();
    graph_tableau_.reset();
}

bool AutonomousTrainingPipeline::start_training() {
    PipelineState warm = state_.load();
    if (warm == PipelineState::COMPLETE) {
        set_state(PipelineState::READY);
    }

    PipelineState expected = PipelineState::READY;
    if (!state_.compare_exchange_strong(expected, PipelineState::ACQUIRING_DATA)) {
        return false;  // Not in READY state
    }
    
    stop_requested_ = false;
    pause_requested_ = false;
    current_epoch_ = 0;
    current_batch_ = 0;
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
    
    // Start DataSynthesizer
    synthesizer_->start(config_.acquisition_threads, config_.perturbation_threads);
    const size_t collect_limit = adaptive_collect_limit(config_);
    const size_t hard_cap = std::min(config_.batch_size, training_micro_batch_cap_for(config_));
    if (collect_limit < hard_cap && !microbatch_guardrail_logged_.exchange(true)) {
        std::cout << "[TrainingPipeline] Microbatch guardrail active: collect_limit="
                  << collect_limit << " (from hard_cap=" << hard_cap
                  << ", experts=" << config_.moe_num_experts
                  << ", routing_qutrits=" << config_.routing_qutrits
                  << ", estimated_router_work=" << router_work_estimate(config_) << ")"
                  << std::endl;
    }
    const size_t route_cap = adaptive_route_topk_cap(config_);
    if (route_cap < std::max<size_t>(size_t{1}, config_.moe_top_k)) {
        std::cout << "[TrainingPipeline] Routing guardrail active: effective_top_k="
                  << route_cap << " (requested=" << config_.moe_top_k
                  << ", experts=" << config_.moe_num_experts
                  << ", routing_qutrits=" << config_.routing_qutrits << ")"
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
        const size_t target = std::max<size_t>(size_t{1}, config_.prefill_target_samples);
        const auto timeout = std::chrono::milliseconds(std::max<uint32_t>(1000u, config_.prefill_timeout_ms));
        const auto poll = std::chrono::milliseconds(std::max<uint32_t>(10u, config_.prefill_poll_ms));
        const auto started = std::chrono::steady_clock::now();
        bool reached = false;
        size_t current = 0;
        size_t log_tick = 0;
        bool first_sample_logged = false;
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
            if ((log_tick++ % 20u) == 0u) {
                std::cout << "[TrainingPipeline] Prefill ring buffer: " << current
                          << "/" << target << " samples" << std::endl;
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
    set_state(PipelineState::TRAINING);
    
    while (!stop_requested_) {
        // Check for pause
        if (pause_requested_) {
            set_state(PipelineState::PAUSED);
            while (pause_requested_ && !stop_requested_) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            if (stop_requested_) break;
            set_state(PipelineState::TRAINING);
        }
        
        // Process batch - returns number of samples actually trained
        size_t samples_trained = process_batch();
        
        if (samples_trained == 0) {
            // No samples available - DataSynthesizer not producing data
            // Log periodically but don't fail - APIs might recover
            ++consecutive_empty_batches_;
            if (consecutive_empty_batches_ % 100u == 1u) {
                std::cerr << "[TrainingPipeline] Waiting for DataSynthesizer to produce samples... "
                          << "(empty batches: " << consecutive_empty_batches_ << ")" << std::endl;
            }
            // Push live queue/epoch hints to MCP even when idle (GetProgress reads last_metrics).
            if (consecutive_empty_batches_ % 25u == 1u) {
                update_metrics();
                if (config_.enable_wui_streaming) {
                    emit_metrics();
                }
            }
            // Brief yield before retry
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;  // Retry without incrementing counters
        }
        
        consecutive_empty_batches_ = 0;
        
        // Update batch counter only when training actually occurred
        ++current_batch_;
        
        // Topology evaluation
        if (config_.enable_betti_guidance && 
            current_batch_ % config_.topology_evaluation_interval == 0) {
            evaluate_topology();
        }
        
        // samples_trained = contrastive rows this batch (not MoE route fan-out).
        samples_processed_ += samples_trained;
        samples_processed_total_ += samples_trained;
        if (samples_processed_ >= config_.samples_per_epoch) {
            ++current_epoch_;
            samples_processed_ = 0;
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
                    break;
                }
            }
        }
        
        update_metrics();
        if (config_.enable_wui_streaming) {
            emit_metrics();
        }
        
        // After productive work, yield only; sleep when idle to avoid pegging a core.
        if (samples_trained > 0) {
            std::this_thread::yield();
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    
    if (state_.load() != PipelineState::COMPLETE && 
        state_.load() != PipelineState::FAILED) {
        set_state(PipelineState::STOPPING);
    }
}

size_t AutonomousTrainingPipeline::process_batch() {
    if (config_.moe_input_dim == 0) {
        std::cerr << "[TrainingPipeline] moe_input_dim is 0; cannot form training vectors." << std::endl;
        return 0;
    }
    {
        std::lock_guard<std::mutex> lock(metrics_mutex_);
        cached_metrics_.train_batch_collect_limit = 0;
        cached_metrics_.train_batch_rows_done = 0;
        cached_metrics_.ff_route_steps_current_batch = 0;
    }
    const bool kTiming = training_timing_enabled_for(config_);
    const auto timing_batch_t0 = kTiming ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    int64_t timing_collect_ms = 0;
    int64_t timing_route_ms = 0;
    int64_t timing_ff_ms = 0;

    // Get samples from DataSynthesizer (prefer async pos+neg pairs from perturbation_worker).
    struct ContrastiveRow {
        TrainingSample positive;
        std::optional<TrainingSample> prepared_negative;
    };
    std::vector<ContrastiveRow> batch_work;
    // Do not wait for full training.batch_size before first MoE step.
    // For large MoE routing matrices, clamp aggressively so metrics and counters advance steadily.
    const size_t collect_limit = std::max(size_t{1}, adaptive_collect_limit(config_));
    const size_t effective_top_k = adaptive_route_topk_cap(config_);
    batch_work.reserve(collect_limit);

    // Timeout mechanism: max 30 seconds to acquire collect_limit samples
    const auto max_wait_time = std::chrono::seconds(30);
    const auto start_time = std::chrono::steady_clock::now();
    auto last_heartbeat = start_time;
    size_t consecutive_empty_checks = 0;
    batch_inflight_active_.store(true);
    batch_inflight_done_.store(0);
    {
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
                      << "/" << collect_limit << " within 30 seconds. "
                      << "DataSynthesizer may not be producing samples." << std::endl;
            batch_inflight_active_.store(false);
            batch_inflight_done_.store(0);
            batch_inflight_target_.store(0);
            return 0;  // Return 0 samples trained - no progress possible
        }
        
        TrainingSample pos_pair;
        TrainingSample neg_pair;
        if (synthesizer_->try_pop_contrastive_pair(pos_pair, neg_pair)) {
            batch_work.push_back({std::move(pos_pair), std::move(neg_pair)});
            batch_inflight_done_.store(static_cast<uint32_t>(batch_work.size()));
            consecutive_empty_checks = 0;
        } else {
            // Wait for samples with exponential backoff
            consecutive_empty_checks++;
            auto wait_ms = std::min(10 * (1 << std::min(consecutive_empty_checks, size_t(10))), 1000);
            std::this_thread::sleep_for(std::chrono::milliseconds(wait_ms));
            --i;  // Retry this sample
        }

        const auto now = std::chrono::steady_clock::now();
        if (now - last_heartbeat >= std::chrono::seconds(2)) {
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
        batch_inflight_active_.store(false);
        batch_inflight_done_.store(0);
        batch_inflight_target_.store(0);
        return 0;  // Return 0 samples trained - no progress was made
    }

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
    bool batch_used_tritpack5_input_route = false;
    bool batch_router_sycl_path_used = false;
    auto last_train_hb = std::chrono::steady_clock::now();
    // Allow an early [Pipeline] emit once FF routes exist (do not wait 2s from cold start).
    auto last_streaming_pipeline_emit = std::chrono::steady_clock::now() - std::chrono::seconds(5);

    // Process each sample through Forward-Forward and MoE
    for (size_t row_index = 0; row_index < batch_work.size(); ++row_index) {
        const auto& row = batch_work[row_index];
        TernaryRouteInput pos_in = extract_ternary_route_input(row.positive);
        std::vector<ternary::Trit> positive_sample = std::move(pos_in.trits);

        if (positive_sample.empty()) {
            std::cerr << "[TrainingPipeline] ERROR: empty positive sample after conversion; failing batch." << std::endl;
            {
                std::lock_guard<std::mutex> lock(metrics_mutex_);
                cached_metrics_.status_message =
                    "DATA_ERROR: empty positive sample after conversion";
            }
            batch_inflight_active_.store(false);
            batch_inflight_done_.store(0);
            batch_inflight_target_.store(0);
            stop_requested_ = true;
            set_state(PipelineState::FAILED);
            return 0;
        }

        std::vector<ternary::Trit> negative_sample;
        if (row.prepared_negative) {
            TernaryRouteInput neg_in = extract_ternary_route_input(*row.prepared_negative);
            negative_sample = std::move(neg_in.trits);
        }
        if (negative_sample.empty() && config_.allow_generated_negatives) {
            negative_sample = generate_negative_sample(positive_sample);
        }
        if (negative_sample.empty()) {
            std::cerr << "[TrainingPipeline] ERROR: missing contrastive negative sample; failing batch." << std::endl;
            {
                std::lock_guard<std::mutex> lock(metrics_mutex_);
                cached_metrics_.status_message =
                    "DATA_ERROR: missing contrastive negative sample";
            }
            batch_inflight_active_.store(false);
            batch_inflight_done_.store(0);
            batch_inflight_target_.store(0);
            stop_requested_ = true;
            set_state(PipelineState::FAILED);
            return 0;
        }

        // Route positive sample: TritPack5 path uses dense GF(3) packed input (+ packed weights) for symplectic logits
        const auto timing_route_t0 = kTiming ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
        std::vector<size_t> selected_experts =
            pos_in.route_packed_t5.empty() ? router_->route_topk(positive_sample)
                                           : router_->route_topk_from_tritpack5(pos_in.route_packed_t5, pos_in.trits.size());
        batch_used_tritpack5_input_route =
            batch_used_tritpack5_input_route || !pos_in.route_packed_t5.empty();
        batch_router_sycl_path_used =
            batch_router_sycl_path_used || router_->symplectic_logits_last_used_sycl();
        if (kTiming) {
            timing_route_ms += elapsed_ms(timing_route_t0);
        }
        if (selected_experts.size() > effective_top_k) {
            selected_experts.resize(effective_top_k);
        }
        if (selected_experts.empty() && !experts_.empty()) {
            std::cerr << "[TrainingPipeline] ERROR: router returned no experts for a row; failing batch." << std::endl;
            {
                std::lock_guard<std::mutex> lock(metrics_mutex_);
                cached_metrics_.status_message =
                    "ROUTER_ERROR: route_topk returned no experts";
            }
            batch_inflight_active_.store(false);
            batch_inflight_done_.store(0);
            batch_inflight_target_.store(0);
            stop_requested_ = true;
            set_state(PipelineState::FAILED);
            return 0;
        }
        
        // Train each selected expert with Forward-Forward (lazy materialize when lazy_moe_experts).
        // Materialize + resolve pointers on the training thread before optional OpenMP across experts.
        std::vector<size_t> route_expert_idx;
        std::vector<moe::ExpertNetwork*> route_expert_ptr;
        route_expert_idx.reserve(selected_experts.size());
        route_expert_ptr.reserve(selected_experts.size());
        for (size_t expert_idx : selected_experts) {
            if (expert_idx >= experts_.size()) {
                continue;
            }
            moe::ExpertNetwork* expert = nullptr;
            if (config_.lazy_moe_experts) {
                expert = ensure_expert(expert_idx);
            } else {
                expert = experts_[expert_idx].get();
            }
            if (!expert) {
                continue;
            }
            route_expert_idx.push_back(expert_idx);
            route_expert_ptr.push_back(expert);
        }

        const int nr = static_cast<int>(route_expert_ptr.size());
        std::vector<uint32_t> pos_good(nr, 0);
        std::vector<uint32_t> neg_good(nr, 0);
        std::vector<int64_t> ff_ms_part;
        if (kTiming && nr > 0) {
            ff_ms_part.assign(static_cast<size_t>(nr), 0);
        }

#if defined(_OPENMP)
        const bool use_omp_experts = training_parallel_experts_enabled_for(config_) && nr > 1;
#pragma omp parallel for schedule(static) if(use_omp_experts)
#endif
        for (int ri = 0; ri < nr; ++ri) {
            moe::ExpertNetwork* expert = route_expert_ptr[static_cast<size_t>(ri)];
            const auto timing_ff_t0 = kTiming ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
            expert->TrainForwardForward(positive_sample, negative_sample);
            const auto route_g = expert->last_forward_forward_route_goodness();
            pos_good[static_cast<size_t>(ri)] = route_g.first;
            neg_good[static_cast<size_t>(ri)] = route_g.second;
            if (kTiming) {
                ff_ms_part[static_cast<size_t>(ri)] = elapsed_ms(timing_ff_t0);
            }
        }

        for (int ri = 0; ri < nr; ++ri) {
            const size_t expert_idx = route_expert_idx[static_cast<size_t>(ri)];
            const uint32_t pos_goodness = pos_good[static_cast<size_t>(ri)];
            const uint32_t neg_goodness = neg_good[static_cast<size_t>(ri)];
            if (kTiming) {
                timing_ff_ms += ff_ms_part[static_cast<size_t>(ri)];
            }
            const int32_t delta = static_cast<int32_t>(pos_goodness) - static_cast<int32_t>(neg_goodness);

            sum_pos_goodness += static_cast<uint64_t>(pos_goodness);
            sum_neg_goodness += static_cast<uint64_t>(neg_goodness);
            sum_delta += static_cast<int64_t>(delta);
            expert_counts[expert_idx]++;
            expert_deltas[expert_idx] += delta;
            total_routes++;
            {
                const uint64_t sum = static_cast<uint64_t>(collect_limit) + static_cast<uint64_t>(total_routes);
                batch_inflight_done_.store(static_cast<uint32_t>(
                    std::min<uint64_t>(sum, static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()))));
            }
            const auto hb_now = std::chrono::steady_clock::now();
            if (hb_now - last_train_hb >= std::chrono::seconds(2)) {
                update_metrics();
                if (config_.enable_wui_streaming) {
                    emit_metrics();
                }
                last_train_hb = hb_now;
            }
        }

        // Publish running FF averages during the micro-batch so GetProgress / [Pipeline] are not stuck at
        // zeros until all collect_limit rows finish (large batches can take minutes).
        {
            std::lock_guard<std::mutex> lock(metrics_mutex_);
            cached_metrics_.train_batch_rows_done = static_cast<uint32_t>(row_index + 1);
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
            if (stream_now - last_streaming_pipeline_emit >= std::chrono::seconds(2)) {
                update_metrics();
                emit_metrics();
                last_streaming_pipeline_emit = stream_now;
            }
        }

        if (goodness_log_level >= 2u && row_index < 3 && nr > 0) {
            const size_t out_dim = route_expert_ptr[0]->GetConfig().output_dim;
            std::fprintf(stderr,
                         "[TrainingPipeline][Goodness] row=%zu MoE_input_trits=%zu expert_last_layer_dim=%zu | "
                         "per_route (#nonzero_out_pos #nonzero_out_neg delta): ",
                         row_index,
                         positive_sample.size(),
                         out_dim);
            for (int ri = 0; ri < nr; ++ri) {
                const uint32_t pg = pos_good[static_cast<size_t>(ri)];
                const uint32_t ng = neg_good[static_cast<size_t>(ri)];
                const int32_t d = static_cast<int32_t>(pg) - static_cast<int32_t>(ng);
                std::fprintf(stderr,
                             "e%zu(%u/%u/%d)%s",
                             route_expert_idx[static_cast<size_t>(ri)],
                             static_cast<unsigned>(pg),
                             static_cast<unsigned>(ng),
                             static_cast<int>(d),
                             (ri + 1 < nr) ? " " : "\n");
            }
        }
    }

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

    // Return contrastive rows trained (one per batch_work entry), not per-expert route count.
    batch_inflight_active_.store(false);
    batch_inflight_done_.store(0);
    batch_inflight_target_.store(0);
    if (kTiming) {
        const int64_t total_ms = elapsed_ms(timing_batch_t0);
        std::cerr << "[TrainingTiming] total_ms=" << total_ms << " collect_ms=" << timing_collect_ms
                  << " route_ms=" << timing_route_ms << " ff_train_ms=" << timing_ff_ms
                  << " routes=" << total_routes << " rows=" << batch_work.size()
                  << " collect_limit=" << collect_limit
                  << " top_k_cap=" << effective_top_k << std::endl;
    }
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
            if (pipeline_use_sycl_trit_quant(config_.moe_input_dim, config_.sycl_trit_quant_min_moe_dim) && !arg.empty()) {
                const size_t src_use = std::min(arg.size(), static_cast<size_t>(config_.moe_input_dim));
                std::vector<int8_t> packed = ::q_mini_wasm_v2::sycl_kernels::quantize_float_buffer_to_trits_sycl(
                    arg.data(), src_use, config_.moe_input_dim);
                assign_trits_from_i8_pack(packed, out.trits);
                q::ternary::pack_batch_t5(packed, out.route_packed_t5);
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
            if (pipeline_use_sycl_trit_quant(config_.moe_input_dim, config_.sycl_trit_quant_min_moe_dim) && !arg.empty()) {
                const size_t src_use = std::min(arg.size(), static_cast<size_t>(config_.moe_input_dim));
                std::vector<int8_t> packed = ::q_mini_wasm_v2::sycl_kernels::quantize_i32_buffer_to_trits_sycl(
                    arg.data(), src_use, config_.moe_input_dim);
                assign_trits_from_i8_pack(packed, out.trits);
                q::ternary::pack_batch_t5(packed, out.route_packed_t5);
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
                const size_t src_use = std::min(scratch.size(), static_cast<size_t>(config_.moe_input_dim));
                std::vector<int8_t> packed = ::q_mini_wasm_v2::sycl_kernels::quantize_i32_buffer_to_trits_sycl(
                    scratch.empty() ? nullptr : scratch.data(), src_use, config_.moe_input_dim);
                assign_trits_from_i8_pack(packed, out.trits);
                q::ternary::pack_batch_t5(packed, out.route_packed_t5);
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
                const size_t src_use = std::min(scratch.size(), static_cast<size_t>(config_.moe_input_dim));
                std::vector<int8_t> packed = ::q_mini_wasm_v2::sycl_kernels::quantize_float_buffer_to_trits_sycl(
                    scratch.empty() ? nullptr : scratch.data(), src_use, config_.moe_input_dim);
                assign_trits_from_i8_pack(packed, out.trits);
                q::ternary::pack_batch_t5(packed, out.route_packed_t5);
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
        } else if constexpr (std::is_same_v<T, std::string_view>) {
            if (arg.empty()) {
                return;
            }
#if defined(USE_SYCL) && USE_SYCL
            if (pipeline_use_sycl_trit_quant(config_.moe_input_dim, config_.sycl_trit_quant_min_moe_dim)) {
                std::vector<int8_t> packed = ::q_mini_wasm_v2::sycl_kernels::quantize_string_bytes_to_trits_sycl(
                    arg.data(), arg.size(), config_.moe_input_dim);
                assign_trits_from_i8_pack(packed, out.trits);
                q::ternary::pack_batch_t5(packed, out.route_packed_t5);
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
    if (out.trits.size() < config_.moe_input_dim) {
        out.trits.resize(config_.moe_input_dim, ternary::Trit::ZERO);
    } else if (out.trits.size() > config_.moe_input_dim) {
        out.trits.resize(config_.moe_input_dim);
    }
    if (!out.route_packed_t5.empty()) {
        std::vector<int8_t> lanes(out.trits.size());
        for (size_t i = 0; i < out.trits.size(); ++i) {
            lanes[i] = static_cast<int8_t>(out.trits[i]);
        }
        q::ternary::pack_batch_t5(lanes, out.route_packed_t5);
    }

    return out;
}

/**
 * @brief Generate negative sample by corrupting positive sample
 */
std::vector<ternary::Trit> AutonomousTrainingPipeline::generate_negative_sample(
    const std::vector<ternary::Trit>& positive
) {
    static std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<size_t> pos_dist(0, positive.size() - 1);
    std::uniform_int_distribution<int> val_dist(-1, 1);
    
    std::vector<ternary::Trit> negative = positive;
    
    // Corrupt ~10% of values
    size_t num_corruptions = std::max(size_t(1), positive.size() / 10);
    
    for (size_t i = 0; i < num_corruptions; ++i) {
        size_t pos = pos_dist(rng);
        // Flip to different value
        int8_t new_val = static_cast<int8_t>(val_dist(rng));
        negative[pos] = static_cast<ternary::Trit>(new_val);
    }
    
    return negative;
}

bool AutonomousTrainingPipeline::evaluate_topology() {
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
    
    metrics.status_message = status;
    metrics.gf3_hebbian_weight_cell_updates = moe::gf3_hebbian_weight_cell_updates_total();
    // Always surface live counters (cache can lag one update vs training_loop atomics).
    metrics.samples_processed = samples_processed_.load();
    metrics.samples_processed_total = samples_processed_total_.load();
    
    return metrics;
}

void AutonomousTrainingPipeline::update_metrics() {
    // Update internal metrics cache
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    
    // Pipeline state
    cached_metrics_.current_epoch = current_epoch_.load();
    cached_metrics_.current_batch = current_batch_.load();
    cached_metrics_.samples_processed = samples_processed_.load();
    cached_metrics_.samples_processed_total = samples_processed_total_.load();
    cached_metrics_.is_running = (state_ == PipelineState::TRAINING);
    
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
    if (config_.enable_checkpoints) {
        std::string checkpoint_path = "checkpoint_epoch_" + 
            std::to_string(current_epoch_) + "_batch_" + 
            std::to_string(current_batch_) + ".qmini";
        export_model(checkpoint_path);
    }
}

void AutonomousTrainingPipeline::apply_run_overrides(const std::string& data_path, uint32_t num_epochs) {
    std::lock_guard<std::mutex> lock(config_mutex_);
    config_.data_path = data_path;
    config_.num_epochs = static_cast<size_t>(num_epochs);
}

bool AutonomousTrainingPipeline::export_model(const std::string& path) {
    const PipelineState st0 = state_.load();
    if (st0 == PipelineState::TRAINING || st0 == PipelineState::ACQUIRING_DATA) {
        return false;
    }

    std::scoped_lock lock(config_mutex_, experts_mutex_);

    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    file.write(kFileMagicV3, 8);
    const uint32_t fmt = 5; // v5: full MoE router state (RUF2) after expert blobs
    wr_u32(file, fmt);
    write_pipeline_config(file, config_);

    const uint64_t epoch = current_epoch_.load();
    const uint64_t batch = current_batch_.load();
    const uint64_t samples_total = samples_processed_total_.load();
    wr_u64(file, epoch);
    wr_u64(file, batch);
    wr_u64(file, samples_total);

    const uint64_t graph_nodes = graph_tableau_ ? static_cast<uint64_t>(graph_tableau_->num_qutrits()) : 0ull;
    wr_u64(file, graph_nodes);

    const uint64_t num_slots = static_cast<uint64_t>(experts_.size());
    wr_u64(file, num_slots);

    for (size_t i = 0; i < experts_.size(); ++i) {
        const uint8_t present = experts_[i] ? uint8_t{1} : uint8_t{0};
        wr_u8(file, present);
        if (!present) {
            continue;
        }
        auto* gf3 = dynamic_cast<moe::GF3MultiLayerExpert*>(experts_[i].get());
        if (!gf3) {
            return false;
        }
        gf3->SerializeWeights(file);
    }

    if (router_) {
        router_->SerializeRouterState(file);
    }

    return static_cast<bool>(file);
}

bool AutonomousTrainingPipeline::import_model(const std::string& path) {
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
    if (!rd_u32(file, fmt) || (fmt != 3 && fmt != 4 && fmt != 5)) {
        return false;
    }

    PipelineConfig imported{};
    if (!read_pipeline_config(file, imported)) {
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
        auto* gf3 = dynamic_cast<moe::GF3MultiLayerExpert*>(experts_[i].get());
        if (!gf3 || !gf3->DeserializeWeights(file)) {
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
    return true;
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
