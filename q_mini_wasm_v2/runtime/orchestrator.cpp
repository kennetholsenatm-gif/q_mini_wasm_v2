#include "orchestrator.hpp"
#include <algorithm>
#include <iostream>

namespace q_mini_wasm_v2::runtime {

RuntimeOrchestrator::RuntimeOrchestrator(const RuntimeConfig& config)
    : config_(config)
    , stop_requested_(false)
    , active_tasks_(0)
    , flash_cim_initialized_(false)
{
    start_workers();
}

RuntimeOrchestrator::~RuntimeOrchestrator() {
    stop_workers();
}

// ============================================================================
// Task Submission
// ============================================================================

std::future<void> RuntimeOrchestrator::submit_tableau_update(
    core::stabilizer::StabilizerTableau& tableau,
    std::function<void(core::stabilizer::StabilizerTableau&)> gate_func
) {
    auto promise = std::make_shared<std::promise<void>>();
    auto future = promise->get_future();
    
    auto task = [&tableau, gate_func, promise]() {
        try {
            gate_func(tableau);
            promise->set_value();
        } catch (...) {
            promise->set_exception(std::current_exception());
        }
    };
    
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        task_queue_.push(task);
        ++active_tasks_;
    }
    queue_cv_.notify_one();
    
    return future;
}

std::future<std::vector<size_t>> RuntimeOrchestrator::submit_moe_routing(
    core::moe::MoERouter& router,
    const std::vector<core::ternary::Trit>& input
) {
    auto promise = std::make_shared<std::promise<std::vector<size_t>>>();
    auto future = promise->get_future();
    
    auto task = [&router, input, promise]() {
        try {
            auto result = router.route_topk(input);
            promise->set_value(result);
        } catch (...) {
            promise->set_exception(std::current_exception());
        }
    };
    
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        task_queue_.push(task);
        ++active_tasks_;
    }
    queue_cv_.notify_one();
    
    return future;
}

std::future<core::learning::LayerGoodness> RuntimeOrchestrator::submit_ff_training(
    core::learning::ForwardForwardLearner& learner,
    size_t layer_idx,
    const std::vector<std::vector<core::ternary::Trit>>& positive_data,
    const std::vector<std::vector<core::ternary::Trit>>& negative_data
) {
    auto promise = std::make_shared<std::promise<core::learning::LayerGoodness>>();
    auto future = promise->get_future();
    
    auto task = [&learner, layer_idx, &positive_data, &negative_data, promise]() {
        try {
            auto result = learner.train_layer(layer_idx, positive_data, negative_data);
            promise->set_value(result);
        } catch (...) {
            promise->set_exception(std::current_exception());
        }
    };
    
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        task_queue_.push(task);
        ++active_tasks_;
    }
    queue_cv_.notify_one();
    
    return future;
}

std::future<int> RuntimeOrchestrator::submit_steane_polling(
    const core::steane::QutritSteaneCode& steane,
    std::vector<core::ternary::Trit>& physical
) {
    auto promise = std::make_shared<std::promise<int>>();
    auto future = promise->get_future();

    auto task = [&steane, &physical, promise]() {
        try {
            int corrected_location = -1;
            if (steane.detect_error(physical)) {
                corrected_location = steane.correct_error(physical);
            }
            promise->set_value(corrected_location);
        } catch (...) {
            promise->set_exception(std::current_exception());
        }
    };

    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        task_queue_.push(task);
        ++active_tasks_;
    }
    queue_cv_.notify_one();

    return future;
}

// ============================================================================
// Synchronization
// ============================================================================

void RuntimeOrchestrator::wait_all() {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    completion_cv_.wait(lock, [this]() {
        return active_tasks_ == 0 && task_queue_.empty();
    });
}

bool RuntimeOrchestrator::has_pending_tasks() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return active_tasks_ > 0 || !task_queue_.empty();
}

size_t RuntimeOrchestrator::pending_task_count() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return active_tasks_ + task_queue_.size();
}

// ============================================================================
// Flash-CIM Interface (GF(3) Implementation)
// ============================================================================

bool RuntimeOrchestrator::init_flash_cim() {
    // Initialize Flash-CIM simulation for GF(3) operations
    flash_cim_initialized_ = true;
    flash_cim_config_.pack_size = 5; // 5-trit blocks
    flash_cim_config_.wordline_count = 64;
    flash_cim_config_.multi_wordline_sensing = true;
    return flash_cim_initialized_;
}

