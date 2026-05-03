#pragma once

#include <cstdint>
#include <vector>
#include <memory>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <functional>
#include <optional>
#include <ostream>
#include <string>
#include <unordered_map>

#include "data_synthesizer.hpp"
#include "../learning/forward_forward.hpp"
#include "../moe/router.hpp"
#include "../moe/gf3_layers.hpp"
#include "../qgnn/betti_extractor.hpp"
#include "../qgnn/graph_tableau.hpp"

namespace q_mini_wasm_v2::core::training {

/**
 * @brief Ternary input for FF training plus optional TritPack5 for MoE routing.
 *
 * When @ref route_packed_t5 is non-empty, @c MoERouter::route_topk_from_tritpack5 reads the wire directly;
 * @ref trits may stay empty until the row path unpacks once for Forward–Forward.
 */
struct TernaryRouteInput {
    std::vector<ternary::Trit> trits;
    std::vector<uint8_t> route_packed_t5;
};

/**
 * @brief Comprehensive training metrics from all pipeline stages
 */
struct PipelineMetrics {
    // Forward-Forward metrics (running averages over the in-flight micro-batch once routes > 0; finalized at batch end)
    uint32_t ff_positive_goodness = 0;
    uint32_t ff_negative_goodness = 0;
    int32_t ff_goodness_delta = 0;
    uint64_t ff_total_train_calls = 0;
    /** Expert-level FF steps completed in the current in-flight micro-batch (0 when idle). */
    uint64_t ff_route_steps_current_batch = 0;
    /** Contrastive rows completed for the current micro-batch / @ref train_batch_collect_limit (0 when idle). */
    uint32_t train_batch_rows_done = 0;
    /** Target contrastive rows for the current micro-batch (0 between batches). */
    uint32_t train_batch_collect_limit = 0;
    /** Effective per-batch collect limit from TOML (min(batch_size, micro_batch_cap)). */
    uint32_t train_collect_limit_effective = 0;
    /** Effective route top-k from TOML (capped by expert count). */
    uint32_t route_topk_effective = 0;
    /** Resident expert objects currently kept in memory (lazy mode). */
    uint32_t experts_resident = 0;
    /** Evicted expert weight blobs currently spilled out of resident memory. */
    uint32_t experts_spilled = 0;
    /** Current lazy resident cap used by eviction policy (typically effective top-k). */
    uint32_t experts_resident_cap = 0;
    /** Batch counter when @ref evaluate_topology last completed (0 = never yet). */
    uint64_t last_betti_eval_batch = 0;
    /** GF(3) expert linear layers: cumulative Hebbian weight-cell update steps (non-zero delta applied). */
    uint64_t gf3_hebbian_weight_cell_updates = 0;
    
    // MoE metrics (GF(3) - tropical integers)
    uint32_t moe_load_balance_score = 0;        // Tropical goodness score
    uint32_t avg_routing_latency_ms = 0;        // Integer milliseconds
    std::vector<uint32_t> expert_utilization;   // Last batch: per-expert route counts
    std::vector<int32_t> expert_deltas;
    /** True if the last completed training batch used packed TritPack5 input for Top-K (vs dense trits). */
    bool used_tritpack5_input_route = false;
    /** True if symplectic routing logits used SYCL on the last completed batch (USE_SYCL builds only). */
    bool router_sycl_path_used = false;
    
    // Betti numbers (topology analysis)
    uint32_t betti_beta_0 = 0;  // Connected components
    uint32_t betti_beta_1 = 0;  // 1-cycles
    uint32_t betti_beta_2 = 0;  // 2-voids
    int32_t euler_characteristic = 0;
    
    // Graph state
    size_t graph_nodes = 0;
    size_t graph_edges = 0;
    std::string graph_topology = "unknown";
    
