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
    
    // Batch-level metrics accumulation
    float batch_pos_goodness = 0.0f;
    float batch_neg_goodness = 0.0f;
    float batch_delta = 0.0f;
    size_t total_routes = 0;
    std::vector<uint32_t> expert_counts(config_.moe_num_experts, 0);
    std::vector<int32_t> expert_deltas(config_.moe_num_experts, 0);
    
    // Process each sample through Forward-Forward and MoE
    for (const auto& sample : batch_samples) {
        // Extract ternary vector from sample
        std::vector<ternary::Trit> positive_sample = extract_ternary_vector(sample);
        
        if (positive_sample.empty()) {
            continue;  // Skip invalid samples
        }
        
        // Generate negative sample via corruption
        std::vector<ternary::Trit> negative_sample = generate_negative_sample(positive_sample);
        
        // Route positive sample to experts via MoE router
        auto selected_experts = router_->route_topk(positive_sample);
        
        // Train each selected expert with Forward-Forward
        for (size_t expert_idx : selected_experts) {
            if (expert_idx >= experts_.size() || !experts_[expert_idx]) {
                continue;
            }
            
            // Train this expert
            int32_t delta = experts_[expert_idx]->TrainForwardForward(
                positive_sample, 
                negative_sample
            );
            
            // Compute goodness for metrics
            auto pos_output = experts_[expert_idx]->Forward(positive_sample);
            auto neg_output = experts_[expert_idx]->Forward(negative_sample);
            
            uint32_t pos_goodness = experts_[expert_idx]->ComputeGoodness(pos_output);
            uint32_t neg_goodness = experts_[expert_idx]->ComputeGoodness(neg_output);
            
            // Accumulate metrics
            batch_pos_goodness += static_cast<float>(pos_goodness);
            batch_neg_goodness += static_cast<float>(neg_goodness);
            batch_delta += static_cast<float>(delta);
            expert_counts[expert_idx]++;
            expert_deltas[expert_idx] += delta;
            total_routes++;
        }
    }
    
    // Update cached metrics
    if (total_routes > 0) {
        std::lock_guard<std::mutex> lock(metrics_mutex_);
        cached_metrics_.ff_positive_goodness = static_cast<uint32_t>(batch_pos_goodness / total_routes);
        cached_metrics_.ff_negative_goodness = static_cast<uint32_t>(batch_neg_goodness / total_routes);
        cached_metrics_.ff_goodness_delta = static_cast<int32_t>(batch_delta / total_routes);
        cached_metrics_.ff_total_train_calls += total_routes;
        cached_metrics_.expert_utilization.resize(config_.moe_num_experts);
        cached_metrics_.expert_deltas = expert_deltas;
        
        // Compute utilization percentages
        for (size_t i = 0; i < config_.moe_num_experts; ++i) {
            cached_metrics_.expert_utilization[i] = static_cast<float>(expert_counts[i]) / total_routes;
        }
    }
    
    return true;
}

/**
 * @brief Extract ternary vector from training sample
 */
