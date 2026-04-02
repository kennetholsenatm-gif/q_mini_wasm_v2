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
    stabilizer::StabilizerTableau& tableau,
    std::function<void(stabilizer::StabilizerTableau&)> gate_func
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
    moe::MoERouter& router,
    const std::vector<ternary::Trit>& input
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

std::future<learning::LayerGoodness> RuntimeOrchestrator::submit_ff_training(
    learning::ForwardForwardLearner& learner,
    size_t layer_idx,
    const std::vector<std::vector<ternary::Trit>>& positive_data,
    const std::vector<std::vector<ternary::Trit>>& negative_data
) {
    auto promise = std::make_shared<std::promise<learning::LayerGoodness>>();
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

template<typename T>
std::future<T> RuntimeOrchestrator::submit_async(std::function<T()> task) {
    auto promise = std::make_shared<std::promise<T>>();
    auto future = promise->get_future();
    
    auto wrapped_task = [task, promise]() {
        try {
            auto result = task();
            promise->set_value(result);
        } catch (...) {
            promise->set_exception(std::current_exception());
        }
    };
    
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        task_queue_.push(wrapped_task);
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
// Flash-CIM Interface (Placeholder)
// ============================================================================

bool RuntimeOrchestrator::init_flash_cim() {
    // Placeholder for Flash-CIM initialization
    // In real implementation, this would initialize hardware interface
    flash_cim_initialized_ = true;
    return flash_cim_initialized_;
}

std::vector<int8_t> RuntimeOrchestrator::flash_cim_execute(const std::vector<int8_t>& data) {
    if (!flash_cim_initialized_) {
        throw std::runtime_error("Flash-CIM not initialized");
    }
    
    // Placeholder for Flash-CIM execution
    // In real implementation, this would:
    // 1. Pack data into 5-trit blocks
    // 2. Execute via Multi-Wordline Sensing
    // 3. Unpack results
    
    // For now, just return the input data
    return data;
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