    // Data Synthesizer stats
    size_t ds_total_acquired = 0;
    size_t ds_total_perturbed = 0;
    size_t ds_api_failures = 0;
    size_t ds_queue_depth = 0;
    size_t ds_raw_queue_depth = 0;
    size_t ds_raw_queue_max = 0;
    size_t ds_train_queue_max = 0;
    uint64_t ds_blocked_raw_pushes = 0;
    uint64_t ds_blocked_train_pushes = 0;
    uint64_t ds_blocked_wait_ms = 0;
    uint64_t ds_dropped_payloads = 0;
    uint64_t ds_dropped_payload_string_view = 0;
    uint64_t ds_dropped_payload_other = 0;
    size_t ds_acquisition_queue_depth = 0;
    size_t ds_acquisition_queue_max = 0;
    uint64_t ds_acquisition_blocked_pushes = 0;
    uint64_t ds_acquisition_blocked_wait_ms = 0;
    uint64_t ds_acquisition_dropped_too_short = 0;
    size_t ds_topic_frontier_size = 0;
    size_t ds_topic_frontier_max = 0;
    uint64_t ds_topic_frontier_evictions = 0;
    size_t prefill_target_samples = 0;
    /** Raw payloads waiting + contrastive pairs ready (train queue holds 2 entries per pair). */
    size_t prefill_current_samples = 0;
    bool prefill_reached = false;
    uint32_t prefill_timeout_ms = 0;
    
    // Pipeline state
    uint64_t current_epoch = 0;
    uint64_t current_batch = 0;
    uint64_t samples_processed = 0;  // Contrastive rows in current epoch (resets each epoch)
    uint64_t samples_processed_total = 0;  // Cumulative contrastive rows across run
    uint32_t training_progress = 0;  // Basis points (0-10000 = 0.00%-100.00%)
    bool is_running = false;
    std::string status_message;
    
    // Continuous mode tracking
    uint32_t loop_count = 0;  // Number of completed loops in continuous mode

    /** Live concurrent process_batch workers (runtime tuning); 0 if not reported. */
    size_t live_parallel_batches = 0;
    /** TOML ceiling from InitSession (config.training_parallel_batches). */
    size_t parallel_batches_ceiling = 0;
};

/**
 * @brief Configuration for the autonomous training pipeline (plain aggregate: no inline defaults).
 * Production runs: host loads TOML and passes Training_InitSession. Bootstrapping/tests: default_pipeline_config().
 */
struct PipelineConfig {
    size_t acquisition_threads;
    size_t perturbation_threads;

    size_t ff_num_layers;
    size_t ff_layer_width;
    uint32_t ff_learning_rate_step;
    float learning_rate;

    size_t moe_num_experts;
    size_t moe_top_k;
    size_t moe_input_dim;
    size_t moe_output_dim;
    size_t moe_hidden_dim;
    size_t moe_expert_internal_layers;
    size_t routing_qutrits;

    size_t graph_initial_nodes;
    size_t graph_initial_edges;
    size_t betti_max_qutrits;
    uint32_t betti_guidance_threshold;
    uint32_t shadow_dim;

    size_t batch_size;
    size_t num_epochs;
    size_t samples_per_epoch;
    size_t training_micro_batch_cap;
    /** Legacy TOML field; kept for checkpoint/session ABI. Does not cap row collect size (see micro_batch_cap + batch_size). */
    size_t training_collect_floor;
    bool training_timing_to_stderr;
    moe::SyclRouteMode training_sycl_route_mode;
    size_t sycl_trit_quant_min_moe_dim;
    uint32_t goodness_log_level;
    size_t topology_evaluation_interval;
    size_t checkpoint_interval;
    bool enable_prefill_ring_buffer;
    size_t prefill_target_samples;
    uint32_t prefill_timeout_ms;
    uint32_t prefill_poll_ms;
    size_t max_acquisition_queue_depth;
    size_t max_raw_queue_depth;
    size_t max_train_queue_depth;

    bool enable_betti_guidance;
    bool enable_knowledge_engine;
    bool enable_checkpoints;
    bool enable_wui_streaming;
    bool enable_continuous_mode;
    bool enable_steane_correction;
    bool enable_error_correction;
    bool enable_flash_cim;

    bool lazy_moe_experts;

    size_t moe_ff_active_internal_layers;

    std::string data_path;
    bool prefer_local_data;
    std::string data_sources_toml_path;
    bool allow_generated_negatives;

