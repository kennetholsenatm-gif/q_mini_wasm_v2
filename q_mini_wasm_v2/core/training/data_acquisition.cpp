#include "data_acquisition.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <regex>
#include <filesystem>
#include <random>

#ifdef _WIN32
    #include <windows.h>
    #include <winhttp.h>
    #pragma comment(lib, "winhttp.lib")
#endif

namespace q_mini_wasm_v2::core::training {

DataAcquisitionManager::DataAcquisitionManager() {}

DataAcquisitionManager::~DataAcquisitionManager() {
    stop();
}

bool DataAcquisitionManager::load_sources(const std::string& config_path) {
    sources_.clear();
    
    // Check if file exists
    if (!std::filesystem::exists(config_path)) {
        log("Data sources config not found: " + config_path);
        return false;
    }
    
    // Parse TOML manually (simple parser for our needs)
    std::ifstream file(config_path);
    if (!file.is_open()) {
        log("Failed to open: " + config_path);
        return false;
    }
    
    std::string line;
    DataSourceConfig current_source;
    bool in_source = false;
    
    while (std::getline(file, line)) {
        // Trim whitespace
        line.erase(0, line.find_first_not_of(" \t"));
        line.erase(line.find_last_not_of(" \t") + 1);
        
        if (line.empty() || line[0] == '#') continue;
        
        // Check for [[sources]] section start
        if (line == "[[sources]]") {
            if (in_source && !current_source.name.empty()) {
                sources_.push_back(current_source);
            }
            current_source = DataSourceConfig{};
            in_source = true;
            continue;
        }
        
        // Check for [settings] - skip for now
        if (line == "[settings]") {
            if (in_source && !current_source.name.empty()) {
                sources_.push_back(current_source);
            }
            in_source = false;
            continue;
        }
        
        if (!in_source) continue;
        
        // Parse key = value
        size_t eq_pos = line.find('=');
        if (eq_pos == std::string::npos) continue;
        
        std::string key = line.substr(0, eq_pos);
        std::string value = line.substr(eq_pos + 1);
        
        // Trim
        key.erase(0, key.find_first_not_of(" \t"));
        key.erase(key.find_last_not_of(" \t") + 1);
        value.erase(0, value.find_first_not_of(" \t"));
        value.erase(value.find_last_not_of(" \t") + 1);
        
        // Remove quotes
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
            value = value.substr(1, value.size() - 2);
        }
        
        if (key == "name") current_source.name = value;
        else if (key == "type") current_source.type = value;
        else if (key == "url") current_source.url = value;
        else if (key == "path") current_source.path = value;
        else if (key == "enabled") current_source.enabled = (value == "true");
        else if (key == "rate_limit") current_source.rate_limit = std::stof(value);
        else if (key == "priority") current_source.priority = std::stoi(value);
    }
    
    // Add last source
    if (in_source && !current_source.name.empty()) {
        sources_.push_back(current_source);
    }
    
    // Filter to only enabled sources
    sources_.erase(
        std::remove_if(sources_.begin(), sources_.end(),
            [](const DataSourceConfig& s) { return !s.enabled; }),
        sources_.end()
    );
    
    // Sort by priority (higher first)
    std::sort(sources_.begin(), sources_.end(),
        [](const DataSourceConfig& a, const DataSourceConfig& b) {
            return a.priority > b.priority;
        });
    
    log("Loaded " + std::to_string(sources_.size()) + " data sources");
    return !sources_.empty();
}

bool DataAcquisitionManager::start() {
    if (running_.load()) {
        log("Data acquisition already running");
        return false;
    }
    
    if (sources_.empty()) {
        log("No data sources configured");
        return false;
    }
    
    should_stop_ = false;
    paused_ = false;
    running_ = true;
    
    {
        std::lock_guard<std::mutex> lock(progress_mutex_);
        progress_ = AcquisitionProgress{};
        progress_.is_running = true;
        progress_.start_time = std::chrono::steady_clock::now();
        progress_.total_sources = static_cast<int>(sources_.size());
    }
    
    acquisition_thread_ = std::thread(&DataAcquisitionManager::acquisition_loop, this);
    log("Data acquisition started with " + std::to_string(sources_.size()) + " sources");
    return true;
}

