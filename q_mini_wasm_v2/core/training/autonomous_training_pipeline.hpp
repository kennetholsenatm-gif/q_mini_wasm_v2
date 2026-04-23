#pragma once

#include <cstdint>
#include <vector>
#include <memory>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <string>

#include "data_synthesizer.hpp"
#include "../learning/forward_forward.hpp"
#include "../moe/router.hpp"
#include "../moe/expert_network.hpp"
#include "../qgnn/betti_extractor.hpp"
#include "../qgnn/graph_tableau.hpp"

namespace q_mini_wasm_v2::core::training {

/**
 * @brief Comprehensive training metrics from all pipeline stages
 */
struct PipelineMetrics {
    // Forward-Forward metrics
    uint32_t ff_positive_goodness = 0;
    uint32_t ff_negative_goodness = 0;
    int32_t ff_goodness_delta = 0;
    uint64_t ff_total_train_calls = 0;
    
    // MoE metrics (GF(3) - tropical integers)
    uint32_t moe_load_balance_score = 0;        // Tropical goodness score
    uint32_t avg_routing_latency_ms = 0;        // Integer milliseconds
    std::vector<uint32_t> expert_utilization;   // Tropical utilization counts
    std::vector<int32_t> expert_deltas;
    
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
    
    // Pipeline state
    uint64_t current_epoch = 0;
    uint64_t current_batch = 0;
    uint64_t samples_processed = 0;  // Total samples processed in current epoch
    uint32_t training_progress = 0;  // Basis points (0-10000 = 0.00%-100.00%)
    bool is_running = false;
    std::string status_message;
    
    // Continuous mode tracking
    uint32_t loop_count = 0;  // Number of completed loops in continuous mode
};

/**
 * @brief Configuration for the autonomous training pipeline
 */
struct PipelineConfig {
    // Data Synthesizer settings
    size_t acquisition_threads = 4;
    size_t perturbation_threads = 2;
    
    // Forward-Forward settings
    size_t ff_num_layers = 3;
    size_t ff_layer_width = 128;
    uint32_t ff_learning_rate_step = 1;  // GF(3) learning step size (no float)
    float learning_rate = 0.001f;  // Standard learning rate
    
    // MoE settings
    size_t moe_num_experts = 243;
    size_t moe_top_k = 3;
    size_t moe_input_dim = 64;
    size_t moe_output_dim = 64;
    size_t moe_hidden_dim = 128;
    /** Layers inside each expert network (Forward–Forward stack depth). */
    size_t moe_expert_internal_layers = 2;
    /** MoE router qutrit width; must match TOML model.routing_qutrits. */
    size_t routing_qutrits = 16;
    
    // Betti/Graph settings
    size_t graph_initial_nodes = 64;
    size_t graph_initial_edges = 112;
    size_t betti_max_qutrits = 243;
    uint32_t betti_guidance_threshold = 15;  // β₁ threshold for optimization
    uint32_t shadow_dim = 64;  // Shadow dimension for stabilizer
    
    // Training loop settings
    size_t batch_size = 32;
    size_t num_epochs = 100;
    size_t samples_per_epoch = 1000;  // samples to process per epoch
    size_t topology_evaluation_interval = 10;  // batches between Betti analysis
    /** Checkpoints taken when current_epoch % checkpoint_interval == 0 (after epoch completes). */
    size_t checkpoint_interval = 10;
    
    // Control flags
    bool enable_betti_guidance = true;
    bool enable_knowledge_engine = true;
    bool enable_checkpoints = true;
    bool enable_wui_streaming = true;
    bool enable_continuous_mode = false;  // Auto-restart when epoch limit reached
    bool enable_steane_correction = false;  // Steane error correction
    bool enable_error_correction = false;   // Alias for steane correction
    bool enable_flash_cim = false;         // Flash CIM optimization
    
    // Data source - if set, load local data instead of external APIs
    std::string data_path;  // Path to local training data files
    bool prefer_local_data = true;  // Use local data if available, fall back to APIs
    /** Absolute or CWD-relative path to data_sources.toml (host should pass absolute). */
    std::string data_sources_toml_path;
};

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
 *   PipelineConfig config;
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
    bool export_model(const std::string& path) const;
    
    /**
     * @brief Import model from file
     * @param path Import file path
     * @return true if import successful
     */
    bool import_model(const std::string& path);
    
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
    std::vector<std::unique_ptr<moe::ExpertNetwork>> experts_;
    std::unique_ptr<qgnn::BettiExtractor> betti_extractor_;
    std::unique_ptr<qgnn::GraphTableau> graph_tableau_;
    
    // State
    std::atomic<PipelineState> state_{PipelineState::IDLE};
    std::atomic<bool> stop_requested_{false};
    std::atomic<bool> pause_requested_{false};
    
    // Training state
    std::atomic<uint64_t> current_epoch_{0};
    std::atomic<uint64_t> current_batch_{0};
    std::atomic<uint64_t> samples_processed_{0};  // Samples processed in current epoch
    std::atomic<uint32_t> loop_count_{0};  // Continuous mode loop counter
    size_t consecutive_empty_batches_{0};
    
    // Threading
    std::thread training_thread_;
    mutable std::mutex metrics_mutex_;
    mutable std::mutex config_mutex_;
    
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
    size_t process_batch();
    bool evaluate_topology();
    bool optimize_graph_topology();
    void update_metrics();
    void emit_metrics();
    void checkpoint_if_needed();
    
    // Betti guidance
    qgnn::BettiExtractor::SimplicialComplex build_simplicial_complex();
    void adjust_topology_based_on_betti(const qgnn::BettiExtractor::BettiNumbers& betti);
    
    // Helper functions
    std::vector<ternary::Trit> extract_ternary_vector(const TrainingSample& sample);
    std::vector<ternary::Trit> generate_negative_sample(const std::vector<ternary::Trit>& positive);
    
    std::vector<std::unique_ptr<moe::ExpertNetwork>> create_experts(
        size_t num_experts, 
        const moe::ExpertNetwork::ExpertConfig& expert_config
    );
    
    static std::string state_to_string(PipelineState state);
};

/**
 * @brief Factory: new pipeline only; caller must initialize(config) then start_training().
 */
std::unique_ptr<AutonomousTrainingPipeline> create_training_pipeline();

} // namespace q_mini_wasm_v2::core::training