std::vector<ternary::Trit> AutonomousTrainingPipeline::extract_ternary_vector(
    const TrainingSample& sample
) {
    std::vector<ternary::Trit> result;
    
    // Handle different payload types from ApiPayload variant
    std::visit([&result, this](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;
        
        if constexpr (std::is_same_v<T, std::vector<ternary::Trit>>) {
            // Already ternary - direct use
            result = arg;
        }
        else if constexpr (std::is_same_v<T, std::vector<float>>) {
            // Convert float vector to ternary via quantization
            result.reserve(arg.size());
            for (float val : arg) {
                // Quantize to {-1, 0, 1} based on thresholds
                if (val > 0.33f) {
                    result.push_back(ternary::Trit::POSITIVE);
                } else if (val < -0.33f) {
                    result.push_back(ternary::Trit::NEGATIVE);
                } else {
                    result.push_back(ternary::Trit::ZERO);
                }
            }
        }
        else if constexpr (std::is_same_v<T, std::vector<std::vector<float>>>) {
            // Flatten matrix and convert
            size_t total_elements = 0;
            for (const auto& row : arg) {
                total_elements += row.size();
            }
            result.reserve(std::min(total_elements, config_.moe_input_dim));
            
            size_t count = 0;
            for (const auto& row : arg) {
                for (float val : row) {
                    if (count >= config_.moe_input_dim) break;
                    if (val > 0.33f) {
                        result.push_back(ternary::Trit::POSITIVE);
                    } else if (val < -0.33f) {
                        result.push_back(ternary::Trit::NEGATIVE);
                    } else {
                        result.push_back(ternary::Trit::ZERO);
                    }
                    count++;
                }
                if (count >= config_.moe_input_dim) break;
            }
        }
        else if constexpr (std::is_same_v<T, std::string_view>) {
            // Hash string to ternary vector
            result.reserve(config_.moe_input_dim);
            for (size_t i = 0; i < config_.moe_input_dim; ++i) {
                size_t char_idx = i % arg.size();
                char c = arg[char_idx];
                // Map char value to ternary
                int8_t val = (c % 3) - 1;  // Maps 0,1,2 to -1,0,1
                result.push_back(static_cast<ternary::Trit>(val));
            }
        }
    }, sample.data);
    
    // Pad or truncate to match expected input dimension
    if (result.size() < config_.moe_input_dim) {
        result.resize(config_.moe_input_dim, ternary::Trit::ZERO);
    } else if (result.size() > config_.moe_input_dim) {
        result.resize(config_.moe_input_dim);
    }
    
    return result;
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
    for (const auto& [i, j] : edges) {
        if (i < nodes && j < nodes) {
            complex.add_edge(static_cast<uint32_t>(i), static_cast<uint32_t>(j));
        }
    }
    
    // If no edges found, create initial edges based on config
    if (complex.edges.empty()) {
        size_t num_edges = std::min(config_.graph_initial_edges, nodes * (nodes - 1) / 2);
        for (size_t e = 0, i = 0; e < num_edges && i < nodes; ++i) {
            for (size_t j = i + 1; j < nodes && e < num_edges; ++j, ++e) {
                complex.add_edge(static_cast<uint32_t>(i), static_cast<uint32_t>(j));
                // Actually add to graph_tableau as well
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
    std::lock_guard<std::mutex> lock(config_mutex_);
    
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    
    // Write header
    const char* header = "QMINI_V2";
    file.write(header, 8);
    
    // Write config
    file.write(reinterpret_cast<const char*>(&config_), sizeof(config_));
    
    // Write expert count
    size_t expert_count = experts_.size();
    file.write(reinterpret_cast<const char*>(&expert_count), sizeof(expert_count));
    
    // Write graph state
    size_t graph_nodes = graph_tableau_->num_qutrits();
    file.write(reinterpret_cast<const char*>(&graph_nodes), sizeof(graph_nodes));
    
    // Write training metrics
    auto metrics = get_metrics();
    file.write(reinterpret_cast<const char*>(&metrics.current_epoch), sizeof(metrics.current_epoch));
    file.write(reinterpret_cast<const char*>(&metrics.current_batch), sizeof(metrics.current_batch));
    
    file.close();
    return true;
}

bool AutonomousTrainingPipeline::import_model(const std::string& path) {
    std::lock_guard<std::mutex> lock(config_mutex_);
    
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    
    // Read and verify header
    char header[9] = {0};
    file.read(header, 8);
    if (std::string(header, 8) != "QMINI_V2") {
        return false;
    }
    
    // Read config
    PipelineConfig imported_config;
    file.read(reinterpret_cast<char*>(&imported_config), sizeof(imported_config));
    config_ = imported_config;
    
    // Read expert count
    size_t expert_count;
    file.read(reinterpret_cast<char*>(&expert_count), sizeof(expert_count));
    
    // Read graph state
    size_t graph_nodes;
    file.read(reinterpret_cast<char*>(&graph_nodes), sizeof(graph_nodes));
    
    // Read training progress
    file.read(reinterpret_cast<char*>(&current_epoch_), sizeof(current_epoch_));
    file.read(reinterpret_cast<char*>(&current_batch_), sizeof(current_batch_));
    
    file.close();
    
    // Reinitialize with imported config
    return initialize(config_);
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
