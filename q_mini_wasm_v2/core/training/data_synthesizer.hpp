#pragma once

#include <cstdint>
#include <vector>
#include <string_view>
#include <optional>
#include <variant>
#include <functional>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>

namespace q_mini_wasm_v2::core::training {

/**
 * @brief Data Synthesizer Agent for Autonomous Forward-Forward Training
 * 
 * Implements infinite-curriculum training pipeline from research:
 * - Dual-thread-pool: Acquisition Pool (REST/GraphQL) + Perturbation Pool
 * - API integrations: WolframAlpha, PubChem, OEIS, PDB, NASA Exoplanet
 * - Contrastive pair generation: positive (verified) vs negative (corrupted)
 * - Ternary state mapping: +1 (True), -1 (False), 0 (Unknown)
 * 
 * Requirements:
 * - C++17: std::optional, std::variant for API payloads
 * - std::string_view for zero-copy JSON parsing
 * - Rate limiting and exponential backoff
 */

using Trit = int8_t;  // {-1, 0, +1}

// API response types (C++17 variant)
using ApiPayload = std::variant<
    std::string_view,                    // Raw JSON/text
    std::vector<float>,                  // Numeric vector
    std::vector<std::vector<float>>,     // Matrix data
    std::vector<Trit>                    // Ternary encoded
>;

/**
 * @brief Training sample with contrastive label
 */
struct TrainingSample {
    ApiPayload data;
    Trit label;                          // +1 (positive), -1 (negative), 0 (unknown)
    std::string_view source_api;         // "wolfram", "pubchem", "oeis", etc.
    std::string_view domain;             // "math", "chemistry", "biology", "physics"
    
    bool is_positive() const { return label == 1; }
    bool is_negative() const { return label == -1; }
};

/**
 * @brief API client interface
 */
class ApiClient {
public:
    virtual ~ApiClient() = default;
    
    // Async query API, return optional payload
    virtual std::optional<ApiPayload> query(std::string_view endpoint, 
                                            std::string_view params) = 0;
    
    // Rate limit remaining (0 = exhausted, must wait)
    virtual size_t rate_limit_remaining() const = 0;
    
    // Exponential backoff after failure
    virtual void backoff() = 0;
};

/**
 * @brief WolframAlpha API client
 * 
 * Queries mathematical proofs, calculus, physics simulations.
 * Perturbation: Symbolic substitution (change sign, misapply chain rule)
 */
class WolframClient : public ApiClient {
public:
    std::optional<ApiPayload> query(std::string_view endpoint, 
                                    std::string_view params) override;
    size_t rate_limit_remaining() const override { return rate_limit_; }
    void backoff() override;
    
    // Generate negative sample by corrupting mathematical AST
    static ApiPayload perturb_symbolic(const ApiPayload& positive);

private:
    size_t rate_limit_ = 100;
    size_t backoff_ms_ = 1000;
};

/**
 * @brief PubChem API client
 * 
 * Queries molecular graphs, SMILES strings, chemical properties.
 * Perturbation: Reaction-aware negative sampling (valency violations)
 */
class PubChemClient : public ApiClient {
public:
    std::optional<ApiPayload> query(std::string_view endpoint,
                                    std::string_view params) override;
    size_t rate_limit_remaining() const override { return rate_limit_; }
    void backoff() override;
    
    // Generate valid SMILES variation (positive) vs corrupted (negative)
    static ApiPayload perturb_smiles(const ApiPayload& positive);

private:
    size_t rate_limit_ = 50;
    size_t backoff_ms_ = 2000;
};

/**
 * @brief OEIS (Integer Sequences) API client
 * 
 * Queries integer sequences, generating functions.
 * Perturbation: Mutation-based bootstrapping (splice sequences, alter recursion)
 */
class OeisClient : public ApiClient {
public:
    std::optional<ApiPayload> query(std::string_view endpoint,
                                    std::string_view params) override;
    size_t rate_limit_remaining() const override { return rate_limit_; }
    void backoff() override;
    
    // Mutate sequence by splicing or altering growth factor
    static ApiPayload perturb_sequence(const ApiPayload& positive);

private:
    size_t rate_limit_ = 200;
    size_t backoff_ms_ = 500;
};

/**
 * @brief Thread pool for async operations
 */
class ThreadPool {
public:
    explicit ThreadPool(size_t num_threads);
    ~ThreadPool();
    
    template<typename F>
    void enqueue(F&& task);
    
    void wait_for_completion();

private:
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex queue_mutex_;
    std::condition_variable condition_;
    bool stop_ = false;
};

/**
 * @brief Data Synthesizer Agent
 * 
 * Dual-thread-pool architecture:
 * - Acquisition Pool: Async HTTP requests to APIs
 * - Perturbation Pool: Generate contrastive pairs
 * 
 * Outputs training samples to Forward-Forward layers via USM queues.
 */
class DataSynthesizer {
public:
    DataSynthesizer();
    ~DataSynthesizer();
    
    // Initialize API clients with keys/endpoints
    void initialize_apis();
    
    // Start synthesis pipeline
    void start(size_t acquisition_threads = 4, 
               size_t perturbation_threads = 2);
    
    // Stop all threads
    void stop();
    
    // Get next training sample (blocking)
    TrainingSample get_sample();
    
    // Non-blocking check
    bool has_sample() const;

    // Statistics
    struct Stats {
        size_t total_acquired = 0;
        size_t total_perturbed = 0;
        size_t api_failures = 0;
        size_t queue_depth = 0;
    };
    Stats get_stats() const;

private:
    // Acquisition thread function
    void acquisition_worker(ApiClient* client);
    
    // Perturbation thread function
    void perturbation_worker();
    
    // API clients
    std::vector<std::unique_ptr<ApiClient>> clients_;
    
    // Thread pools
    std::unique_ptr<ThreadPool> acquisition_pool_;
    std::unique_ptr<ThreadPool> perturbation_pool_;
    
    // Queues
    std::queue<ApiPayload> raw_queue_;      // From APIs
    std::queue<TrainingSample> train_queue_; // Contrastive pairs
    mutable std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    
    // Control
    bool running_ = false;
    Stats stats_;
    mutable std::mutex stats_mutex_;
};

} // namespace q_mini_wasm_v2::core::training