std::vector<int8_t> RuntimeOrchestrator::flash_cim_execute(const std::vector<int8_t>& data) {
    if (!flash_cim_initialized_) {
        throw std::runtime_error("Flash-CIM not initialized");
    }
    
    // Pack data into 5-trit blocks
    std::vector<uint8_t> packed_data = pack_trits_to_bytes(data);
    
    // Simulate Multi-Wordline Sensing (MWS) computation
    // Perform GF(3) matrix-vector multiplication in memory
    std::vector<int8_t> result = simulate_mws_computation(packed_data);
    
    return result;
}

std::vector<uint8_t> RuntimeOrchestrator::pack_trits_to_bytes(const std::vector<int8_t>& trits) {
    std::vector<uint8_t> packed;
    size_t num_blocks = (trits.size() + 4) / 5; // Round up
    
    for (size_t i = 0; i < num_blocks; ++i) {
        uint8_t packed_byte = 0;
        for (size_t j = 0; j < 5 && (i * 5 + j) < trits.size(); ++j) {
            // Encode each trit {-1, 0, 1} as {2, 0, 1} in 2 bits
            int8_t trit = trits[i * 5 + j];
            uint8_t encoded = (trit == -1) ? 2 : (trit == 0) ? 0 : 1;
            packed_byte |= (encoded << (j * 2));
        }
        packed.push_back(packed_byte);
    }
    
    return packed;
}

std::vector<int8_t> RuntimeOrchestrator::simulate_mws_computation(const std::vector<uint8_t>& packed_data) {
    std::vector<int8_t> result;
    
    // Simulate compute-in-memory operations
    // Each byte represents 5 trits, perform GF(3) operations
    for (uint8_t byte : packed_data) {
        for (size_t j = 0; j < 5; ++j) {
            uint8_t encoded = (byte >> (j * 2)) & 0x3;
            // Decode back to trit
            int8_t trit = (encoded == 2) ? -1 : (encoded == 0) ? 0 : 1;
            
            // Apply GF(3) transformation (simulate CIM computation)
            // Perform modular arithmetic operation
            int8_t transformed = gf3_multiply(trit, 1); // Identity for now
            result.push_back(transformed);
        }
    }
    
    return result;
}

int8_t RuntimeOrchestrator::gf3_multiply(int8_t a, int8_t b) {
    // GF(3) multiplication: map {-1, 0, 1} to {2, 0, 1} for arithmetic
    int a_mapped = (a == -1) ? 2 : a;
    int b_mapped = (b == -1) ? 2 : b;
    
    int product = (a_mapped * b_mapped) % 3;
    
    // Map back to {-1, 0, 1}
    return (product == 2) ? -1 : static_cast<int8_t>(product);
}

bool RuntimeOrchestrator::is_flash_cim_available() const {
    return flash_cim_initialized_;
}

// ============================================================================
// Configuration
// ============================================================================

void RuntimeOrchestrator::update_config(const RuntimeConfig& new_config) {
    bool needs_restart = (new_config.num_worker_threads != config_.num_worker_threads);
    
    config_ = new_config;
    
    if (needs_restart) {
        stop_workers();
        start_workers();
    }
}

// ============================================================================
// Internal Methods
// ============================================================================

void RuntimeOrchestrator::worker_thread() {
    while (true) {
        std::function<void()> task;
        
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this]() {
                return stop_requested_ || !task_queue_.empty();
            });
            
            if (stop_requested_ && task_queue_.empty()) {
                return;
            }
            
            task = std::move(task_queue_.front());
            task_queue_.pop();
        }
        
        // Execute task
        task();
        
        // Decrement active tasks and notify completion
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            --active_tasks_;
        }
        completion_cv_.notify_all();
    }
}

void RuntimeOrchestrator::start_workers() {
    stop_requested_ = false;
    
    for (size_t i = 0; i < config_.num_worker_threads; ++i) {
        workers_.emplace_back(&RuntimeOrchestrator::worker_thread, this);
    }
}

void RuntimeOrchestrator::stop_workers() {
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        stop_requested_ = true;
    }
    queue_cv_.notify_all();
    
    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    
    workers_.clear();
}

std::unique_ptr<RuntimeOrchestrator> create_orchestrator(const RuntimeConfig& config) {
    return std::make_unique<RuntimeOrchestrator>(config);
}

} // namespace q_mini_wasm_v2::runtime