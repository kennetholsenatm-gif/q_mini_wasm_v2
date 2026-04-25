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
#include <algorithm>
#include <condition_variable>
#include <fstream>
#include <atomic>

namespace q_mini_wasm_v2::core::training {

// Forward declaration
class DataAcquisitionManager;

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

// API response types (C++17 variant) - Fixed-point versions added
using ApiPayload = std::variant<
    std::string_view,                    // Raw JSON/text
    std::vector<float>,                  // Numeric vector (legacy - use fixed below)
    std::vector<std::vector<float>>,     // Matrix data (legacy - use fixed below)
    std::vector<int32_t>,                // Fixed-point numeric vector (scale 1000)
    std::vector<std::vector<int32_t>>,   // Fixed-point matrix (scale 1000)
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
    
    int8_t is_positive() const { return label == 1 ? 1 : 0; }
    int8_t is_negative() const { return label == -1 ? 1 : 0; }
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
    
    // Check if API client has valid configuration (keys, endpoints, etc.)
    virtual bool is_configured() const = 0;
};

/**
 * @brief WolframAlpha API client
 * 
 * Queries mathematical proofs, calculus, physics simulations.
 * Perturbation: Symbolic substitution (change sign, misapply chain rule)
 */
class WolframClient : public ApiClient {
public:
    WolframClient();
    std::optional<ApiPayload> query(std::string_view endpoint, 
                                    std::string_view params) override;
    size_t rate_limit_remaining() const override { return rate_limit_; }
    void backoff() override;
    bool is_configured() const override { return !api_key_.empty(); }
    
    // Generate negative sample by corrupting mathematical AST
    static ApiPayload perturb_symbolic(const ApiPayload& positive);

private:
    size_t rate_limit_ = 10000;  // 100x increase - external APIs enforce via HTTP 429
    size_t backoff_ms_ = 100;
    std::string api_key_;  // Set via environment or config
    std::chrono::steady_clock::time_point last_query_time_;
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
    bool is_configured() const override { return true; }  // No key required
    
    // Generate valid SMILES variation (positive) vs corrupted with valency violations (negative)
    static ApiPayload perturb_smiles(const ApiPayload& positive);

private:
    size_t rate_limit_ = 5000;  // 100x increase
    size_t backoff_ms_ = 200;
    std::chrono::steady_clock::time_point last_query_time_;
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
    bool is_configured() const override { return true; }  // No key required
    
    // Mutate sequence by splicing or altering growth factor
    static ApiPayload perturb_sequence(const ApiPayload& positive);

private:
    size_t rate_limit_ = 20000;  // 100x increase
    size_t backoff_ms_ = 50;
    std::chrono::steady_clock::time_point last_query_time_;
};

/**
 * @brief Wikidata SPARQL API client
 * 
 * Queries RDF triples, ontological graphs.
 * Perturbation: Property recommender disruption (plausible but incorrect entity swapping)
 */
class WikidataClient : public ApiClient {
public:
    std::optional<ApiPayload> query(std::string_view endpoint,
                                    std::string_view params) override;
    size_t rate_limit_remaining() const override { return rate_limit_; }
    void backoff() override;
    bool is_configured() const override { return true; }  // No key required
    
    // Disrupt ontological triples by swapping objects with plausible alternatives
    static ApiPayload perturb_triples(const ApiPayload& positive);

private:
    size_t rate_limit_ = 10000;  // 100x increase
    size_t backoff_ms_ = 100;
    std::chrono::steady_clock::time_point last_query_time_;
};

/**
 * @brief arXiv API client
 * 
 * Queries scientific pre-prints, abstracts, papers.
 * Perturbation: Semantic contradiction injection (invert core claims)
 */
class ArxivClient : public ApiClient {
public:
    std::optional<ApiPayload> query(std::string_view endpoint,
                                    std::string_view params) override;
    size_t rate_limit_remaining() const override { return rate_limit_; }
    void backoff() override;
    bool is_configured() const override { return true; }  // No key required
    
    // Generate negative samples by inverting scientific claims
    static ApiPayload perturb_scientific(const ApiPayload& positive);

private:
    size_t rate_limit_ = 15000;  // 100x increase
    size_t backoff_ms_ = 100;
    std::chrono::steady_clock::time_point last_query_time_;
};

