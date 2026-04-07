#pragma once

#include <vector>
#include <memory>
#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <future>

#include "../core/stabilizer/tableau.hpp"
#include "../core/moe/router.hpp"
#include "../core/learning/forward_forward.hpp"
#include "../core/steane/qutrit_steane.hpp"

namespace q_mini_wasm_v2::runtime {

/**
 * @brief Execution task types
 */
enum class TaskType {
    TABLEAU_UPDATE,
    MOE_ROUTING,
    FORWARD_FORWARD,
    MEASUREMENT
};

/**
 * @brief Execution task
 */
struct ExecutionTask {
    TaskType type;
    std::function<void()> work;
    std::promise<void> completion;
};

/**
 * @brief Runtime Configuration
 */
struct RuntimeConfig {
    size_t num_worker_threads;      // Number of worker threads
    size_t max_queue_size;          // Maximum pending tasks
    bool enable_async;              // Enable async execution
    bool enable_flash_cim;          // Enable Flash-CIM interface
};

/**
 * @brief WASM-like Runtime Orchestrator
 * 
 * Provides asynchronous execution orchestration for q_mini_wasm_v2 operations,
 * enabling:
 * 1. Parallel execution of tableau updates
 * 2. Async MoE routing computations
 * 3. Overlapping computation with I/O (Flash-CIM)
 * 4. Thread pool for efficient resource utilization
 * 
 * Based on research: "A Unified QMINIWASM Framework: Bridging Qutrit 
 * Stabilizer Formalisms and Extreme-Edge Ternary AI"
 */
class RuntimeOrchestrator {
public:
    /**
     * @brief Construct runtime orchestrator
     * @param config Runtime configuration
     */
    explicit RuntimeOrchestrator(const RuntimeConfig& config);
    
    /**
     * @brief Destructor - stops all worker threads
     */
    ~RuntimeOrchestrator();

    // ========================================================================
    // Task Submission
    // ========================================================================
    
    /**
     * @brief Submit tableau update task
     * @param tableau Stabilizer tableau to update
     * @param gate_func Gate operation function
     * @return Future for completion
     */
    std::future<void> submit_tableau_update(
        core::stabilizer::StabilizerTableau& tableau,
        std::function<void(core::stabilizer::StabilizerTableau&)> gate_func
    );
    
    /**
     * @brief Submit MoE routing task
     * @param router MoE router
     * @param input Input features
     * @return Future for routing result
     */
    std::future<std::vector<size_t>> submit_moe_routing(
        core::moe::MoERouter& router,
        const std::vector<core::ternary::Trit>& input
    );
    
    /**
     * @brief Submit Forward-Forward training task
     * @param learner Forward-Forward learner
     * @param layer_idx Layer to train
     * @param positive_data Positive samples
     * @param negative_data Negative samples
     * @return Future for goodness metrics
     */
    std::future<core::learning::LayerGoodness> submit_ff_training(
        core::learning::ForwardForwardLearner& learner,
        size_t layer_idx,
        const std::vector<std::vector<core::ternary::Trit>>& positive_data,
        const std::vector<std::vector<core::ternary::Trit>>& negative_data
    );

    /**
     * @brief Submit Steane Code Error Polling Task
     * 
     * Asynchronously polls the physical qutrit array for errors via syndrome
     * measurement, and corrects them in-place if found.
     * 
     * @param steane Qutrit Steane Code instance
     * @param physical Array of physical qutrits to check and potentially correct
     * @return Future containing the index of the corrected qutrit, or -1 if no error
     */
    std::future<int> submit_steane_polling(
        const core::steane::QutritSteaneCode& steane,
        std::vector<core::ternary::Trit>& physical
    );
    
    /**
     * @brief Submit generic async task
     * @param task Task function
     * @return Future for task result
     */
    template<typename T>
    std::future<T> submit_async(std::function<T()> task) {
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

    // ========================================================================
    // Synchronization
    // ========================================================================
    
    /**
     * @brief Wait for all pending tasks to complete
     */
    void wait_all();
    
    /**
     * @brief Check if any tasks are pending
     */
    bool has_pending_tasks() const;
    
    /**
     * @brief Get number of pending tasks
     */
    size_t pending_task_count() const;

    // ========================================================================
    // Flash-CIM Interface
    // ========================================================================
    
    /**
     * @brief Initialize Flash-CIM interface
     * @return true if initialization successful
     */
    bool init_flash_cim();
    
    /**
     * @brief Execute in-situ operation via Flash-CIM
     * @param data Data to process
     * @return Processed data
     */
    std::vector<int8_t> flash_cim_execute(const std::vector<int8_t>& data);
    
    /**
     * @brief Check if Flash-CIM is available
     */
    bool is_flash_cim_available() const;

    // ========================================================================
    // Configuration
    // ========================================================================
    
    /**
     * @brief Get runtime configuration
     */
    const RuntimeConfig& config() const { return config_; }
    
    /**
     * @brief Update configuration
     */
    void update_config(const RuntimeConfig& new_config);

private:
    RuntimeConfig config_;
    
    // Thread pool
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> task_queue_;
    
    // Synchronization
    mutable std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::condition_variable completion_cv_;
    
    // State
    bool stop_requested_;
    size_t active_tasks_;
    
    // Flash-CIM state
    bool flash_cim_initialized_;
    
    // ========================================================================
    // Worker Thread Function
    // ========================================================================
    
    void worker_thread();
    
    /**
     * @brief Start worker threads
     */
    void start_workers();
    
    /**
     * @brief Stop worker threads
     */
    void stop_workers();
};

/**
 * @brief Factory function for creating runtime orchestrator
 */
std::unique_ptr<RuntimeOrchestrator> create_orchestrator(const RuntimeConfig& config);

} // namespace q_mini_wasm_v2::runtime