    size_t directory_max_lines;
    size_t max_jsonl_local_samples;
    size_t min_text_length;
    size_t max_text_length;

    size_t checkpoint_async_queue_max;
    uint32_t collect_empty_backoff_base_ms;
    uint32_t collect_empty_backoff_max_shift;
    uint32_t collect_empty_backoff_cap_ms;
    uint32_t metrics_heartbeat_sec;

    bool training_parallel_contrastive_rows;
    /** Concurrent `process_batch()` workers per training_loop tick (each host thread uses its own SYCL queue). */
    size_t training_parallel_batches;
    /**
     * Hard ceiling on effective concurrent workers (applied after realtime scaler): min(live, cap).
     * 0 = uncapped (legacy). Non-zero helps iGPU/UMA when training.parallel_batches is large (e.g. 192–512).
     */
    size_t training_parallel_batches_runtime_cap;
    size_t ff_expert_chunk_size;
    size_t target_routes_per_batch;
    uint32_t collect_window_ms;
    /** Do not leave the collect phase on @ref collect_window_ms until at least this many contrastive rows
     *  are buffered (capped by @ref collect_limit). Prevents tiny "calculator" batches when data is available. */
    uint32_t collect_min_rows_per_batch;
    std::string training_checkpoint_data_dir;

    /** SYCL device USM pre-reserve size in GiB before routing-heavy init (0 = disabled). */
    uint32_t sycl_prereserve_gib;
    /**
     * Allocation chunk size (MiB) for each `malloc_device` step while filling @ref sycl_prereserve_gib.
     * Larger chunks reach the target reservation with fewer SYCL calls (better for filling big UMA budgets).
     */
    uint32_t sycl_prereserve_chunk_mib;
    /** SYCL GPU pick when multiple devices exist: -1 = auto (prefer max compute units), else device list index. */
    int32_t sycl_gpu_device_index;
    /** Minimum input×output weight cells before GF(3) layer uses SYCL matmul path (0 = no extra floor). */
    size_t gf3_sycl_min_weight_cells;
    /** When true, emit throttled stderr lines for batched GF3 SYCL `parallel_for` grid sizing (`training.gf3_sycl_submit_grid_log`). */
    bool gf3_sycl_submit_grid_log;
    /** Host staging budget (MiB) used to cap MoE slots per SYCL multi-slot chunk (see gf3_ff_multislot_host_budget_slots_cap). 0 = pipeline default MiB. */
    size_t gf3_ff_batched_weight_mib;
    /** Pack multiple contrastive rows into one SYCL multi-slot FF batch when possible. */
    bool ff_multi_row_batch;
    /** When >=2, cap MoE slots per SYCL multi-slot FF chunk. When 0, chunk size is
     *  min(all slots, gf3_ff_multislot_host_budget_slots_cap(.., gf3_ff_batched_weight_mib)) so large
     *  training.gf3_ff_batched_weight_mib raises the implicit cap instead of tiny host-staging chunks. */
    uint32_t ff_multi_row_slots_chunk;
    /** Extra ceiling on slots per multi-row SYCL FF chunk (0 = off). Applied after host budget and @ref ff_multi_row_slots_chunk. */
    size_t gf3_ff_multislot_slots_chunk_max;
    /** When true, apply a modest (5/4) uplift to the MiB-derived slot cap — does not discard the cap (staging is
     *  tensor-sized, not “fill VRAM”). Use @ref gf3_ff_multislot_slots_chunk_max / @ref ff_multi_row_slots_chunk to hard-cap. */
    bool gf3_ff_multislot_ignore_host_slot_budget;
    /** Max layers to process per SYCL multi-slot FF batch (0 = process all active layers). 
     *  Reduces per-kernel memory pressure while keeping high slot parallelism. 
     *  Example: 80 layers with gf3_ff_layers_per_batch=4 processes as 20 batches of 4 layers each. */
    uint32_t gf3_ff_layers_per_batch;
    /** Lazy MoE: max resident experts in RAM (0 = heuristic from top-k). */
    size_t lazy_moe_resident_cap;