/**
 * @brief NASA Exoplanet Archive API client
 * 
 * Queries photometric time-series transit data.
 * Perturbation: Non-Keplerian transit noise injection
 */
class NasaExoplanetClient : public ApiClient {
public:
    std::optional<ApiPayload> query(std::string_view endpoint,
                                    std::string_view params) override;
    size_t rate_limit_remaining() const override { return rate_limit_; }
    void backoff() override;
    bool is_configured() const override { return true; }  // No key required
    
    // Inject synthetic astrophysical anomalies into light curves
    static ApiPayload perturb_transit(const ApiPayload& positive);

private:
    size_t rate_limit_ = 10000;  // 100x increase
    size_t backoff_ms_ = 150;
    std::chrono::steady_clock::time_point last_query_time_;
};

/**
 * @brief Protein Data Bank API client
 * 
 * Queries 3D protein folding coordinates, atomic structures.
 * Perturbation: Spatial coordinate drift (steric clash generation)
 */
class PdbClient : public ApiClient {
public:
    std::optional<ApiPayload> query(std::string_view endpoint,
                                    std::string_view params) override;
    size_t rate_limit_remaining() const override { return rate_limit_; }
    void backoff() override;
    bool is_configured() const override { return true; }  // No key required
    
    // Apply rotational/translational noise to generate non-physical structures
    static ApiPayload perturb_coordinates(const ApiPayload& positive);

private:
    size_t rate_limit_ = 5000;  // 100x increase
    size_t backoff_ms_ = 200;
    std::chrono::steady_clock::time_point last_query_time_;
};

/**
 * @brief GitHub API client
 * 
 * Queries SYCL/C++/WebAssembly code repositories.
 * Perturbation: AST mutilation (remove barriers, swap memory allocations)
 */
class GitHubClient : public ApiClient {
public:
    GitHubClient();
    std::optional<ApiPayload> query(std::string_view endpoint,
                                    std::string_view params) override;
    size_t rate_limit_remaining() const override { return rate_limit_; }
    void backoff() override;
    bool is_configured() const override { return true; }  // No key required for basic queries
    
    // Apply destructive logical mutations to code AST
    static ApiPayload perturb_code(const ApiPayload& positive);

private:
    size_t rate_limit_ = 6000;  // 100x increase - use API key for production
    size_t backoff_ms_ = 200;
    std::string api_key_;  // Optional - higher rate limits with key
    std::chrono::steady_clock::time_point last_query_time_;
};

/**
 * @brief Lean Theorem Prover API client
 * 
 * Queries formal mathematical proofs, tactic states.
 * Perturbation: Frame-preserving mutation (invalid tactic injection)
 */
class LeanClient : public ApiClient {
public:
    std::optional<ApiPayload> query(std::string_view endpoint,
                                    std::string_view params) override;
    size_t rate_limit_remaining() const override { return rate_limit_; }
    void backoff() override;
    bool is_configured() const override { return true; }  // No key required
    
    // Inject contextually plausible but mathematically invalid tactics
    static ApiPayload perturb_proof(const ApiPayload& positive);

private:
    size_t rate_limit_ = 10000;  // 100x increase
    size_t backoff_ms_ = 100;
    std::chrono::steady_clock::time_point last_query_time_;
};

/**
 * @brief OpenAlex API client - NO KEY REQUIRED
 * 
 * 250M+ scholarly works, completely open academic catalog.
 * Queries papers, authors, institutions, concepts.
 * Perturbation: Citation manipulation, fake co-authorship
 */
class OpenAlexClient : public ApiClient {
public:
    std::optional<ApiPayload> query(std::string_view endpoint,
                                    std::string_view params) override;
    size_t rate_limit_remaining() const override { return rate_limit_; }
    void backoff() override;
    bool is_configured() const override { return true; }  // NO KEY REQUIRED
    
    // Fabricate citations, alter authorship, misattribute concepts
    static ApiPayload perturb_scholarly(const ApiPayload& positive);

private:
    size_t rate_limit_ = 100000;  // 100k/day encouraged limit
    size_t backoff_ms_ = 100;
    std::chrono::steady_clock::time_point last_query_time_;
};