void DataAcquisitionManager::stop() {
    should_stop_ = true;
    running_ = false;
    
    if (acquisition_thread_.joinable()) {
        acquisition_thread_.join();
    }
    
    {
        std::lock_guard<std::mutex> lock(progress_mutex_);
        progress_.is_running = false;
    }
    
    log("Data acquisition stopped");
}

void DataAcquisitionManager::pause() {
    paused_ = true;
    {
        std::lock_guard<std::mutex> lock(progress_mutex_);
        progress_.is_paused = true;
    }
    log("Data acquisition paused");
}

void DataAcquisitionManager::resume() {
    paused_ = false;
    {
        std::lock_guard<std::mutex> lock(progress_mutex_);
        progress_.is_paused = false;
    }
    log("Data acquisition resumed");
}

AcquisitionProgress DataAcquisitionManager::get_progress() const {
    std::lock_guard<std::mutex> lock(progress_mutex_);
    return progress_;
}

std::vector<TrainingSample> DataAcquisitionManager::fetch_batch(size_t batch_size,
                                                                 size_t min_length,
                                                                 size_t max_length) {
    std::vector<TrainingSample> batch;
    batch.reserve(batch_size);
    
    std::unique_lock<std::mutex> lock(queue_mutex_);
    
    while (batch.size() < batch_size && !sample_queue_.empty()) {
        batch.push_back(std::move(sample_queue_.front()));
        sample_queue_.pop();
    }
    
    lock.unlock();
    
    // Preprocess
    for (auto& sample : batch) {
        if (std::holds_alternative<std::string_view>(sample.data)) {
            auto sv = std::get<std::string_view>(sample.data);
            std::string processed = preprocess_text(std::string(sv), min_length, max_length);
            if (!processed.empty()) {
                // Store processed text (this creates a copy, which is fine for now)
                // In production, use a string pool or arena allocator
            }
        }
    }
    
    return batch;
}

bool DataAcquisitionManager::has_data() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return !sample_queue_.empty();
}

size_t DataAcquisitionManager::queue_size() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return sample_queue_.size();
}