    /** Prefill: stderr progress log cadence (seconds). */
    uint32_t prefill_progress_log_interval_sec;
    /** Prefill: emit stall warning after this many seconds without depth growth. */
    uint32_t prefill_stall_warn_sec;
    /** Collect phase: max wall-clock wait for the first contrastive pair (seconds). */
    uint32_t collect_first_sample_timeout_sec;
    /** SYCL multi-row collect: sleep between coalesce spins (microseconds). */
    uint32_t collect_ff_coalesce_sleep_us;
    /** Training loop: pause polling interval (milliseconds). */
    uint32_t training_pause_poll_ms;
    /** Training loop: sleep when no samples after empty `process_batch` (milliseconds). */
    uint32_t training_idle_retry_ms;
    /** Log “waiting for samples” every N consecutive empty batches (>=1). */
    size_t training_empty_batch_log_interval;
    /** Push metrics on idle cadence every N consecutive empty batches (>=1). */
    size_t training_empty_batch_metrics_interval;
    /** Contrastive train queue resync: discard budget slack vs queue depth (see DataSynthesizer). */
    size_t train_queue_resync_discard_slack;

    /** When true, `start_training` may auto-import the newest compatible checkpoint before the first tick.
     *  When false, every run starts from InitSession weights/policy only (no silent restore). Not stored in checkpoint blobs. */
    bool training_auto_resume_from_checkpoint;
};

/**
 * Built-in defaults for callers that do not use InitSession (WASM create, unit tests).
 * Source of truth: config/pipeline_defaults.toml → generated C++ in
 * autonomous_training_pipeline_defaults.gen.cpp (scripts/gen_pipeline_default_config.py).
 */
PipelineConfig default_pipeline_config();

/**
 * @brief Training pipeline states
 */
enum class PipelineState {
    IDLE,                   // Not initialized
    INITIALIZING,          // Setting up components
    READY,                 // Ready to start
    ACQUIRING_DATA,       // DataSynthesizer active
    TRAINING,             // Forward-Forward training
    EVALUATING_TOPOLOGY,  // Betti number computation
    OPTIMIZING_GRAPH,     // Graph topology update
    CHECKPOINTING,        // Saving state
    PAUSED,               // Training paused
    STOPPING,             // Graceful shutdown
    COMPLETE,              // Training finished
    FAILED                 // Error state (renamed to avoid Windows ERROR macro)
};

/**
 * @brief Autonomous Training Pipeline
 * 
 * Integrates DataSynthesizer, Forward-Forward training, 243-expert MoE,
 * and BettiExtractor-based quantum graph topology into a unified pipeline.
 * 
 * Features:
 * - Continuous data acquisition from knowledge engines (Wolfram, PubChem, OEIS)
 * - Forward-Forward layer-wise training with tropical goodness
 * - 243-expert MoE with GF(3) layers and Hebbian updates
 * - Betti-guided topology optimization via stabilizer codes
 * - Real-time WUI control and monitoring via MCP API
 * 
 * Usage:
 *   AutonomousTrainingPipeline pipeline;
 *   PipelineConfig config = default_pipeline_config(); // or load from host/TOML
 *   config.moe_num_experts = 243;
 *   pipeline.initialize(config);
 *   pipeline.start_training();
 *   auto metrics = pipeline.get_metrics();
 */
class AutonomousTrainingPipeline {
public:
    AutonomousTrainingPipeline();
    ~AutonomousTrainingPipeline();
    
    // Non-copyable
    AutonomousTrainingPipeline(const AutonomousTrainingPipeline&) = delete;
    AutonomousTrainingPipeline& operator=(const AutonomousTrainingPipeline&) = delete;
    
    /**
     * @brief Initialize all pipeline components
     * @param config Pipeline configuration
     * @return true if initialization successful
     */
    bool initialize(const PipelineConfig& config);
    
    /**
     * @brief Start the training loop
     * @return true if started successfully
     */
    bool start_training();
    
    /**
     * @brief Stop the training loop gracefully
     */
    void stop_training();
    
    /**
     * @brief Pause training (can be resumed)
     */
    void pause_training();
    
    /**
     * @brief Resume paused training
     */
    void resume_training();
    
