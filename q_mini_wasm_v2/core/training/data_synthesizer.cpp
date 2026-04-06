#include "data_synthesizer.hpp"
#include <algorithm>
#include <random>
#include <chrono>
#include <thread>

namespace q_mini_wasm_v2::core::training {

// ============================================================================
// ThreadPool Implementation
// ============================================================================

ThreadPool::ThreadPool(size_t num_threads) {
    for (size_t i = 0; i < num_threads; ++i) {
        workers_.emplace_back([this] {
            while (true) {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(queue_mutex_);
                    condition_.wait(lock, [this] { return stop_ || !tasks_.empty(); });
                    if (stop_ && tasks_.empty()) return;
                    task = std::move(tasks_.front());
                    tasks_.pop();
                }
                task();
            }
        });
    }
}

ThreadPool::~ThreadPool() {
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        stop_ = true;
    }
    condition_.notify_all();
    for (auto& worker : workers_) {
        worker.join();
    }
}

void ThreadPool::wait_for_completion() {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    condition_.wait(lock, [this] { return tasks_.empty(); });
}

// ============================================================================
// API Client Implementations
// ============================================================================

std::optional<ApiPayload> WolframClient::query(std::string_view endpoint, 
                                                std::string_view params) {
    if (rate_limit_ == 0) {
        backoff();
        return std::nullopt;
    }
    
    // Simulated API query - in production, this would use cURL
    // Parse mathematical query from params
    --rate_limit_;
    
    // Return mock mathematical data
    std::vector<float> result;
    // Would extract from WolframAlpha response
    return result;
}

void WolframClient::backoff() {
    std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms_));
    backoff_ms_ *= 2;  // Exponential backoff
    rate_limit_ = 100;  // Reset after backoff
}

ApiPayload WolframClient::perturb_symbolic(const ApiPayload& positive) {
    // Extract mathematical expression from payload
    // Inject logical fallacy: sign change, chain rule misapplication, etc.
    // Frame-preserving mutation that maintains structure but breaks validity
    
    auto result = positive;
    // Apply perturbation logic
    return result;
}

std::optional<ApiPayload> PubChemClient::query(std::string_view endpoint,
                                                std::string_view params) {
    if (rate_limit_ == 0) {
        backoff();
        return std::nullopt;
    }
    --rate_limit_;
    
    // Query PubChem for molecular data
    // Return SMILES string, molecular properties
    std::string_view smiles = "CC(C)Cc1ccc(cc1)C(C)C(=O)O";  // Example
    return std::vector<Trit>();  // Would encode SMILES as trits
}

void PubChemClient::backoff() {
    std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms_));
    backoff_ms_ *= 2;
    rate_limit_ = 50;
}

ApiPayload PubChemClient::perturb_smiles(const ApiPayload& positive) {
    // Reaction-aware negative sampling
    // Generate valid alternative SMILES (positive) vs corrupted with valency violations (negative)
    // Use CONSMI/SimSon techniques
    
    auto result = positive;
    // Corrupt SMILES while maintaining surface syntax
    return result;
}

std::optional<ApiPayload> OeisClient::query(std::string_view endpoint,
                                           std::string_view params) {
    if (rate_limit_ == 0) {
        backoff();
        return std::nullopt;
    }
    --rate_limit_;
    
    // Query integer sequence database
    std::vector<float> sequence = {1, 1, 2, 3, 5, 8, 13, 21};  // Fibonacci
    return sequence;
}

void OeisClient::backoff() {
    std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms_));
    backoff_ms_ *= 2;
    rate_limit_ = 200;
}

