#include "autonomous_training_pipeline.hpp"
#include "../moe/gf3_layers.hpp"
#include <fstream>
#include <iostream>
#include <algorithm>
#include <numeric>

namespace q_mini_wasm_v2::core::training {

AutonomousTrainingPipeline::AutonomousTrainingPipeline() = default;

AutonomousTrainingPipeline::~AutonomousTrainingPipeline() {
    if (state_.load() == PipelineState::TRAINING || 
        state_.load() == PipelineState::ACQUIRING_DATA) {
        stop_training();
    }
}

bool AutonomousTrainingPipeline::initialize(const PipelineConfig& config) {
    std::lock_guard<std::mutex> lock(config_mutex_);
    
    set_state(PipelineState::INITIALIZING);
    config_ = config;
    
    if (!initialize_components()) {
        set_state(PipelineState::ERROR);
        return false;
    }
    
    set_state(PipelineState::READY);
    return true;
}

bool AutonomousTrainingPipeline::initialize_components() {
    // Initialize DataSynthesizer
    synthesizer_ = std::make_unique<DataSynthesizer>();
    synthesizer_->initialize_apis();
    
    // Initialize Forward-Forward Learner
    learning::FFConfig ff_config;
    ff_config.num_layers = config_.ff_num_layers;
    ff_config.layer_width = config_.ff_layer_width;
    ff_config.learning_rate = config_.ff_learning_rate;
    ff_learner_ = std::make_unique<learning::ForwardForwardLearner>(ff_config);
    
    // Initialize MoE Router
    moe::MoEConfig router_config;
    router_config.total_experts = config_.moe_num_experts;
    router_config.top_k = config_.moe_top_k;
    router_config.input_dim = config_.moe_input_dim;
    router_ = std::make_unique<moe::MoERouter>(router_config);
    
    // Create experts
    moe::ExpertNetwork::ExpertConfig expert_config;
    expert_config.input_dim = config_.moe_input_dim;
    expert_config.output_dim = config_.moe_output_dim;
    expert_config.hidden_dim = config_.moe_hidden_dim;
    expert_config.num_layers = 2;
    
    experts_ = create_experts(config_.moe_num_experts, expert_config);
    
    // Register experts with router
    for (size_t i = 0; i < experts_.size(); ++i) {
        router_->RegisterExpert(i, experts_[i].get());
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
    PipelineState expected = PipelineState::READY;
    if (!state_.compare_exchange_strong(expected, PipelineState::ACQUIRING_DATA)) {
        return false;  // Not in READY state
    }
    
    stop_requested_ = false;
    pause_requested_ = false;
    current_epoch_ = 0;
    current_batch_ = 0;
    
    // Start DataSynthesizer
    synthesizer_->start(config_.acquisition_threads, config_.perturbation_threads);
    
    // Start training thread
    training_thread_ = std::thread(&AutonomousTrainingPipeline::training_loop, this);
    
    return true;
}

void AutonomousTrainingPipeline::stop_training() {
    stop_requested_ = true;
    
    if (training_thread_.joinable()) {
        training_thread_.join();
    }
    
    shutdown_components();
    set_state(PipelineState::IDLE);
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
        
        // Process batch
        if (!process_batch()) {
            // Error in batch processing
            set_state(PipelineState::ERROR);
            break;
        }
        
        // Update batch counter
        ++current_batch_;
        
        // Topology evaluation
        if (config_.enable_betti_guidance && 
            current_batch_ % config_.topology_evaluation_interval == 0) {
            evaluate_topology();
        }
        
        // Checkpoint
        if (config_.enable_checkpoints && 
            current_batch_ % config_.checkpoint_interval == 0) {
            checkpoint_if_needed();
        }
        
        // Update and emit metrics
        update_metrics();
        if (config_.enable_wui_streaming) {
            emit_metrics();
        }
        
        // Check for epoch completion
        if (current_batch_ >= config_.num_epochs * (config_.batch_size > 0 ? 100 : 1)) {
            ++current_epoch_;
            current_batch_ = 0;
            
            if (current_epoch_ >= config_.num_epochs) {
                set_state(PipelineState::COMPLETE);
                break;
            }
        }
        
        // Small yield to prevent CPU spinning
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    
    if (state_.load() != PipelineState::COMPLETE && 
        state_.load() != PipelineState::ERROR) {
        set_state(PipelineState::STOPPING);
    }
}

bool AutonomousTrainingPipeline::process_batch() {
    // Get samples from DataSynthesizer
    std::vector<TrainingSample> batch_samples;
    batch_samples.reserve(config_.batch_size);
    
    for (size_t i = 0; i < config_.batch_size; ++i) {
        if (synthesizer_->has_sample()) {
            batch_samples.push_back(synthesizer_->get_sample());
        } else {
            // Wait for samples
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            --i;  // Retry
        }
    }
    
    if (batch_samples.empty()) {
        return true;  // No data yet, not an error
    }
    
    // Process each sample through Forward-Forward and MoE
    for (const auto& sample : batch_samples) {
        // Extract ternary vector from sample
        std::vector<ternary::Trit> input_vector;
        // TODO: Convert sample.data to ternary vector
        
        // Route to experts
        // TODO: Implement routing and training
    }
    
    return true;
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
    
    // TODO: Extract edges from graph_tableau state
    // For now, create placeholder edges
    size_t num_edges = std::min(config_.graph_initial_edges, nodes * (nodes - 1) / 2);
    complex.edges.reserve(num_edges);
    
    return complex;
}

void AutonomousTrainingPipeline::adjust_topology_based_on_betti(
    const qgnn::BettiExtractor::BettiNumbers& betti
) {
    set_state(PipelineState::OPTIMIZING_GRAPH);
    
    // High β₁ indicates many cycles - simplify topology
    if (betti.beta_1 > config_.betti_guidance_threshold * 2) {
        // Reduce edges to break cycles
        // TODO: Implement edge removal strategy
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
    
    // Calculate progress
    if (config_.num_epochs > 0) {
        float epoch_progress = static_cast<float>(current_epoch_) / config_.num_epochs * 100.0f;
        metrics.training_progress = epoch_progress;
    }
    
    // Add status message
    metrics.status_message = state_to_string(state_.load());
    
    // Get DataSynthesizer stats
    if (synthesizer_) {
        auto ds_stats = synthesizer_->get_stats();
        metrics.ds_total_acquired = ds_stats.total_acquired;
        metrics.ds_total_perturbed = ds_stats.total_perturbed;
        metrics.ds_api_failures = ds_stats.api_failures;
        metrics.ds_queue_depth = ds_stats.queue_depth;
    }
    
    return metrics;
}

void AutonomousTrainingPipeline::update_metrics() {
    // Update internal metrics cache
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    
    // Graph state
    if (graph_tableau_) {
        cached_metrics_.graph_nodes = graph_tableau_->num_qutrits();
        cached_metrics_.graph_topology = "graph_tableau";
    }
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
            std::to_string(current_batch_) + ".bin";
        export_model(checkpoint_path);
    }
}

bool AutonomousTrainingPipeline::export_model(const std::string& path) const {
    // TODO: Implement model serialization
    // Export expert weights, router state, graph topology
    return true;
}

bool AutonomousTrainingPipeline::import_model(const std::string& path) {
    // TODO: Implement model deserialization
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
        case PipelineState::ERROR: return "error";
        default: return "unknown";
    }
}

std::unique_ptr<AutonomousTrainingPipeline> create_training_pipeline(
    const PipelineConfig& config
) {
    auto pipeline = std::make_unique<AutonomousTrainingPipeline>();
    if (!pipeline->initialize(config)) {
        return nullptr;
    }
    return pipeline;
}

} // namespace q_mini_wasm_v2::core::training