    /**
     * @brief Get current pipeline state
     */
    PipelineState get_state() const { return state_.load(); }
    
    /**
     * @brief Get current metrics from all subsystems
     */
    PipelineMetrics get_metrics() const;
    
    /**
     * @brief Export trained model to file
     * @param path Export file path
     * @return true if export successful
     */
    bool export_model(const std::string& path);
    
    /**
     * @brief Import model from file
     * @param path Import file path
     * @param merge_runtime_from When non-null (e.g. auto-resume), copy host/throughput knobs from this
     *        session config over the checkpoint blob so stale ff_multi_row_slots_chunk / micro_batch_cap
     *        from older runs do not override current TOML.
     * @return true if import successful
     */
    bool import_model(const std::string& path, const PipelineConfig* merge_runtime_from = nullptr);

    /** Update paths/epoch budget without rebuilding MoE stacks (used between training runs). */
    void apply_run_overrides(const std::string& data_path, uint32_t num_epochs);
    
    /**
     * @brief Get current configuration
     */
    PipelineConfig get_config() const { return config_; }
    
    /**
     * @brief Update configuration (only when paused or idle)
     * @param config New configuration
     * @return true if updated successfully
     */
    bool update_config(const PipelineConfig& config);

    /**
     * Optional start count consumed once inside initialize(): clamp to [1, ceiling].
     * Call after Training_InitSession, before Training_StartTraining. 0 = begin at full ceiling.
     */
    void set_pending_live_parallel_start(size_t pb) noexcept;

    /** Hot path: clamp pb to [1, parallel_batches_ceiling]. Safe while TRAINING. */
    bool set_live_parallel_batches(size_t pb) noexcept;

    size_t effective_parallel_batches() const noexcept;
    size_t live_parallel_batches_value() const noexcept;
    size_t parallel_batches_ceiling_value() const noexcept;
    
    /**
     * @brief Force immediate topology evaluation
     */
    void trigger_topology_evaluation();
    
    /**
     * @brief Apply Betti-guided graph optimization
     */
    void apply_betti_guidance();
    
    /**
     * @brief Get access to internal components (for advanced use)
     */
    DataSynthesizer* get_synthesizer() const { return synthesizer_.get(); }
    learning::ForwardForwardLearner* get_ff_learner() const { return ff_learner_.get(); }
    moe::MoERouter* get_router() const { return router_.get(); }
    qgnn::BettiExtractor* get_betti_extractor() const { return betti_extractor_.get(); }
    qgnn::GraphTableau* get_graph_tableau() const { return graph_tableau_.get(); }

    /**
     * @brief Register callback for state changes
     */
    void on_state_change(std::function<void(PipelineState, PipelineState)> callback);
    
    /**
     * @brief Register callback for metrics updates
     */
    void on_metrics_update(std::function<void(const PipelineMetrics&)> callback);

private:
    // Configuration
    PipelineConfig config_;
    
    // Components
    std::unique_ptr<DataSynthesizer> synthesizer_;
    std::unique_ptr<learning::ForwardForwardLearner> ff_learner_;
    std::unique_ptr<moe::MoERouter> router_;
    std::vector<std::unique_ptr<moe::GF3MultiLayerExpert>> experts_;
    moe::ExpertNetwork::ExpertConfig expert_template_{};
    mutable std::mutex experts_mutex_;
    /** Serialize MoE routing when multiple training rows run in parallel (router has per-call diagnostics state). */
    std::mutex router_route_mutex_;
    /** One mutex per expert index: concurrent rows may route to the same expert; FF weight updates must not race. */
    std::vector<std::unique_ptr<std::mutex>> expert_ff_mutexes_;
    /** Heartbeat / partial timing lines when training rows in parallel. */
    std::mutex parallel_row_heartbeat_mutex_;
    /** Evicted lazy experts persisted as serialized GF3 weights (key = expert index). */
    std::unordered_map<size_t, std::string> evicted_expert_weights_;
    /** Monotonic touch generation for LRU eviction among resident experts. */
    std::vector<uint64_t> expert_touch_generation_;
    /** In-flight usage pins per expert index; pinned experts are never evicted. */
    std::vector<uint32_t> expert_active_pin_counts_;
    /** Total eviction candidates skipped because they were inside cooldown window. */
    std::atomic<uint64_t> expert_eviction_cooldown_skips_total_{0};
    /** Total resident expert evictions performed by lazy spill policy. */
    std::atomic<uint64_t> expert_evictions_total_{0};
    uint64_t expert_touch_clock_{1};
    size_t expert_resident_count_{0};
    std::unique_ptr<qgnn::BettiExtractor> betti_extractor_;
    std::unique_ptr<qgnn::GraphTableau> graph_tableau_;
    