/**
 * @brief Gutendex API client - NO KEY REQUIRED
 * 
 * Project Gutenberg books via REST API (76,000+ books).
 * Full-text access to classic literature.
 * Perturbation: Chapter reordering, character name swaps
 */
class GutendexClient : public ApiClient {
public:
    std::optional<ApiPayload> query(std::string_view endpoint,
                                    std::string_view params) override;
    size_t rate_limit_remaining() const override { return rate_limit_; }
    void backoff() override;
    bool is_configured() const override { return true; }  // NO KEY REQUIRED
    
    // Reorder paragraphs, swap character names, alter endings
    static ApiPayload perturb_literature(const ApiPayload& positive);

private:
    size_t rate_limit_ = 50000;  // Generous, no strict limit
    size_t backoff_ms_ = 50;
    std::chrono::steady_clock::time_point last_query_time_;
};

/**
 * @brief USGS Earthquake API client - NO KEY REQUIRED
 * 
 * Real-time and historical seismic data worldwide.
 * GeoJSON format earthquake events.
 * Perturbation: Magnitude manipulation, location drift
 */
class UsgsEarthquakeClient : public ApiClient {
public:
    std::optional<ApiPayload> query(std::string_view endpoint,
                                    std::string_view params) override;
    size_t rate_limit_remaining() const override { return rate_limit_; }
    void backoff() override;
    bool is_configured() const override { return true; }  // NO KEY REQUIRED
    
    // Alter magnitude, shift epicenter, fabricate aftershocks
    static ApiPayload perturb_seismic(const ApiPayload& positive);

private:
    size_t rate_limit_ = 10000;  // No strict limit
    size_t backoff_ms_ = 100;
    std::chrono::steady_clock::time_point last_query_time_;
};

/**
 * @brief SpaceX API client - NO KEY REQUIRED
 * 
 * Launch data, rocket specs, mission details (REST + GraphQL).
 * Technical documentation and engineering data.
 * Perturbation: Payload mass errors, date shifts, stage swaps
 */
class SpaceXClient : public ApiClient {
public:
    std::optional<ApiPayload> query(std::string_view endpoint,
                                    std::string_view params) override;
    size_t rate_limit_remaining() const override { return rate_limit_; }
    void backoff() override;
    bool is_configured() const override { return true; }  // NO KEY REQUIRED
    
    // Swap payload capacities, alter launch dates, mix up stages
    static ApiPayload perturb_telemetry(const ApiPayload& positive);

private:
    size_t rate_limit_ = 50000;  // No strict limit
    size_t backoff_ms_ = 50;
    std::chrono::steady_clock::time_point last_query_time_;
};

/**
 * @brief Chronicling America API client - NO KEY REQUIRED
 * 
 * 20M+ historic newspaper pages from Library of Congress.
 * OCR text from 1789-1963.
 * Perturbation: Date misattribution, headline swaps
 */
class ChroniclingAmericaClient : public ApiClient {
public:
    std::optional<ApiPayload> query(std::string_view endpoint,
                                    std::string_view params) override;
    size_t rate_limit_remaining() const override { return rate_limit_; }
    void backoff() override;
    bool is_configured() const override { return true; }  // NO KEY REQUIRED
    
    // Alter dates, swap headlines, fabricate quotes
    static ApiPayload perturb_historical(const ApiPayload& positive);

private:
    size_t rate_limit_ = 20000;  // Generous LOC limits
    size_t backoff_ms_ = 100;
    std::chrono::steady_clock::time_point last_query_time_;
};

/**
 * @brief GBIF API client - NO KEY REQUIRED
 * 
 * Global Biodiversity Information Facility (2B+ species records).
 * Species occurrences, taxonomy, images.
 * Perturbation: Location spoofing, species misclassification
 */
class GbifClient : public ApiClient {
public:
    std::optional<ApiPayload> query(std::string_view endpoint,
                                    std::string_view params) override;
    size_t rate_limit_remaining() const override { return rate_limit_; }
    void backoff() override;
    bool is_configured() const override { return true; }  // NO KEY REQUIRED
    
    // Shift coordinates, swap species names, alter dates
    static ApiPayload perturb_biodiversity(const ApiPayload& positive);

private:
    size_t rate_limit_ = 10000;  // 10k/hour for heavy use
    size_t backoff_ms_ = 100;
    std::chrono::steady_clock::time_point last_query_time_;
};

