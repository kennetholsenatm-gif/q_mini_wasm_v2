#pragma once

#include <string>
#include <vector>
#include <map>
#include <thread>
#include <atomic>
#include <mutex>
#include <queue>
#include <functional>
#include <chrono>
#include <optional>
#include "data_synthesizer.hpp"

namespace q_mini_wasm_v2::core::training {

/**
 * @brief Data Source Configuration
 * Mirrors config/data_sources.toml structure
 */
struct DataSourceConfig {
    std::string name;
    std::string type;           // "web_api", "local_file", "local_directory"
    std::string url;            // For web_api
    std::string path;           // For local_file/directory
    std::string pattern;        // For directory: *.txt, *.jsonl
    std::vector<std::string> extensions;  // For directory: [.txt, .md, .json]
    std::map<std::string, std::string> headers;  // For web_api
    bool enabled = true;
    float rate_limit = 1.0;     // requests per second
    int priority = 1;           // Higher = fetched first
};

/**
 * @brief Data Acquisition Progress
 */
struct AcquisitionProgress {
    bool is_running = false;
    bool is_paused = false;
    int total_sources = 0;
    int completed_sources = 0;
    int total_items = 0;
    int processed_items = 0;
    std::string current_source;
    std::string current_operation;
    std::chrono::steady_clock::time_point start_time;
    std::vector<std::string> errors;
    
    double get_duration_seconds() const {
        return std::chrono::duration<double>(
            std::chrono::steady_clock::now() - start_time).count();
    }
};

/**
 * @brief Data Acquisition Manager
 * 
 * Integrates with trainer for continuous learning:
 * - Fetches data from configured sources (APIs, local files)
 * - Streams data into training pipeline
 * - Respects rate limits and priorities
 * - Supports pause/resume for WUI control
 */
class DataAcquisitionManager {
public:
    DataAcquisitionManager();
    ~DataAcquisitionManager();
    
    // Load data sources from config file
    bool load_sources(const std::string& config_path);
    
    // Start/stop acquisition
    bool start();
    void stop();
    void pause();
    void resume();
    
    // Check status
    bool is_running() const { return running_.load(); }
    bool is_paused() const { return paused_.load(); }
    AcquisitionProgress get_progress() const;
    
    // Fetch next batch for training (blocking, thread-safe)
    std::vector<TrainingSample> fetch_batch(size_t batch_size, 
                                            size_t min_length = 50,
                                            size_t max_length = 100000);
    
    // Check if data is available without blocking
    bool has_data() const;
    
    // Get count of samples in queue
    size_t queue_size() const;
    
    // Set SSE logging callback
    using LogCallback = std::function<void(const std::string&)>;
    void set_log_callback(LogCallback callback) { log_callback_ = callback; }
    
    // Preprocess text
    static std::string preprocess_text(const std::string& text, 
                                       size_t min_length = 50,
                                       size_t max_length = 100000);

private:
    void acquisition_loop();
    void fetch_from_web_api(const DataSourceConfig& source);
    void fetch_from_local_file(const DataSourceConfig& source);
    void fetch_from_directory(const DataSourceConfig& source);
    
    void log(const std::string& message);
    void log_progress(const std::string& type, const std::string& source, 
                      const std::string& operation, int items = 0);
    
    std::vector<DataSourceConfig> sources_;
    std::queue<TrainingSample> sample_queue_;
    mutable std::mutex queue_mutex_;
    
    std::thread acquisition_thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> paused_{false};
    std::atomic<bool> should_stop_{false};
    
    AcquisitionProgress progress_;
    mutable std::mutex progress_mutex_;
    
    LogCallback log_callback_;
    
    // String pool for sample lifetime management
    std::vector<std::string> string_pool_;
    mutable std::mutex string_pool_mutex_;
    
    // HTTP client for web APIs
    struct HttpResponse {
        int status_code = 0;
        std::string body;
        bool success = false;
        std::string error_message;
    };
    HttpResponse http_get(const std::string& url, 
                          const std::map<std::string, std::string>& headers,
                          int timeout_ms = 30000);
};

} // namespace q_mini_wasm_v2::core::training