    // State
    std::atomic<PipelineState> state_{PipelineState::IDLE};
    std::atomic<bool> stop_requested_{false};
    std::atomic<bool> pause_requested_{false};
    
    // Training state
    std::atomic<uint64_t> current_epoch_{0};
    std::atomic<uint64_t> current_batch_{0};
    std::atomic<uint64_t> samples_processed_{0};  // Contrastive rows / epoch (see process_batch return)
    std::atomic<uint64_t> samples_processed_total_{0};  // Cumulative contrastive rows
    std::atomic<uint32_t> loop_count_{0};  // Continuous mode loop counter
    size_t consecutive_empty_batches_{0};
    std::atomic<bool> batch_inflight_active_{false};
    std::atomic<uint32_t> batch_inflight_done_{0};
    std::atomic<uint32_t> batch_inflight_target_{0};
    /** Contrastive rows finished in the current in-flight process_batch (multi-row SYCL counts per chunk). */
    std::atomic<uint32_t> intrabatch_contrastive_rows_done_{0};
    std::atomic<bool> pipeline_collect_hint_logged_{false};
    std::atomic<uint64_t> last_checkpoint_batch_{0};
    /** First `process_batch()` call only: stderr phase markers; cleared when the call returns (any path). */
    std::atomic<bool> trace_first_process_batch_phases_{true};

    /** Background disk writes for automatic checkpoints (bounded queue; blocks producer if full). */
    std::once_flag checkpoint_writer_once_;
    std::thread checkpoint_writer_thread_;
    std::mutex checkpoint_queue_mutex_;
    std::condition_variable checkpoint_queue_cv_;
    std::condition_variable checkpoint_queue_slots_cv_;
    std::deque<std::string> checkpoint_queue_;
    std::atomic<bool> checkpoint_writer_stop_{false};

    // Threading
    std::thread training_thread_;
    mutable std::mutex metrics_mutex_;
    mutable std::mutex config_mutex_;

    /** Runtime concurrent process_batch workers (<= config_.training_parallel_batches ceiling). */
    mutable std::atomic<size_t> live_parallel_batches_{1};
    size_t parallel_batches_ceiling_{1};
    std::atomic<size_t> pending_live_parallel_start_{0};
    /** Until the first tick trains >0 rows, force parallel_batches=1 (SYCL/device warm-up). First tick caps
     *  collected contrastive rows so concurrent parallel_batches waves begin quickly (see process_batch). */
    
    // Callbacks
    std::function<void(PipelineState, PipelineState)> state_callback_;
    std::function<void(const PipelineMetrics&)> metrics_callback_;
    std::mutex callback_mutex_;

    // Cached metrics
    mutable PipelineMetrics cached_metrics_;
    
    // Private methods
    void set_state(PipelineState new_state);
    void training_loop();
    bool initialize_components();
    void shutdown_components();
    /** @param wave_parallel_batches concurrent workers in this training_loop iteration (not live atomic reads). */
    size_t process_batch(size_t wave_parallel_batches);
    bool evaluate_topology();
    bool optimize_graph_topology();
    void update_metrics();
    void emit_metrics();
    void checkpoint_if_needed();
    /** Serialize checkpoint while holding expert/router locks (same bytes as @ref export_model). */
    bool serialize_checkpoint_blob(std::ostream& os);
    void ensure_checkpoint_writer_started();
    void checkpoint_writer_loop();
    void enqueue_checkpoint_job(std::string path);
    void shutdown_checkpoint_writer();