/**
 * @brief Thread pool for async operations
 */
class ThreadPool {
public:
    explicit ThreadPool(size_t num_threads);
    ~ThreadPool();
    
    template<typename F>
    void enqueue(F&& task) {
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            tasks_.emplace(std::forward<F>(task));
        }
        condition_.notify_one();
    }
    
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
    
    // Load data sources from config file (data_sources.toml)
    // This replaces hardcoded APIs with configured sources
    bool use_config(const std::string& config_path = "config/data_sources.toml");
    
    // Load local training data from file/directory (JSONL, CSV, etc.)
    bool load_local_data(const std::string& data_path);
    /** Load plain-text lines from .txt files under a directory (for local training corpora). */
    bool load_from_directory(const std::string& dir_path);
    
    // Start synthesis pipeline - if local data loaded, use it; else use APIs
    void start(size_t acquisition_threads = 4, 
               size_t perturbation_threads = 2);
    void set_queue_limits(size_t raw_queue_max, size_t train_queue_max, size_t acquisition_queue_max) {
        max_raw_queue_depth_ = std::max<size_t>(size_t{64}, raw_queue_max);
        max_train_queue_depth_ = std::max<size_t>(size_t{128}, train_queue_max);
        max_acquisition_queue_depth_ = std::max<size_t>(size_t{64}, acquisition_queue_max);
    }
    
    // Check if local data is being used
    bool using_local_data() const { return has_local_data_; }
    
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
        size_t raw_queue_depth = 0;
        size_t raw_queue_max = 0;
        size_t train_queue_max = 0;
        uint64_t blocked_raw_pushes = 0;
        uint64_t blocked_train_pushes = 0;
        uint64_t blocked_wait_ms = 0;
        uint64_t dropped_payloads = 0;
        uint64_t dropped_payload_string_view = 0;
        uint64_t dropped_payload_other = 0;
        size_t acquisition_queue_depth = 0;
        size_t acquisition_queue_max = 0;
        uint64_t acquisition_blocked_pushes = 0;
        uint64_t acquisition_blocked_wait_ms = 0;
        uint64_t acquisition_dropped_too_short = 0;
        size_t topic_frontier_size = 0;
        size_t topic_frontier_max = 0;
        uint64_t topic_frontier_evictions = 0;
    };
    Stats get_stats() const;

private:
    // Acquisition thread functions
    void acquisition_worker(ApiClient* client);
    void config_acquisition_worker();  // For config-based DataAcquisitionManager
    
    // Perturbation thread function
    void perturbation_worker();
    
    // Autonomous topic discovery - extract new topics from API responses
    std::vector<std::string> extract_topics_from_response(const ApiPayload& response, 
                                                           const std::string& current_topic);
    
    // API clients (legacy hardcoded - replaced by config-based sources)
    std::vector<std::unique_ptr<ApiClient>> clients_;
    
    // Config-based data acquisition (preferred)
    std::unique_ptr<DataAcquisitionManager> acquisition_mgr_;
    bool use_config_sources_ = false;
    
    // Thread pools
    std::unique_ptr<ThreadPool> acquisition_pool_;
    std::unique_ptr<ThreadPool> perturbation_pool_;
    
    // Queues
    std::queue<ApiPayload> raw_queue_;      // From APIs
    std::queue<TrainingSample> train_queue_; // Contrastive pairs
    mutable std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::condition_variable queue_not_full_cv_;
    size_t max_raw_queue_depth_ = 32768;
    size_t max_train_queue_depth_ = 65536;
    size_t max_acquisition_queue_depth_ = 32768;
    
    // Control
    bool running_ = false;
    Stats stats_;
    mutable std::mutex stats_mutex_;
    
    // Local data storage (when using file-based training instead of APIs)
    bool has_local_data_ = false;
    std::vector<TrainingSample> local_samples_;
    size_t local_sample_index_ = 0;
    mutable std::mutex local_data_mutex_;
    std::string data_path_;

    /** When both local corpus and config/web feeds are active, alternate draws (with fallback). */
    std::atomic<uint64_t> interleave_counter_{0};
};

} // namespace q_mini_wasm_v2::core::training
