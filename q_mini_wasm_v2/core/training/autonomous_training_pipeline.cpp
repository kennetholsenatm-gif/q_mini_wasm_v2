#include "autonomous_training_pipeline.hpp"
#include "../gf3/gf3_types.hpp"
#include "../moe/gf3_layers.hpp"
#include "../moe/unified_config.hpp"
#include "../moe/runtime_orchestrator.hpp"
#include "../ternary/trit.hpp"
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
        set_state(PipelineState::FAILED);
        return false;
    }
    
    set_state(PipelineState::READY);
    return true;
}

bool AutonomousTrainingPipeline::initialize_components() {
    // Initialize DataSynthesizer
    synthesizer_ = std::make_unique<DataSynthesizer>();
    
    // Use config-based data sources (data_sources.toml) ONLY
    // NO fallback to hardcoded APIs - must use configured sources
    bool config_loaded = false;
    if (!config_.data_path.empty()) {
        // If a specific data path is provided, use local data
        config_loaded = synthesizer_->load_local_data(config_.data_path);
        if (config_loaded) {
            std::cout << "[TrainingPipeline] Using local data from: " << config_.data_path << std::endl;
        } else {
            std::cerr << "[TrainingPipeline] ERROR: Failed to load local data from: " << config_.data_path << std::endl;
            return false;
        }
    } else {
        const std::string& ds_path = config_.data_sources_toml_path.empty()
            ? std::string("config/data_sources.toml")
            : config_.data_sources_toml_path;
        config_loaded = synthesizer_->use_config(ds_path);
        if (config_loaded) {
            std::cout << "[TrainingPipeline] Using configured data sources from data_sources.toml" << std::endl;
        } else {
            std::cerr << "[TrainingPipeline] ERROR: Failed to load data_sources.toml" << std::endl;
            std::cerr << "[TrainingPipeline] Please configure data sources in config/data_sources.toml" << std::endl;
            return false;  // NO FALLBACK - training cannot proceed without configured sources
        }
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
    router_config.active_experts = config_.moe_top_k;
    router_config.routing_qutrits = std::max<size_t>(1u, config_.routing_qutrits);
    router_ = std::make_unique<moe::MoERouter>(router_config);
    
    // Create experts
    moe::ExpertNetwork::ExpertConfig expert_config;
    expert_config.input_dim = config_.moe_input_dim;
    expert_config.output_dim = config_.moe_output_dim;
    expert_config.hidden_dim = config_.moe_hidden_dim;
    expert_config.num_layers = std::max<size_t>(1u, config_.moe_expert_internal_layers);
    
    experts_ = create_experts(config_.moe_num_experts, expert_config);
    
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
        
        // Update and emit metrics
        update_metrics();
        if (config_.enable_wui_streaming) {
            emit_metrics();
        }
        
        // Check for epoch completion based on samples processed
        samples_processed_ += samples_trained;
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
        
        // Small yield to prevent CPU spinning
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    
    if (state_.load() != PipelineState::COMPLETE && 
        state_.load() != PipelineState::FAILED) {
        set_state(PipelineState::STOPPING);
    }
}

size_t AutonomousTrainingPipeline::process_batch() {
    // Get samples from DataSynthesizer
    std::vector<TrainingSample> batch_samples;
    batch_samples.reserve(config_.batch_size);
    
    // Timeout mechanism: max 30 seconds to acquire a full batch
    const auto max_wait_time = std::chrono::seconds(30);
    const auto start_time = std::chrono::steady_clock::now();
    size_t consecutive_empty_checks = 0;
    
    for (size_t i = 0; i < config_.batch_size; ++i) {
        // Check for timeout
        auto elapsed = std::chrono::steady_clock::now() - start_time;
        if (elapsed > max_wait_time) {
            std::cerr << "[TrainingPipeline] TIMEOUT: Failed to acquire sample " << (i + 1) 
                      << "/" << config_.batch_size << " within 30 seconds. "
                      << "DataSynthesizer may not be producing samples." << std::endl;
            return 0;  // Return 0 samples trained - no progress possible
        }
        
        if (synthesizer_->has_sample()) {
            batch_samples.push_back(synthesizer_->get_sample());
            consecutive_empty_checks = 0;  // Reset counter on success
        } else {
            // Wait for samples with exponential backoff
            consecutive_empty_checks++;
            auto wait_ms = std::min(10 * (1 << std::min(consecutive_empty_checks, size_t(10))), 1000);
            std::this_thread::sleep_for(std::chrono::milliseconds(wait_ms));
            --i;  // Retry this sample
        }
    }
    
    if (batch_samples.empty()) {
        std::cerr << "[TrainingPipeline] No training samples available - DataSynthesizer queue empty" << std::endl;
        return 0;  // Return 0 samples trained - no progress was made
    }
    
    // Forward-Forward metrics (GF(3) TropicalInt - max-plus algebra)
    using namespace q_mini_wasm_v2::core::gf3;
    TropicalInt batch_pos_goodness = tropical::ZERO;
    TropicalInt batch_neg_goodness = tropical::ZERO;
    TropicalInt batch_delta = tropical::ZERO;
    
    // MoE metrics
    uint32_t total_routes = 0;
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
            experts_[expert_idx]->TrainForwardForward(positive_sample, negative_sample);
            
            // Compute goodness for metrics
            uint32_t pos_goodness = experts_[expert_idx]->ComputeGoodness(positive_sample);
            uint32_t neg_goodness = experts_[expert_idx]->ComputeGoodness(negative_sample);
            int32_t delta = static_cast<int32_t>(pos_goodness) - static_cast<int32_t>(neg_goodness);
            
            // Accumulate metrics using tropical addition (max)
            batch_pos_goodness = tropical::add(batch_pos_goodness, static_cast<TropicalInt>(pos_goodness));
            batch_neg_goodness = tropical::add(batch_neg_goodness, static_cast<TropicalInt>(neg_goodness));
            batch_delta = tropical::add(batch_delta, static_cast<TropicalInt>(delta));
            expert_counts[expert_idx]++;
            expert_deltas[expert_idx] += delta;
            total_routes++;
        }
    }
    
    // Update cached metrics using tropical division
    if (total_routes > 0) {
        std::lock_guard<std::mutex> lock(metrics_mutex_);
        cached_metrics_.ff_positive_goodness = static_cast<uint32_t>(tropical::divide(batch_pos_goodness, static_cast<TropicalInt>(total_routes)));
        cached_metrics_.ff_negative_goodness = static_cast<uint32_t>(tropical::divide(batch_neg_goodness, static_cast<TropicalInt>(total_routes)));
        cached_metrics_.ff_goodness_delta = static_cast<int32_t>(tropical::divide(batch_delta, static_cast<TropicalInt>(total_routes)));
        cached_metrics_.ff_total_train_calls += total_routes;
        cached_metrics_.expert_utilization.resize(config_.moe_num_experts);
        cached_metrics_.expert_deltas = expert_deltas;
        
        // Compute utilization using tropical division (not percentage - raw count)
        for (size_t i = 0; i < config_.moe_num_experts; ++i) {
            cached_metrics_.expert_utilization[i] = static_cast<uint32_t>(tropical::divide(static_cast<TropicalInt>(expert_counts[i]), static_cast<TropicalInt>(total_routes)));
        }
    }
    
    // Return number of samples actually trained (successful routes through experts)
    return total_routes;
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
            size_t count = 0;
            for (float val : arg) {
                // Quantize to {-1, 0, 1} based on thresholds
                if (val > 0.33f) {
                    result.push_back(ternary::Trit::POSITIVE);
                } else if (val < -0.33f) {
                    result.push_back(ternary::Trit::NEGATIVE);
                } else {
                    result.push_back(ternary::Trit::ZERO);
                }
                count++;
                if (count >= static_cast<size_t>(config_.moe_input_dim)) break;
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
    // Encode vertex pairs (i, j) into TritPack5 format
    // TritPack5 stores 5 trits in 8 bits - we use first 3 trits for i, last 2 for j (mod 3)
    // This is a simplified encoding for Betti number computation
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
        
        // Add diagnostic info to status message
        if (ds_stats.queue_depth == 0 && state_.load() == PipelineState::TRAINING) {
            status += " | WAITING: DataSynthesizer queue empty";
            if (ds_stats.api_failures > 0) {
                status += " (API failures: " + std::to_string(ds_stats.api_failures) + ")";
            }
        } else if (ds_stats.total_acquired == 0 && state_.load() == PipelineState::TRAINING) {
            status += " | WAITING: No data acquired from APIs yet";
        } else {
            status += " | queue_depth=" + std::to_string(ds_stats.queue_depth) +
                      " acquired=" + std::to_string(ds_stats.total_acquired);
        }
    }
    
    metrics.status_message = status;
    
    return metrics;
}

void AutonomousTrainingPipeline::update_metrics() {
    // Update internal metrics cache
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    
    // Pipeline state
    cached_metrics_.current_epoch = current_epoch_.load();
    cached_metrics_.current_batch = current_batch_.load();
    cached_metrics_.samples_processed = samples_processed_.load();
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
        case PipelineState::FAILED: return "error";
        default: return "unknown";
    }
}

std::unique_ptr<AutonomousTrainingPipeline> create_training_pipeline() {
    return std::make_unique<AutonomousTrainingPipeline>();
}

} // namespace q_mini_wasm_v2::core::training