    // Betti guidance
    qgnn::BettiExtractor::SimplicialComplex build_simplicial_complex();
    void adjust_topology_based_on_betti(const qgnn::BettiExtractor::BettiNumbers& betti);
    
    // Helper functions
    TernaryRouteInput extract_ternary_route_input(const TrainingSample& sample);
    std::vector<ternary::Trit> generate_negative_sample(const std::vector<ternary::Trit>& positive);
    
    std::vector<std::unique_ptr<moe::GF3MultiLayerExpert>> create_experts(
        size_t num_experts,
        const moe::ExpertNetwork::ExpertConfig& expert_config);

    /** Materialize GF3 expert at index when lazy_moe_experts; thread-safe. */
    moe::GF3MultiLayerExpert* ensure_expert(size_t expert_idx, bool pin_for_use = false);
    /** Release one in-flight pin for an expert index (no-op if already 0). */
    void release_expert_pin(size_t expert_idx);
    /** Count experts currently pinned for in-flight use. */
    uint32_t pinned_expert_count() const;

    /** Aggregated FF output from one contrastive row (merged into batch under lock). */
    struct RowTrainMerge {
        int64_t route_ms = 0;
        int64_t ff_ms = 0;
        uint32_t neg_prepared_inc = 0;
        uint32_t neg_missing_inc = 0;
        bool used_tritpack5_input_route = false;
        bool router_sycl_path = false;
        std::vector<size_t> route_expert_idx;
        std::vector<uint32_t> pos_good;
        std::vector<uint32_t> neg_good;
    };

    /** One contrastive row collected for @ref process_batch (positive + optional prepared negative). */
    struct CollectedContrastiveRow {
        TrainingSample positive;
        std::optional<TrainingSample> prepared_negative;
    };

    /**
     * Route all rows, pack MoE slots across rows, run one SYCL multi-slot FF batch when possible.
     * @return true if training ran (merges filled); false to fall back to per-row training (or SYCL off).
     * @param fatal_message set on hard errors (same as single-row fatal).
     */
    bool try_ff_multi_row_slot_batch_(
        const std::vector<CollectedContrastiveRow>& batch_work,
        bool kTiming,
        size_t effective_top_k,
        size_t wave_parallel_batches,
        std::vector<RowTrainMerge>& out_merges,
        int64_t& out_ff_ms_total,
        std::string& fatal_message);

    /** Calls @ref try_ff_multi_row_slot_batch_; on MSVC wraps SEH and sets @p fatal_message (no silent fallback). */
    bool try_ff_multi_row_slot_batch_invoke_(
        const std::vector<CollectedContrastiveRow>& batch_work,
        bool kTiming,
        size_t effective_top_k,
        size_t wave_parallel_batches,
        std::vector<RowTrainMerge>& out_merges,
        int64_t& out_ff_ms_total,
        std::string& fatal_message);

    /**
     * Route + FF for one contrastive row. Thread-safe vs other rows (per-expert locks, router lock).
     * @return 0 success (merge filled), 1 skipped (no prepared negative), 2 fatal (fatal_message set).
     */
    int train_contrastive_row_impl(
        size_t row_index,
        const TrainingSample& positive,
        const std::optional<TrainingSample>& prepared_negative,
        bool trace_phases,
        bool kTiming,
        unsigned goodness_log_level,
        size_t effective_top_k,
        RowTrainMerge& merge,
        std::string& fatal_message
    );
    /** Max resident expert objects in lazy mode (defaults to ~16× effective top_k, not top_k alone). */
    size_t lazy_resident_expert_cap() const;
    /** Mark expert as recently used for lazy LRU eviction policy. */
    void touch_expert_locked(size_t expert_idx);
    /** Evict one resident lazy expert (except @p protected_idx) to serialized spill map. */
    bool spill_one_expert_locked(size_t protected_idx);
    
    static std::string state_to_string(PipelineState state);
};

/**
 * @brief Factory: new pipeline only; caller must initialize(config) then start_training().
 */
std::unique_ptr<AutonomousTrainingPipeline> create_training_pipeline();

} // namespace q_mini_wasm_v2::core::training