void DataAcquisitionManager::acquisition_loop() {
    for (size_t i = 0; i < sources_.size() && !should_stop_; ++i) {
        const auto& source = sources_[i];
        
        {
            std::lock_guard<std::mutex> lock(progress_mutex_);
            progress_.current_source = source.name;
            progress_.current_operation = "Fetching " + source.type;
        }
        
        log_progress("source_start", source.name, "Processing " + source.type);
        
        // Wait if paused
        while (paused_.load() && !should_stop_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        if (should_stop_) break;
        
        // Fetch based on type
        if (source.type == "web_api") {
            fetch_from_web_api(source);
        } else if (source.type == "local_file") {
            fetch_from_local_file(source);
        } else if (source.type == "local_directory") {
            fetch_from_directory(source);
        }
        
        {
            std::lock_guard<std::mutex> lock(progress_mutex_);
            progress_.completed_sources++;
        }
        
        log_progress("source_complete", source.name, "Done", progress_.processed_items);
    }
    
    running_ = false;
    {
        std::lock_guard<std::mutex> lock(progress_mutex_);
        progress_.is_running = false;
        progress_.current_operation = "Completed";
    }
    
    log("Data acquisition completed");
}

void DataAcquisitionManager::fetch_from_web_api(const DataSourceConfig& source) {
    if (source.url.empty()) {
        log("No URL for web API source: " + source.name);
        return;
    }
    
    log("Fetching from: " + source.url);
    
    auto response = http_get(source.url, source.headers, 30000);
    
    if (!response.success) {
        std::lock_guard<std::mutex> lock(progress_mutex_);
        progress_.errors.push_back("HTTP error from " + source.name);
        return;
    }
    
    // Try to parse as JSON and extract items
    // For now, create a single sample with the response
    TrainingSample sample;
    // Store string (need to manage lifetime properly in production)
    sample.data = std::string_view(""); // Placeholder
    sample.label = 1; // Positive
    sample.source_api = source.name;
    sample.domain = "web";
    
    std::lock_guard<std::mutex> lock(queue_mutex_);
    // sample_queue_.push(sample); // Disabled until proper string storage
    
    std::lock_guard<std::mutex> plock(progress_mutex_);
    progress_.processed_items++;
    progress_.total_items++;
    
    // Rate limiting
    std::this_thread::sleep_for(
        std::chrono::milliseconds(static_cast<int>(1000.0f / source.rate_limit)));
}

void DataAcquisitionManager::fetch_from_local_file(const DataSourceConfig& source) {
    if (source.path.empty() || !std::filesystem::exists(source.path)) {
        log("File not found: " + source.path);
        return;
    }
    
    std::ifstream file(source.path);
    if (!file.is_open()) {
        log("Failed to open: " + source.path);
        return;
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    
    // Create samples from chunks
    const size_t chunk_size = 4096;
    int items = 0;
    
    for (size_t i = 0; i < content.size(); i += chunk_size) {
        std::string chunk = content.substr(i, chunk_size);
        std::string processed = preprocess_text(chunk);
        
        if (!processed.empty()) {
            // Add to queue
            std::lock_guard<std::mutex> lock(queue_mutex_);
            // Create sample with proper string storage
            // For now, just count
            items++;
        }
        
        if (items % 100 == 0) {
            log_progress("progress", source.name, "Processing", items);
        }
    }
    
    std::lock_guard<std::mutex> lock(progress_mutex_);
    progress_.processed_items += items;
    progress_.total_items += items;
}

void DataAcquisitionManager::fetch_from_directory(const DataSourceConfig& source) {
    if (source.path.empty() || !std::filesystem::is_directory(source.path)) {
        log("Directory not found: " + source.path);
        return;
    }
    
    std::vector<std::string> extensions = source.extensions.empty() 
        ? std::vector<std::string>{".txt", ".md", ".json"}
        : source.extensions;
    
    int items = 0;
    
    for (const auto& entry : std::filesystem::recursive_directory_iterator(source.path)) {
        if (should_stop_) break;
        
        if (!entry.is_regular_file()) continue;
        
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        
        bool match = false;
        for (const auto& allowed : extensions) {
            std::string allowed_lower = allowed;
            std::transform(allowed_lower.begin(), allowed_lower.end(), 
                          allowed_lower.begin(), ::tolower);
            if (ext == allowed_lower) {
                match = true;
                break;
            }
        }
        
        if (!match) continue;
        
        // Read file
        std::ifstream file(entry.path());
        if (!file.is_open()) continue;
        
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string content = buffer.str();
        
        // Process in chunks
        const size_t chunk_size = 4096;
        for (size_t i = 0; i < content.size(); i += chunk_size) {
            std::string chunk = content.substr(i, chunk_size);
            std::string processed = preprocess_text(chunk);
            
            if (!processed.empty()) {
                items++;
            }
        }
        
        if (items % 100 == 0) {
            log_progress("progress", source.name, "Processing files", items);
        }
        
        // Check pause
        while (paused_.load() && !should_stop_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    
    std::lock_guard<std::mutex> lock(progress_mutex_);
    progress_.processed_items += items;
    progress_.total_items += items;
}

std::string DataAcquisitionManager::preprocess_text(const std::string& text,
                                                       size_t min_length,
                                                       size_t max_length) {
    if (text.empty()) return "";
    
    // Remove extra whitespace
    std::string result;
    result.reserve(text.size());
    bool last_was_space = false;
    
    for (char c : text) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!last_was_space) {
                result += ' ';
                last_was_space = true;
            }
        } else {
            result += c;
            last_was_space = false;
        }
    }
    
    // Trim
    size_t start = result.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    size_t end = result.find_last_not_of(" \t\n\r");
    result = result.substr(start, end - start + 1);
    
    // Check length
    if (result.size() < min_length) return "";
    if (result.size() > max_length) {
        result = result.substr(0, max_length);
    }
    
    return result;
}

void DataAcquisitionManager::log(const std::string& message) {
    if (log_callback_) {
        log_callback_("{\"type\": \"log\", \"message\": \"" + message + "\"}");
    }
}

void DataAcquisitionManager::log_progress(const std::string& type,
                                           const std::string& source,
                                           const std::string& operation,
                                           int items) {
    if (log_callback_) {
        std::string msg = "{\"type\": \"" + type + "\", " +
                          "\"source\": \"" + source + "\", " +
                          "\"operation\": \"" + operation + "\"";
        if (items > 0) {
            msg += ", \"items\": " + std::to_string(items);
        }
        msg += "}";
        log_callback_(msg);
    }
}

#ifdef _WIN32
DataAcquisitionManager::HttpResponse DataAcquisitionManager::http_get(
    const std::string& url,
    const std::map<std::string, std::string>& headers,
    int timeout_ms) {
    
    HttpResponse response;
    
    // Auto-add https:// prefix if URL lacks scheme
    std::string full_url = url;
    if (url.find("://") == std::string::npos) {
        full_url = "https://" + url;
    }
    
    // Parse URL
    std::string protocol, host, path = "/";
    int port = 443;
    
    size_t protocol_end = full_url.find("://");
    if (protocol_end == std::string::npos) {
        response.success = false;
        return response;
    }
    
    protocol = full_url.substr(0, protocol_end);
    size_t host_start = protocol_end + 3;
    size_t path_start = full_url.find('/', host_start);
    
    if (path_start == std::string::npos) {
        host = full_url.substr(host_start);
    } else {
        host = full_url.substr(host_start, path_start - host_start);
        path = full_url.substr(path_start);
    }
    
    size_t port_colon = host.find(':');
    if (port_colon != std::string::npos) {
        port = std::stoi(host.substr(port_colon + 1));
        host = host.substr(0, port_colon);
    }
    
    // Convert to wide strings
    std::wstring whost(host.begin(), host.end());
    std::wstring wpath(path.begin(), path.end());
    
    // Use WinHTTP
    HINTERNET hSession = WinHttpOpen(L"Q-Mini-DataBot/1.0", 
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME, 
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    
    if (!hSession) {
        response.success = false;
        return response;
    }
    
    HINTERNET hConnect = WinHttpConnect(hSession, whost.c_str(), port, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        response.success = false;
        return response;
    }
    
    DWORD flags = (protocol == "https") ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", wpath.c_str(),
                                            NULL, WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        response.success = false;
        return response;
    }
    
    // Set timeout
    WinHttpSetTimeouts(hRequest, timeout_ms, timeout_ms, timeout_ms, timeout_ms);
    
    // Add headers
    for (const auto& [key, value] : headers) {
        std::wstring wheader((key + ": " + value).begin(), 
                             (key + ": " + value).end());
        WinHttpAddRequestHeaders(hRequest, wheader.c_str(), 
                                 static_cast<DWORD>(wheader.length()),
                                 WINHTTP_ADDREQ_FLAG_ADD);
    }
    
    // Send request
    BOOL result = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                     WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    
    if (!result) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        response.success = false;
        return response;
    }
    
    // Receive response
    result = WinHttpReceiveResponse(hRequest, NULL);
    if (!result) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        response.success = false;
        return response;
    }
    
    // Get status code
    DWORD status_code = 0;
    DWORD size = sizeof(status_code);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status_code, &size,
                        WINHTTP_NO_HEADER_INDEX);
    response.status_code = static_cast<int>(status_code);
    
    // Read body
    std::string body;
    DWORD bytes_available = 0;
    while (WinHttpQueryDataAvailable(hRequest, &bytes_available) && bytes_available > 0) {
        std::vector<char> buffer(bytes_available);
        DWORD bytes_read = 0;
        WinHttpReadData(hRequest, buffer.data(), bytes_available, &bytes_read);
        body.append(buffer.data(), bytes_read);
    }
    response.body = body;
    response.success = (response.status_code >= 200 && response.status_code < 300);
    
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    
    return response;
}
#else
DataAcquisitionManager::HttpResponse DataAcquisitionManager::http_get(
    const std::string& url,
    const std::map<std::string, std::string>& headers,
    int timeout_ms) {
    // Non-Windows: return error for now
    HttpResponse response;
    response.success = false;
    return response;
}
#endif

} // namespace q_mini_wasm_v2::core::training