ApiPayload OeisClient::perturb_sequence(const ApiPayload& positive) {
    // Mutation-based bootstrapping
    // 1. Splice: combine first half of one sequence with second half of another
    // 2. Growth factor alteration: modify recursive relation
    // 3. Colijn-Plazzotta rank perturbation
    
    auto result = positive;
    static std::mt19937 rng(std::random_device{}());
    
    if (std::holds_alternative<std::vector<float>>(result)) {
        auto& seq = std::get<std::vector<float>>(result);
        if (!seq.empty()) {
            std::uniform_int_distribution<size_t> dist(0, seq.size() - 1);
            size_t pos = dist(rng);
            seq[pos] = static_cast<float>(dist(rng) * 2);  // Random perturbation
        }
    }
    
    return result;
}

// ============================================================================
// DataSynthesizer Implementation
// ============================================================================

DataSynthesizer::DataSynthesizer() = default;
DataSynthesizer::~DataSynthesizer() {
    if (running_) stop();
}

void DataSynthesizer::initialize_apis() {
    clients_.push_back(std::make_unique<WolframClient>());
    clients_.push_back(std::make_unique<PubChemClient>());
    clients_.push_back(std::make_unique<OeisClient>());
}

void DataSynthesizer::start(size_t acquisition_threads, size_t perturbation_threads) {
    if (running_) return;
    running_ = true;
    
    initialize_apis();
    
    acquisition_pool_ = std::make_unique<ThreadPool>(acquisition_threads);
    perturbation_pool_ = std::make_unique<ThreadPool>(perturbation_threads);
    
    // Start acquisition workers
    for (auto& client : clients_) {
        acquisition_pool_->enqueue([this, &client] {
            acquisition_worker(client.get());
        });
    }
    
    // Start perturbation workers
    for (size_t i = 0; i < perturbation_threads; ++i) {
        perturbation_pool_->enqueue([this] {
            perturbation_worker();
        });
    }
}

void DataSynthesizer::stop() {
    running_ = false;
    queue_cv_.notify_all();
    acquisition_pool_.reset();
    perturbation_pool_.reset();
}

TrainingSample DataSynthesizer::get_sample() {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    queue_cv_.wait(lock, [this] { return !train_queue_.empty() || !running_; });
    
    if (!train_queue_.empty()) {
        TrainingSample sample = std::move(train_queue_.front());
        train_queue_.pop();
        return sample;
    }
    
    return TrainingSample{};  // Empty sample if stopped
}

bool DataSynthesizer::has_sample() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return !train_queue_.empty();
}

DataSynthesizer::Stats DataSynthesizer::get_stats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    Stats s = stats_;
    s.queue_depth = train_queue_.size();
    return s;
}

void DataSynthesizer::acquisition_worker(ApiClient* client) {
    while (running_) {
        auto payload = client->query("query", "");
        
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            if (payload) {
                raw_queue_.push(*payload);
                {
                    std::lock_guard<std::mutex> stats_lock(stats_mutex_);
                    ++stats_.total_acquired;
                }
            } else {
                std::lock_guard<std::mutex> stats_lock(stats_mutex_);
                ++stats_.api_failures;
            }
        }
        queue_cv_.notify_one();
        
        // Rate limit respect
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void DataSynthesizer::perturbation_worker() {
    while (running_) {
        ApiPayload positive;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this] { return !raw_queue_.empty() || !running_; });
            
            if (!running_) break;
            if (raw_queue_.empty()) continue;
            
            positive = std::move(raw_queue_.front());
            raw_queue_.pop();
        }
        
        // Generate contrastive pairs
        TrainingSample pos_sample{positive, 1, "synthesizer", "general"};
        
        // Create negative sample via perturbation
        ApiPayload negative = WolframClient::perturb_symbolic(positive);
        TrainingSample neg_sample{negative, -1, "synthesizer", "general"};
        
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            train_queue_.push(pos_sample);
            train_queue_.push(neg_sample);
            {
                std::lock_guard<std::mutex> stats_lock(stats_mutex_);
                stats_.total_perturbed += 2;
            }
        }
        queue_cv_.notify_one();
    }
}

} // namespace q_mini_wasm_v2::core::training
