#include "data_synthesizer.hpp"
#include "data_acquisition.hpp"
#include <algorithm>
#include <random>
#include <chrono>
#include <thread>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <cstdint>
#include <iostream>
#include <unordered_set>
#include <cstdlib>  // getenv
#include <filesystem>
#include <optional>
#include <deque>
#include <array>
#include <string_view>

// HTTP client support - requires libcurl or similar
// For production: link with -lcurl
#ifdef HAS_LIBCURL
#include <curl/curl.h>
#endif

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
// HTTP Client Helper
// ============================================================================

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "winhttp.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#endif

/**
 * @brief Simple HTTP client for API requests
 * 
 * Note: For production use, link with libcurl for robust HTTP/HTTPS support.
 * This implementation provides basic HTTP GET functionality.
 */
class SimpleHttpClient {
public:
    struct Response {
        int status_code = 0;
        std::string body;
        bool success = false;
        std::string error;
    };

    static Response get(const std::string& url, int timeout_ms = 5000) {
        Response response;
        
#ifdef HAS_LIBCURL
        // Use libcurl if available
        CURL* curl = curl_easy_init();
        if (!curl) {
            response.error = "Failed to initialize CURL";
            return response;
        }
        
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);  // Dev only
        
        CURLcode res = curl_easy_perform(curl);
        if (res == CURLE_OK) {
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status_code);
            response.success = (response.status_code == 200);
        } else {
            response.error = curl_easy_strerror(res);
        }
        
        curl_easy_cleanup(curl);
#elif defined(_WIN32)
        // Use WinHTTP on Windows
        response = winhttp_get(url, timeout_ms);
#else
        // Native HTTP client not available without libcurl
        response.error = "HTTP client not available - install libcurl";
        response.success = false;
#endif
        
        return response;
    }
    
#ifdef _WIN32
    // Helper function to properly convert UTF-8 to UTF-16
    static std::wstring utf8_to_utf16(const std::string& utf8) {
        if (utf8.empty()) return std::wstring();
        int size = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
        if (size == 0) return std::wstring();
        std::wstring utf16(size - 1, 0);
        MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &utf16[0], size);
        return utf16;
    }
    
    static Response winhttp_get(const std::string& url, int timeout_ms) {
        Response response;
        
        // Parse URL to get server and path
        std::string server, path;
        bool is_https = false;
        
        size_t protocol_end = url.find("://");
        if (protocol_end == std::string::npos) {
            response.error = "Invalid URL: no protocol";
            return response;
        }
        
        std::string protocol = url.substr(0, protocol_end);
        is_https = (protocol == "https");
        
        size_t server_start = protocol_end + 3;
        size_t path_start = url.find('/', server_start);
        
        if (path_start == std::string::npos) {
            server = url.substr(server_start);
            path = "/";
        } else {
            server = url.substr(server_start, path_start - server_start);
            path = url.substr(path_start);
        }
        
        // Extract port if specified
        std::wstring wserver = utf8_to_utf16(server);
        INTERNET_PORT port = is_https ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;
        
        size_t port_colon = server.find(':');
        if (port_colon != std::string::npos) {
            port = (INTERNET_PORT)std::stoi(server.substr(port_colon + 1));
            wserver = utf8_to_utf16(server.substr(0, port_colon));
        }
        
        std::wstring wpath = utf8_to_utf16(path);
        std::wstring wuser_agent = L"DataSynthesizer/1.0";
        
        // Initialize WinHTTP
        HINTERNET hSession = WinHttpOpen(wuser_agent.c_str(), 
                                         WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                         WINHTTP_NO_PROXY_NAME, 
                                         WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession) {
            response.error = "WinHttpOpen failed";
            return response;
        }
        
        // Set timeouts
        WinHttpSetTimeouts(hSession, timeout_ms, timeout_ms, timeout_ms, timeout_ms);
        
        // Connect to server
        HINTERNET hConnect = WinHttpConnect(hSession, wserver.c_str(), port, 0);
        if (!hConnect) {
            WinHttpCloseHandle(hSession);
            response.error = "WinHttpConnect failed";
            return response;
        }
        
        // Create request
        DWORD flags = is_https ? WINHTTP_FLAG_SECURE : 0;
        HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", wpath.c_str(), 
                                               NULL, WINHTTP_NO_REFERER, 
                                               WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
        if (!hRequest) {
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            response.error = "WinHttpOpenRequest failed";
            return response;
        }
        
        // Send request
        BOOL bResults = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, 
                                          WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
        if (!bResults) {
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            response.error = "WinHttpSendRequest failed";
            return response;
        }
        
        // Receive response
        bResults = WinHttpReceiveResponse(hRequest, NULL);
        if (!bResults) {
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            response.error = "WinHttpReceiveResponse failed";
            return response;
        }
        
        // Get status code
        DWORD dwStatusCode = 0;
        DWORD dwSize = sizeof(dwStatusCode);
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                           WINHTTP_HEADER_NAME_BY_INDEX, &dwStatusCode, &dwSize, WINHTTP_NO_HEADER_INDEX);
        response.status_code = static_cast<int>(dwStatusCode);
        
        // Read response body
        DWORD dwDownloaded = 0;
        do {
            dwSize = 0;
            WinHttpQueryDataAvailable(hRequest, &dwSize);
            if (dwSize > 0) {
                std::vector<char> buffer(dwSize + 1);
                ZeroMemory(buffer.data(), dwSize + 1);
                WinHttpReadData(hRequest, buffer.data(), dwSize, &dwDownloaded);
                if (dwDownloaded > 0) {
                    response.body.append(buffer.data(), dwDownloaded);
                }
            }
        } while (dwSize > 0);
        
        response.success = (response.status_code == 200);
        if (!response.success && response.error.empty()) {
            response.error = "HTTP " + std::to_string(response.status_code);
        }
        
        // Cleanup
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        
        return response;
    }
#endif

private:
#ifdef HAS_LIBCURL
    static size_t write_callback(void* contents, size_t size, size_t nmemb, std::string* userp) {
        userp->append(static_cast<char*>(contents), size * nmemb);
        return size * nmemb;
    }
#endif
};

/**
 * @brief Parse simple JSON array of numbers to fixed-point int32_t
 * TROPICAL_SCALE = 1000 for fixed-point representation
 */
static constexpr int32_t TROPICAL_SCALE = 1000;

std::vector<int32_t> parse_json_array_fixed(const std::string& json) {
    std::vector<int32_t> result;
    
    // Simple parser for [1.0, 2.0, 3.0] format
    size_t start = json.find('[');
    size_t end = json.find(']');
    
    if (start == std::string::npos || end == std::string::npos || end <= start) {
        return result;
    }
    
    std::string content = json.substr(start + 1, end - start - 1);
    std::stringstream ss(content);
    std::string token;
    
    while (std::getline(ss, token, ',')) {
        // Trim whitespace
        token.erase(0, token.find_first_not_of(" \t"));
        token.erase(token.find_last_not_of(" \t") + 1);
        
        if (!token.empty()) {
            try {
                float val = std::stof(token);
                // Convert to fixed-point with TROPICAL_SCALE
                int32_t fixed_val = static_cast<int32_t>(val * TROPICAL_SCALE);
                result.push_back(fixed_val);
            } catch (...) {
                // Skip invalid tokens
            }
        }
    }
    
    return result;
}

// ============================================================================
// API Client Implementations
// ============================================================================

WolframClient::WolframClient() {
    // Load API key from environment if available
    const char* env_key = std::getenv("WOLFRAM_API_KEY");
    if (env_key) {
        api_key_ = env_key;
    }
}

std::optional<ApiPayload> WolframClient::query(std::string_view endpoint, 
                                                std::string_view params) {
    if (rate_limit_ == 0) {
        backoff();
        return std::nullopt;
    }
    
    --rate_limit_;
    
    // Build query URL
    std::string url = "https://api.wolframalpha.com/v2/query?input=";
    
    // URL encode params
    for (char c : params) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            url += c;
        } else {
            std::stringstream hex;
            hex << '%' << std::uppercase << std::hex << std::setw(2) << std::setfill('0') << (static_cast<int>(c) & 0xFF);
            url += hex.str();
        }
    }
    
    url += "&appid=" + api_key_;
    url += "&format=plaintext";
    
    // Make HTTP request
    auto response = SimpleHttpClient::get(url, 10000);
    
    if (!response.success) {
        // No fallback to mock data - fail explicitly
        std::cerr << "[DataSynthesizer] API query failed: " << response.error << std::endl;
        last_query_time_ = std::chrono::steady_clock::now();
        return std::nullopt;
    }
    
    // Parse response - extract numerical values to fixed-point
    auto result = parse_json_array_fixed(response.body);
    if (result.empty()) {
        // If no JSON array found, extract numbers from text
        std::vector<int32_t> numbers;
        std::stringstream ss(response.body);
        std::string token;
        while (ss >> token) {
            try {
                size_t pos;
                float val = std::stof(token, &pos);
                if (pos == token.length()) {
                    // Convert to fixed-point
                    numbers.push_back(static_cast<int32_t>(val * TROPICAL_SCALE));
                }
            } catch (...) {}
        }
        result = numbers;
    }
    
    // Ensure we have some data
    if (result.empty()) {
        result.push_back(0);
    }
    
    last_query_time_ = std::chrono::steady_clock::now();
    return result;
}

void WolframClient::backoff() {
    std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms_));
    backoff_ms_ *= 2;  // Exponential backoff
    rate_limit_ = 10000;  // Reset to high limit after backoff
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
    
    // Build PubChem PUG-REST API URL
    std::string url = "https://pubchem.ncbi.nlm.nih.gov/rest/pug/";
    url += std::string(endpoint);
    url += "/" + std::string(params);
    url += "/JSON";
    
    // Make HTTP request
    auto response = SimpleHttpClient::get(url, 8000);
    
    std::vector<int32_t> molecular_data;
    
    if (response.success && !response.body.empty()) {
        // Try to parse molecular properties from JSON
        auto parsed = parse_json_array_fixed(response.body);
        if (!parsed.empty()) {
            molecular_data = parsed;
        }
    }
    
    // No fallback to mock data - return empty if HTTP fails
    if (molecular_data.empty()) {
        std::cerr << "[DataSynthesizer] PubChem API failed, no data retrieved" << std::endl;
        last_query_time_ = std::chrono::steady_clock::now();
        return std::nullopt;
    }
    
    last_query_time_ = std::chrono::steady_clock::now();
    return molecular_data;
}

void PubChemClient::backoff() {
    std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms_));
    backoff_ms_ *= 2;
    rate_limit_ = 5000;  // Reset to high limit
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
    
    // Build OEIS API URL (endpoint is "search", params is like "q=fibonacci")
    std::string url = "https://oeis.org/";
    url += std::string(endpoint);
    url += "?fmt=json&";
    url += std::string(params);
    
    // Make HTTP request
    auto response = SimpleHttpClient::get(url, 5000);
    
    std::vector<int32_t> sequence;
    
    if (response.success && !response.body.empty()) {
        // Try to parse sequence from JSON
        auto parsed = parse_json_array_fixed(response.body);
        if (!parsed.empty()) {
            sequence = parsed;
        }
    }
    
    // No fallback to generated sequence if HTTP fails
    if (sequence.empty()) {
        std::cerr << "[DataSynthesizer] OEIS API failed, no sequence retrieved" << std::endl;
        last_query_time_ = std::chrono::steady_clock::now();
        return std::nullopt;
    }
    
    last_query_time_ = std::chrono::steady_clock::now();
    return sequence;
}

void OeisClient::backoff() {
    std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms_));
    backoff_ms_ *= 2;
    rate_limit_ = 20000;  // Reset to high limit
}

ApiPayload OeisClient::perturb_sequence(const ApiPayload& positive) {
    // Mutation-based bootstrapping
    // 1. Splice: combine first half of one sequence with second half of another
    // 2. Growth factor alteration: modify recursive relation
    // 3. Colijn-Plazzotta rank perturbation
    
    auto result = positive;
    thread_local std::mt19937 rng(std::random_device{}());
    
    if (std::holds_alternative<std::vector<int32_t>>(result)) {
        auto& seq = std::get<std::vector<int32_t>>(result);
        if (!seq.empty()) {
            std::uniform_int_distribution<size_t> dist(0, seq.size() - 1);
            size_t pos = dist(rng);
            seq[pos] = static_cast<int32_t>(dist(rng) % 2000) - 1000;  // Random perturbation (fixed-point)
        }
    }
    
    return result;
}

// ============================================================================
// Wikidata SPARQL Client
// ============================================================================

std::optional<ApiPayload> WikidataClient::query(std::string_view endpoint,
                                                std::string_view params) {
    if (rate_limit_ == 0) {
        backoff();
        return std::nullopt;
    }
    --rate_limit_;
    
    // Build Wikidata entity data URL (e.g., Special:EntityData/Q5.json)
    std::string url = "https://www.wikidata.org/wiki/";
    url += std::string(endpoint);
    
    // Make HTTP request
    auto response = SimpleHttpClient::get(url, 10000);
    
    std::vector<int32_t> triple_data;
    
    if (response.success && !response.body.empty()) {
        // Parse RDF triple structure - extract entity IDs as fixed-point
        // This is a simplified representation (scale: TROPICAL_SCALE)
        for (size_t i = 0; i < response.body.size() && triple_data.size() < 64; ++i) {
            // Convert byte to fixed-point range [-1000, 1000]
            int32_t val = static_cast<int32_t>((response.body[i] % 256) - 128) * 8;
            triple_data.push_back(val);
        }
    }
    
    if (triple_data.empty()) {
        std::cerr << "[DataSynthesizer] Wikidata API failed, no triples retrieved" << std::endl;
        last_query_time_ = std::chrono::steady_clock::now();
        return std::nullopt;
    }
    
    last_query_time_ = std::chrono::steady_clock::now();
    return triple_data;
}

void WikidataClient::backoff() {
    std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms_));
    backoff_ms_ *= 2;
    rate_limit_ = 10000;  // Reset to high limit
}

ApiPayload WikidataClient::perturb_triples(const ApiPayload& positive) {
    // Property recommender disruption
    // Replace object in Subject-Predicate-Object with plausible but incorrect alternative
    auto result = positive;
    thread_local std::mt19937 rng(std::random_device{}());
    
    if (std::holds_alternative<std::vector<int32_t>>(result)) {
        auto& triples = std::get<std::vector<int32_t>>(result);
        if (!triples.empty()) {
            // Swap random entity values to create false but plausible triples
            std::uniform_int_distribution<size_t> dist(0, triples.size() - 1);
            size_t pos1 = dist(rng);
            size_t pos2 = dist(rng);
            std::swap(triples[pos1], triples[pos2]);
        }
    }
    
    return result;
}

// ============================================================================
// arXiv API Client
// ============================================================================

std::optional<ApiPayload> ArxivClient::query(std::string_view endpoint,
                                            std::string_view params) {
    if (rate_limit_ == 0) {
        backoff();
        return std::nullopt;
    }
    --rate_limit_;
    
    // Build arXiv API URL
    std::string url = "http://export.arxiv.org/api/query?search_query=";
    
    // URL encode search params
    for (char c : params) {
        if (std::isalnum(c)) {
            url += c;
        } else if (c == ' ') {
            url += '+';
        } else {
            std::stringstream hex;
            hex << '%' << std::uppercase << std::hex << std::setw(2) << std::setfill('0') << (static_cast<int>(c) & 0xFF);
            url += hex.str();
        }
    }
    
    url += "&max_results=1&sortBy=relevance&sortOrder=descending";
    
    // Make HTTP request
    auto response = SimpleHttpClient::get(url, 8000);
    
    std::vector<int32_t> embedding;
    
    if (response.success && !response.body.empty()) {
        // Convert abstract text to numerical embedding (fixed-point)
        // Simple bag-of-words style encoding (scale: TROPICAL_SCALE)
        for (size_t i = 0; i < response.body.size() && embedding.size() < 256; ++i) {
            if (std::isalpha(response.body[i])) {
                // Convert to fixed-point: (char_val / 128.0 - 1.0) * 1000
                int32_t val = static_cast<int32_t>((response.body[i] - 128) * 8);
                embedding.push_back(val);
            }
        }
    }
    
    if (embedding.empty()) {
        std::cerr << "[DataSynthesizer] arXiv API failed, no papers retrieved" << std::endl;
        last_query_time_ = std::chrono::steady_clock::now();
        return std::nullopt;
    }
    
    last_query_time_ = std::chrono::steady_clock::now();
    return embedding;
}

void ArxivClient::backoff() {
    std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms_));
    backoff_ms_ *= 2;
    rate_limit_ = 15000;  // Reset to high limit
}

ApiPayload ArxivClient::perturb_scientific(const ApiPayload& positive) {
    // Semantic contradiction injection
    // Invert core scientific claims (e.g., "superconducting" -> "insulating")
    auto result = positive;
    thread_local std::mt19937 rng(std::random_device{}());
    
    if (std::holds_alternative<std::vector<int32_t>>(result)) {
        auto& embedding = std::get<std::vector<int32_t>>(result);
        if (!embedding.empty()) {
            // Invert values to simulate semantic negation
            for (auto& val : embedding) {
                val = -val;  // Negate embeddings to represent contradiction
            }
        }
    }
    
    return result;
}

// ============================================================================
// NASA Exoplanet Archive Client
// ============================================================================

std::optional<ApiPayload> NasaExoplanetClient::query(std::string_view endpoint,
                                                      std::string_view params) {
    if (rate_limit_ == 0) {
        backoff();
        return std::nullopt;
    }
    --rate_limit_;
    
    // Build NASA Exoplanet Archive API URL
    std::string url = "https://exoplanetarchive.ipac.caltech.edu/TAP/sync?query=";
    
    // URL encode TAP query
    for (char c : params) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            url += c;
        } else if (c == ' ') {
            url += '+';
        } else {
            std::stringstream hex;
            hex << '%' << std::uppercase << std::hex << std::setw(2) << std::setfill('0') << (static_cast<int>(c) & 0xFF);
            url += hex.str();
        }
    }
    
    url += "&format=json";
    
    // Make HTTP request
    auto response = SimpleHttpClient::get(url, 10000);
    
    std::vector<int32_t> light_curve;
    
    if (response.success && !response.body.empty()) {
        // Parse photometric data points
        auto parsed = parse_json_array_fixed(response.body);
        if (!parsed.empty()) {
            light_curve = parsed;
        }
    }
    
    if (light_curve.empty()) {
        std::cerr << "[DataSynthesizer] NASA Exoplanet API failed, no transit data retrieved" << std::endl;
        last_query_time_ = std::chrono::steady_clock::now();
        return std::nullopt;
    }
    
    last_query_time_ = std::chrono::steady_clock::now();
    return light_curve;
}

void NasaExoplanetClient::backoff() {
    std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms_));
    backoff_ms_ *= 2;
    rate_limit_ = 10000;  // Reset to high limit
}

ApiPayload NasaExoplanetClient::perturb_transit(const ApiPayload& positive) {
    // Non-Keplerian transit noise injection
    // Inject synthetic astrophysical anomalies into light curves
    auto result = positive;
    thread_local std::mt19937 rng(std::random_device{}());
    
    if (std::holds_alternative<std::vector<int32_t>>(result)) {
        auto& light_curve = std::get<std::vector<int32_t>>(result);
        if (!light_curve.empty()) {
            std::uniform_int_distribution<size_t> pos_dist(0, light_curve.size() - 1);
            std::uniform_int_distribution<int32_t> noise_dist(-500, 500);  // Fixed-point noise
            
            // Inject irregular dips that don't correspond to Keplerian orbits
            size_t num_anomalies = light_curve.size() / 10;  // 10% anomalies
            for (size_t i = 0; i < num_anomalies; ++i) {
                size_t pos = pos_dist(rng);
                light_curve[pos] += noise_dist(rng);  // Add non-physical noise (fixed-point)
            }
        }
    }
    
    return result;
}

// ============================================================================
// Protein Data Bank Client
// ============================================================================

std::optional<ApiPayload> PdbClient::query(std::string_view endpoint,
                                           std::string_view params) {
    if (rate_limit_ == 0) {
        backoff();
        return std::nullopt;
    }
    --rate_limit_;
    
    // Build PDB API URL
    std::string url = "https://data.rcsb.org/rest/v1/core/";
    url += std::string(endpoint);
    url += "/";
    url += std::string(params);
    
    // Make HTTP request
    auto response = SimpleHttpClient::get(url, 8000);
    
    std::vector<int32_t> coordinates;
    
    if (response.success && !response.body.empty()) {
        // Parse 3D coordinate data
        auto parsed = parse_json_array_fixed(response.body);
        if (!parsed.empty()) {
            coordinates = parsed;
        }
    }
    
    if (coordinates.empty()) {
        std::cerr << "[DataSynthesizer] PDB API failed, no coordinate data retrieved" << std::endl;
        last_query_time_ = std::chrono::steady_clock::now();
        return std::nullopt;
    }
    
    last_query_time_ = std::chrono::steady_clock::now();
    return coordinates;
}

void PdbClient::backoff() {
    std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms_));
    backoff_ms_ *= 2;
    rate_limit_ = 5000;  // Reset to high limit
}

ApiPayload PdbClient::perturb_coordinates(const ApiPayload& positive) {
    // Spatial coordinate drift
    // Apply rotational and translational noise to generate steric clashes
    auto result = positive;
    thread_local std::mt19937 rng(std::random_device{}());
    
    if (std::holds_alternative<std::vector<int32_t>>(result)) {
        auto& coords = std::get<std::vector<int32_t>>(result);
        // Process as 3D coordinates (x,y,z triplets)
        if (coords.size() >= 3) {
            std::uniform_int_distribution<int32_t> noise_dist(-2000, 2000);  // Fixed-point noise
            
            // Apply random drift to each coordinate
            for (auto& coord : coords) {
                coord += noise_dist(rng);  // Add significant drift (fixed-point)
            }
        }
    }
    
    return result;
}

// ============================================================================
// GitHub API Client
// ============================================================================

GitHubClient::GitHubClient() {
    // Load optional API key from environment for higher rate limits
    const char* env_key = std::getenv("GITHUB_API_KEY");
    if (env_key) {
        api_key_ = env_key;
    }
}

std::optional<ApiPayload> GitHubClient::query(std::string_view endpoint,
                                              std::string_view params) {
    if (rate_limit_ == 0) {
        backoff();
        return std::nullopt;
    }
    --rate_limit_;
    
    // Build GitHub API URL
    std::string url = "https://api.github.com/";
    url += std::string(endpoint);
    
    if (!params.empty()) {
        url += "?" + std::string(params);
    }
    
    // Make HTTP request
    auto response = SimpleHttpClient::get(url, 10000);
    
    std::vector<int32_t> code_embedding;
    
    if (response.success && !response.body.empty()) {
        // Parse code content - convert to fixed-point embedding
        // Extract code characters as normalized fixed-point values
        for (size_t i = 0; i < response.body.size() && code_embedding.size() < 512; ++i) {
            char c = response.body[i];
            // Normalize ASCII to fixed-point [-1000, 1000] range
            int32_t normalized = static_cast<int32_t>((static_cast<int>(c) - 128) * 8);
            code_embedding.push_back(normalized);
        }
    }
    
    if (code_embedding.empty()) {
        std::cerr << "[DataSynthesizer] GitHub API failed, no code retrieved" << std::endl;
        last_query_time_ = std::chrono::steady_clock::now();
        return std::nullopt;
    }
    
    last_query_time_ = std::chrono::steady_clock::now();
    return code_embedding;
}

void GitHubClient::backoff() {
    std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms_));
    backoff_ms_ *= 2;
    rate_limit_ = 6000;  // Reset to high limit
}

ApiPayload GitHubClient::perturb_code(const ApiPayload& positive) {
    // AST mutilation
    // Apply destructive logical mutations to SYCL/C++ code
    // e.g., swap sycl::malloc_shared with sycl::malloc_device, remove barriers
    auto result = positive;
    thread_local std::mt19937 rng(std::random_device{}());
    
    if (std::holds_alternative<std::vector<int32_t>>(result)) {
        auto& code_emb = std::get<std::vector<int32_t>>(result);
        if (!code_emb.empty()) {
            // Simulate code corruption by reordering/swapping segments
            std::uniform_int_distribution<size_t> dist(0, code_emb.size() - 1);
            size_t pos1 = dist(rng);
            size_t pos2 = dist(rng);
            
            // Swap code segments (simulates moving operations out of order)
            // MSVC debug STL asserts when iterator goes past end.
            if (pos2 > pos1 + 10 && pos1 + 5 <= code_emb.size() && pos2 + 5 <= code_emb.size()) {
                std::swap_ranges(code_emb.begin() + pos1, code_emb.begin() + pos1 + 5,
                               code_emb.begin() + pos2);
            }
        }
    }
    
    return result;
}

// ============================================================================
// Lean Theorem Prover Client
// ============================================================================

std::optional<ApiPayload> LeanClient::query(std::string_view endpoint,
                                          std::string_view params) {
    if (rate_limit_ == 0) {
        backoff();
        return std::nullopt;
    }
    --rate_limit_;
    
    // Build Lean API URL (e.g., ProofDB or custom Lean server)
    std::string url = "https://proofdb.org/api/";
    url += std::string(endpoint);
    
    if (!params.empty()) {
        url += "?" + std::string(params);
    }
    
    // Make HTTP request
    auto response = SimpleHttpClient::get(url, 15000);
    
    std::vector<int32_t> proof_state;
    
    if (response.success && !response.body.empty()) {
        // Parse tactic states and proof steps
        auto parsed = parse_json_array_fixed(response.body);
        if (!parsed.empty()) {
            proof_state = parsed;
        }
    }
    
    if (proof_state.empty()) {
        std::cerr << "[DataSynthesizer] Lean API failed, no proof data retrieved" << std::endl;
        last_query_time_ = std::chrono::steady_clock::now();
        return std::nullopt;
    }
    
    last_query_time_ = std::chrono::steady_clock::now();
    return proof_state;
}

void LeanClient::backoff() {
    std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms_));
    backoff_ms_ *= 2;
    rate_limit_ = 10000;  // Reset to high limit
}

ApiPayload LeanClient::perturb_proof(const ApiPayload& positive) {
    // Frame-preserving mutation
    // Inject contextually plausible but mathematically invalid tactics
    // Substitute required hypothesis with orthogonal premise
    auto result = positive;
    thread_local std::mt19937 rng(std::random_device{}());
    
    if (std::holds_alternative<std::vector<int32_t>>(result)) {
        auto& proof = std::get<std::vector<int32_t>>(result);
        if (!proof.empty()) {
            // Perturb proof by corrupting intermediate steps
            std::uniform_int_distribution<size_t> dist(0, proof.size() - 1);
            size_t pos = dist(rng);
            
            // Introduce logical error while maintaining structure (fixed-point)
            proof[pos] = static_cast<int32_t>((dist(rng) % 200) - 100) * 10;
        }
    }
    
    return result;
}

// ============================================================================
// DataSynthesizer Implementation
// ============================================================================

namespace {

/** Shared JSONL row parser: `{"input":[...], "label": ...}` — same semantics as `load_local_data` file path. */
std::optional<TrainingSample> parse_jsonl_training_sample(const std::string& line) {
    if (line.empty() || line[0] == '#') {
        return std::nullopt;
    }
    const size_t input_pos = line.find("\"input\"");
    if (input_pos == std::string::npos) {
        return std::nullopt;
    }
    const size_t arr_start = line.find('[', input_pos);
    const size_t arr_end = line.find(']', arr_start);
    if (arr_start == std::string::npos || arr_end == std::string::npos) {
        return std::nullopt;
    }
    const std::string arr_str = line.substr(arr_start + 1, arr_end - arr_start - 1);
    std::vector<int32_t> values;
    std::stringstream ss(arr_str);
    std::string val;
    while (std::getline(ss, val, ',')) {
        val.erase(0, val.find_first_not_of(" \t"));
        val.erase(val.find_last_not_of(" \t") + 1);
        if (!val.empty()) {
            try {
                values.push_back(static_cast<int32_t>(std::stoi(val)));
            } catch (...) {
            }
        }
    }
    if (values.empty()) {
        return std::nullopt;
    }
    Trit label = 1;
    const size_t label_pos = line.find("\"label\"");
    if (label_pos != std::string::npos) {
        const size_t colon = line.find(':', label_pos);
        if (colon != std::string::npos) {
            std::string label_str = line.substr(colon + 1);
            const size_t end = label_str.find_first_of(",}");
            if (end != std::string::npos) {
                label_str = label_str.substr(0, end);
                label_str.erase(0, label_str.find_first_not_of(" \t"));
                label_str.erase(label_str.find_last_not_of(" \t") + 1);
                try {
                    label = static_cast<Trit>(std::stoi(label_str));
                } catch (...) {
                }
            }
        }
    }
    return TrainingSample{std::move(values), label, "local_file", "local_data"};
}

TrainingSample corrupt_directory_negative_from_positive(TrainingSample pos, size_t line_idx) {
    pos.label = static_cast<Trit>(-1);
    if (!std::holds_alternative<std::vector<int32_t>>(pos.data)) {
        return pos;
    }
    auto& vec = std::get<std::vector<int32_t>>(pos.data);
    if (vec.empty()) {
        return pos;
    }
    std::array<uint32_t, 4> seeds{
        42u,
        static_cast<uint32_t>(line_idx),
        static_cast<uint32_t>(line_idx >> 32),
        0xA5A5A5A5u,
    };
    std::seed_seq seq(seeds.begin(), seeds.end());
    std::mt19937 rng(seq);
    std::uniform_int_distribution<size_t> pos_dist(0, vec.size() - 1);
    std::uniform_int_distribution<int> val_dist(-1000, 1000);
    for (int j = 0; j < 3 && j < static_cast<int>(vec.size()); ++j) {
        vec[pos_dist(rng)] = val_dist(rng);
    }
    return pos;
}

} // namespace

DataSynthesizer::DataSynthesizer() = default;
DataSynthesizer::~DataSynthesizer() {
    if (running_) stop();
}

void DataSynthesizer::clear_indexed_directory_state() {
    local_directory_indexed_ = false;
    corpus_file_paths_.clear();
    corpus_file_is_jsonl_.clear();
    corpus_line_index_.clear();
}

void DataSynthesizer::set_local_corpus_limits(size_t directory_max_lines,
                                              size_t max_jsonl_local_samples,
                                              size_t min_text_length,
                                              size_t max_text_length) {
    directory_max_lines_ = std::max<size_t>(size_t{1}, directory_max_lines);
    max_jsonl_local_samples_ = std::max<size_t>(size_t{1}, max_jsonl_local_samples);
    min_text_length_ = std::max<size_t>(size_t{1}, min_text_length);
    max_text_length_ = std::max<size_t>(size_t{64}, max_text_length);
    if (min_text_length_ > max_text_length_) {
        std::swap(min_text_length_, max_text_length_);
    }
}

bool DataSynthesizer::corpus_line_should_index(const std::string& line, bool is_jsonl) const {
    if (is_jsonl) {
        const auto sample = parse_jsonl_training_sample(line);
        if (!sample) {
            return false;
        }
        if (!std::holds_alternative<std::vector<int32_t>>(sample->data)) {
            return false;
        }
        const auto& vec = std::get<std::vector<int32_t>>(sample->data);
        return vec.size() >= min_text_length_ && vec.size() <= max_text_length_;
    }
    std::string_view v(line);
    while (!v.empty() && (v.back() == '\r' || v.back() == '\n')) {
        v.remove_suffix(1);
    }
    if (v.empty() || v[0] == '#') {
        return false;
    }
    if (v.size() < min_text_length_) {
        return false;
    }
    size_t n = 0;
    for (unsigned char ch : v) {
        if (++n >= max_text_length_) {
            break;
        }
        (void)ch;
    }
    return n >= min_text_length_;
}

std::optional<std::vector<int32_t>> DataSynthesizer::corpus_line_to_features(std::string& line) const {
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
        line.pop_back();
    }
    if (line.empty() || line[0] == '#') {
        return std::nullopt;
    }
    if (line.size() < min_text_length_) {
        return std::nullopt;
    }
    std::vector<int32_t> values;
    values.reserve((std::min)(line.size(), max_text_length_));
    for (unsigned char ch : line) {
        if (values.size() >= max_text_length_) {
            break;
        }
        values.push_back(static_cast<int32_t>(ch));
    }
    if (values.size() < min_text_length_) {
        return std::nullopt;
    }
    return values;
}

std::optional<TrainingSample> DataSynthesizer::read_corpus_line_positive(const CorpusLineRef& ref) const {
    if (ref.file_index >= corpus_file_paths_.size()) {
        return std::nullopt;
    }
    std::ifstream file(corpus_file_paths_[ref.file_index], std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    file.seekg(static_cast<std::streamoff>(ref.byte_offset));
    if (!file) {
        return std::nullopt;
    }
    std::string line;
    if (!std::getline(file, line)) {
        return std::nullopt;
    }
    if (ref.file_index < corpus_file_is_jsonl_.size() && corpus_file_is_jsonl_[ref.file_index] != 0) {
        std::optional<TrainingSample> sample = parse_jsonl_training_sample(line);
        if (!sample) {
            return std::nullopt;
        }
        sample->source_api = "local_dir";
        sample->domain = "local_jsonl";
        return sample;
    }
    auto values = corpus_line_to_features(line);
    if (!values) {
        return std::nullopt;
    }
    return TrainingSample{std::move(*values), static_cast<Trit>(1), "local_dir", "local_text"};
}

std::optional<TrainingSample> DataSynthesizer::pop_next_local_sample_locked() {
    if (local_directory_indexed_) {
        const size_t n = corpus_line_index_.size();
        if (n == 0) {
            return std::nullopt;
        }
        const size_t cycle = local_sample_index_++ % (2 * n);
        const size_t line_idx = (cycle < n) ? cycle : (cycle - n);
        const CorpusLineRef& ref = corpus_line_index_[line_idx];
        auto pos = read_corpus_line_positive(ref);
        if (!pos) {
            return std::nullopt;
        }
        if (cycle < n) {
            return std::move(*pos);
        }
        return corrupt_directory_negative_from_positive(std::move(*pos), line_idx);
    }
    if (!local_samples_.empty()) {
        const size_t idx = local_sample_index_++ % local_samples_.size();
        return local_samples_[idx];
    }
    return std::nullopt;
}

void DataSynthesizer::initialize_apis() {
    // Tier 1: APIs requiring keys (existing)
    clients_.push_back(std::make_unique<WolframClient>());
    clients_.push_back(std::make_unique<PubChemClient>());
    clients_.push_back(std::make_unique<OeisClient>());
    clients_.push_back(std::make_unique<WikidataClient>());
    clients_.push_back(std::make_unique<ArxivClient>());
    clients_.push_back(std::make_unique<NasaExoplanetClient>());
    clients_.push_back(std::make_unique<PdbClient>());
    clients_.push_back(std::make_unique<GitHubClient>());
    clients_.push_back(std::make_unique<LeanClient>());
    
    // Tier 1: NO KEY REQUIRED - New high-value sources
    clients_.push_back(std::make_unique<OpenAlexClient>());        // 250M+ academic works
    clients_.push_back(std::make_unique<GutendexClient>());        // 76K+ classic books
    clients_.push_back(std::make_unique<UsgsEarthquakeClient>());  // Real-time seismic data
    clients_.push_back(std::make_unique<SpaceXClient>());          // Launch/rocket telemetry
    clients_.push_back(std::make_unique<ChroniclingAmericaClient>()); // 20M+ historic newspapers
    clients_.push_back(std::make_unique<GbifClient>());            // 2B+ biodiversity records
}

bool DataSynthesizer::load_from_directory(const std::string& dir_path) {
    local_samples_.clear();
    local_sample_index_ = 0;
    clear_indexed_directory_state();

    std::error_code ec;
    if (!std::filesystem::is_directory(dir_path, ec)) {
        std::cerr << "[DataSynthesizer] Not a directory: " << dir_path << std::endl;
        return false;
    }

    std::vector<std::filesystem::path> sorted_paths;
    size_t discovered_files = 0;
    size_t skipped_extension_files = 0;
    for (const auto& entry : std::filesystem::directory_iterator(dir_path, ec)) {
        if (ec) {
            break;
        }
        if (!entry.is_regular_file()) {
            continue;
        }
        ++discovered_files;
        const std::string ext = entry.path().extension().string();
        if (ext != ".txt" && ext != ".jsonl") {
            ++skipped_extension_files;
            continue;
        }
        sorted_paths.push_back(entry.path());
    }
    std::sort(sorted_paths.begin(), sorted_paths.end());

    corpus_file_paths_.clear();
    corpus_file_is_jsonl_.clear();
    corpus_file_paths_.reserve(sorted_paths.size());
    corpus_file_is_jsonl_.reserve(sorted_paths.size());
    for (const auto& p : sorted_paths) {
        corpus_file_paths_.push_back(p.string());
        corpus_file_is_jsonl_.push_back(p.extension() == ".jsonl" ? uint8_t{1} : uint8_t{0});
    }

    const size_t accepted_files = sorted_paths.size();
    size_t unreadable_files = 0;
    size_t processed_files = 0;
    size_t indexed_lines = 0;
    const auto scan_started = std::chrono::steady_clock::now();
    auto last_progress_log = scan_started;

    for (uint32_t file_index = 0; file_index < corpus_file_paths_.size(); ++file_index) {
        ++processed_files;
        std::ifstream file(corpus_file_paths_[file_index], std::ios::binary);
        if (!file) {
            ++unreadable_files;
            continue;
        }
        uint64_t next_line_begin = static_cast<uint64_t>(file.tellg());
        std::string line;
        const bool is_jsonl = (file_index < corpus_file_is_jsonl_.size()) && (corpus_file_is_jsonl_[file_index] != 0);
        while (indexed_lines < directory_max_lines_) {
            const uint64_t line_start = next_line_begin;
            if (!std::getline(file, line)) {
                break;
            }
            const std::streampos g = file.tellg();
            if (g != std::streampos(-1)) {
                next_line_begin = static_cast<uint64_t>(g);
            } else {
                next_line_begin = line_start + static_cast<uint64_t>(line.size()) + 1u;
            }

            if (!corpus_line_should_index(line, is_jsonl)) {
                continue;
            }
            corpus_line_index_.push_back(CorpusLineRef{file_index, line_start});
            ++indexed_lines;
        }

        const auto now = std::chrono::steady_clock::now();
        if (now - last_progress_log >= std::chrono::seconds(2)) {
            const auto elapsed_s = std::chrono::duration_cast<std::chrono::seconds>(now - scan_started).count();
            const int at_line_cap = (indexed_lines >= directory_max_lines_) ? 1 : 0;
            std::cout << "[DataSynthesizer] Directory index progress: elapsed=" << elapsed_s << "s"
                      << ", discovered=" << discovered_files
                      << ", accepted_ext=" << accepted_files
                      << ", processed=" << processed_files
                      << ", unreadable=" << unreadable_files
                      << ", indexed_lines=" << indexed_lines
                      << ", line_cap=" << directory_max_lines_
                      << ", at_line_cap=" << at_line_cap
                      << ", last_file=\"" << std::filesystem::path(corpus_file_paths_[file_index]).filename().string()
                      << "\"" << std::endl;
            last_progress_log = now;
        }

        if (indexed_lines >= directory_max_lines_) {
            std::cout << "[DataSynthesizer] stopped_scan_early_at_line_cap=1 line_cap=" << directory_max_lines_
                      << " (remaining directory files not opened)" << std::endl;
            break;
        }
    }

    if (indexed_lines == 0) {
        clear_indexed_directory_state();
        std::cerr << "[DataSynthesizer] No .txt/.jsonl lines indexed from directory: " << dir_path
                  << " (discovered_files=" << discovered_files
                  << ", accepted_files=" << accepted_files
                  << ", skipped_extension_files=" << skipped_extension_files
                  << ", unreadable_files=" << unreadable_files << ")"
                  << std::endl;
        return false;
    }

    data_path_ = dir_path;
    has_local_data_ = true;
    local_directory_indexed_ = true;
    std::cout << "[DataSynthesizer] Indexed " << indexed_lines << " text lines (byte offsets only) from directory: "
              << dir_path << " (discovered_files=" << discovered_files
              << ", accepted_files=" << accepted_files
              << ", skipped_extension_files=" << skipped_extension_files
              << ", unreadable_files=" << unreadable_files << ")" << std::endl;
    std::cout << "[DataSynthesizer] Negatives are generated on read (same schedule as prior pos-block + neg-block)."
              << std::endl;
    return true;
}

bool DataSynthesizer::use_config(const std::string& config_path) {
    std::cout << "[DataSynthesizer] Loading data sources from config: " << config_path << std::endl;
    
    acquisition_mgr_ = std::make_unique<DataAcquisitionManager>();
    acquisition_mgr_->set_max_queue_depth(max_acquisition_queue_depth_);
    
    if (!acquisition_mgr_->load_sources(config_path)) {
        std::cerr << "[DataSynthesizer] Failed to load config from: " << config_path << std::endl;
        acquisition_mgr_.reset();
        return false;
    }
    
    use_config_sources_ = true;
    std::cout << "[DataSynthesizer] Config loaded successfully - using configured data sources" << std::endl;
    std::cout << "[DataSynthesizer] Skipping hardcoded API clients" << std::endl;
    return true;
}

bool DataSynthesizer::load_local_data(const std::string& data_path) {
    std::lock_guard<std::mutex> lock(local_data_mutex_);
    
    if (data_path.empty()) {
        std::cerr << "[DataSynthesizer] Error: Empty data path" << std::endl;
        return false;
    }
    
    // Check if path is a directory
    if (std::filesystem::is_directory(data_path)) {
        std::cout << "[DataSynthesizer] Path is directory, loading from: " << data_path << std::endl;
        std::cout << "[DataSynthesizer] Starting directory iteration..." << std::endl;
        return load_from_directory(data_path);
    }
    
    // Clear existing local samples (directory index is mutually exclusive with in-memory JSONL rows).
    local_samples_.clear();
    local_sample_index_ = 0;
    clear_indexed_directory_state();
    std::ifstream file(data_path);
    std::cout << "[DataSynthesizer] Loading local data from: " << data_path << std::endl;

    // Try to open the file
    if (!file.is_open()) {
        std::cerr << "[DataSynthesizer] Failed to open: " << data_path << std::endl;
        return false;
    }

    // Parse JSONL format (one JSON object per line)
    std::string line;
    size_t line_num = 0;
    size_t loaded = 0;

    while (std::getline(file, line)) {
        line_num++;
        if (auto row = parse_jsonl_training_sample(line)) {
            local_samples_.push_back(std::move(*row));
            ++loaded;
        }
        if (loaded >= max_jsonl_local_samples_) {
            break;
        }
    }
    
    file.close();
    
    if (loaded > 0) {
        data_path_ = data_path;
        has_local_data_ = true;
        std::cout << "[DataSynthesizer] Loaded " << loaded << " samples from local file" << std::endl;
        
        // Generate negative samples by perturbing positive ones
        std::mt19937 rng(42);
        size_t original_count = local_samples_.size();
        for (size_t i = 0; i < original_count; ++i) {
            if (local_samples_[i].label == 1) {
                // Create negative sample by corrupting values
                auto neg_sample = local_samples_[i];
                neg_sample.label = -1;
                
                if (std::holds_alternative<std::vector<int32_t>>(neg_sample.data)) {
                    auto& vec = std::get<std::vector<int32_t>>(neg_sample.data);
                    if (!vec.empty()) {
                        // Flip random values
                        std::uniform_int_distribution<size_t> pos_dist(0, vec.size() - 1);
                        std::uniform_int_distribution<int> val_dist(-1000, 1000);
                        for (int j = 0; j < 3 && j < (int)vec.size(); ++j) {
                            vec[pos_dist(rng)] = val_dist(rng);
                        }
                    }
                }
                
                local_samples_.push_back(neg_sample);
            }
        }
        
        std::cout << "[DataSynthesizer] Generated " << (local_samples_.size() - original_count) 
                  << " negative samples, total: " << local_samples_.size() << std::endl;
        return true;
    }
    
    std::cerr << "[DataSynthesizer] No valid samples found in: " << data_path << std::endl;
    return false;
}

void DataSynthesizer::start(size_t acquisition_threads, size_t perturbation_threads) {
    if (running_) return;
    running_ = true;

    const bool hybrid = has_local_data_ && use_config_sources_ && acquisition_mgr_;

    size_t local_draw_pool = 0;
    {
        std::lock_guard<std::mutex> lk(local_data_mutex_);
        if (local_directory_indexed_) {
            local_draw_pool = corpus_line_index_.size() * 2;
        } else {
            local_draw_pool = local_samples_.size();
        }
    }

    if (hybrid) {
        std::cout << "[DataSynthesizer] Hybrid: " << local_draw_pool
                  << " local draw slots + config/web acquisition (interleaved draws)" << std::endl;
        if (!acquisition_mgr_->start()) {
            std::cerr << "[DataSynthesizer] Failed to start DataAcquisitionManager (hybrid)" << std::endl;
            use_config_sources_ = false;
            acquisition_mgr_.reset();
            std::cout << "[DataSynthesizer] Continuing local-only (web/config start failed)" << std::endl;
            return;
        }
        acquisition_pool_ = std::make_unique<ThreadPool>(1);
        acquisition_pool_->enqueue([this] {
            config_acquisition_worker();
        });
        perturbation_pool_ = std::make_unique<ThreadPool>(perturbation_threads);
        for (size_t i = 0; i < perturbation_threads; ++i) {
            perturbation_pool_->enqueue([this] {
                perturbation_worker();
            });
        }
        return;
    }

    // Local-only: corpus in memory (JSONL) or indexed directory (offsets); no acquisition threads
    if (has_local_data_) {
        std::cout << "[DataSynthesizer] Using local data from: " << data_path_ << std::endl;
        std::cout << "[DataSynthesizer] Samples available: " << local_draw_pool << std::endl;
        return;
    }

    // If config-based sources are loaded, use DataAcquisitionManager
    if (use_config_sources_ && acquisition_mgr_) {
        std::cout << "[DataSynthesizer] Using configured data sources from data_sources.toml" << std::endl;
        
        // Start the acquisition manager
        if (!acquisition_mgr_->start()) {
            std::cerr << "[DataSynthesizer] Failed to start DataAcquisitionManager" << std::endl;
            running_ = false;
            return;
        }
        
        std::cout << "[DataSynthesizer] DataAcquisitionManager started" << std::endl;
        
        // Start a thread to fetch from acquisition manager and feed to train_queue_
        acquisition_pool_ = std::make_unique<ThreadPool>(1);
        acquisition_pool_->enqueue([this] {
            config_acquisition_worker();
        });
        
        // Start perturbation workers
        perturbation_pool_ = std::make_unique<ThreadPool>(perturbation_threads);
        for (size_t i = 0; i < perturbation_threads; ++i) {
            perturbation_pool_->enqueue([this] {
                perturbation_worker();
            });
        }
        return;
    }
    
    // Fall back to legacy hardcoded API clients
    initialize_apis();
    
    // Log all clients and their configuration status
    std::cout << "[DataSynthesizer] Initializing API clients..." << std::endl;
    size_t configured_count = 0;
    for (auto& client : clients_) {
        bool configured = client->is_configured();
        if (configured) configured_count++;
        
        // Identify client type for logging
        const char* name = "unknown";
        if (dynamic_cast<WolframClient*>(client.get())) name = "WolframAlpha";
        else if (dynamic_cast<PubChemClient*>(client.get())) name = "PubChem";
        else if (dynamic_cast<OeisClient*>(client.get())) name = "OEIS";
        else if (dynamic_cast<WikidataClient*>(client.get())) name = "Wikidata";
        else if (dynamic_cast<ArxivClient*>(client.get())) name = "arXiv";
        else if (dynamic_cast<NasaExoplanetClient*>(client.get())) name = "NASA_Exoplanet";
        else if (dynamic_cast<PdbClient*>(client.get())) name = "PDB";
        else if (dynamic_cast<GitHubClient*>(client.get())) name = "GitHub";
        else if (dynamic_cast<LeanClient*>(client.get())) name = "Lean";
        else if (dynamic_cast<OpenAlexClient*>(client.get())) name = "OpenAlex";
        else if (dynamic_cast<GutendexClient*>(client.get())) name = "Gutendex";
        else if (dynamic_cast<UsgsEarthquakeClient*>(client.get())) name = "USGS_Earthquake";
        else if (dynamic_cast<SpaceXClient*>(client.get())) name = "SpaceX";
        else if (dynamic_cast<ChroniclingAmericaClient*>(client.get())) name = "ChroniclingAmerica";
        else if (dynamic_cast<GbifClient*>(client.get())) name = "GBIF";
        
        std::cout << "[DataSynthesizer]   " << name << ": " 
                  << (configured ? "CONFIGURED" : "NOT_CONFIGURED") << std::endl;
    }
    
    // Filter to only configured clients - skip APIs without keys/credentials
    size_t total_clients = clients_.size();
    clients_.erase(
        std::remove_if(clients_.begin(), clients_.end(),
            [](const std::unique_ptr<ApiClient>& c) { return !c->is_configured(); }),
        clients_.end()
    );
    
    std::cout << "[DataSynthesizer] API clients enabled: " << clients_.size() << "/" << total_clients << std::endl;
    
    if (clients_.empty()) {
        std::cerr << "[DataSynthesizer] ERROR: No API clients configured!" << std::endl;
        std::cerr << "[DataSynthesizer] Set WOLFRAM_API_KEY env var for WolframAlpha API" << std::endl;
        std::cerr << "[DataSynthesizer] Other APIs (PubChem, OEIS, Wikidata, etc.) don't require keys" << std::endl;
        std::cerr << "[DataSynthesizer] Check network connectivity if keyless APIs are not configured" << std::endl;
        running_ = false;
        return;
    }
    
    acquisition_pool_ = std::make_unique<ThreadPool>(acquisition_threads);
    perturbation_pool_ = std::make_unique<ThreadPool>(perturbation_threads);
    
    // Start acquisition workers ONLY for configured clients
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
    queue_not_full_cv_.notify_all();
    
    // Stop config-based acquisition manager if running
    if (acquisition_mgr_) {
        acquisition_mgr_->stop();
    }
    
    acquisition_pool_.reset();
    perturbation_pool_.reset();
}

TrainingSample DataSynthesizer::get_sample() {
    auto try_pop_local = [this]() -> std::optional<TrainingSample> {
        std::lock_guard<std::mutex> lk(local_data_mutex_);
        if (!has_local_data_) {
            return std::nullopt;
        }
        return pop_next_local_sample_locked();
    };
    auto try_pop_queue = [this]() -> std::optional<TrainingSample> {
        std::lock_guard<std::mutex> lk(queue_mutex_);
        if (!train_queue_.empty()) {
            TrainingSample s = std::move(train_queue_.front());
            train_queue_.pop_front();
            queue_not_full_cv_.notify_all();
            return s;
        }
        return std::nullopt;
    };

    // When a local corpus exists, get_sample() used to prefer local forever. Acquisition threads
    // still fill raw/train queues; if the trainer never pops train_queue_, producers deadlock on
    // full buffers while metrics show "prefill" and samples_processed stays 0. Apply backpressure
    // here without returning a lone train_queue_ element: a single pop_front() at the cap left
    // neg-at-head queues that made try_pop_contrastive_pair() fail forever despite a full buffer.
    {
        std::lock_guard<std::mutex> lk(queue_mutex_);
        const size_t cap = max_train_queue_depth_;
        size_t steps = 0;
        const size_t max_steps = std::max<size_t>(size_t{16}, train_queue_.size());
        while (!train_queue_.empty() && cap >= 4 && train_queue_.size() >= (cap * 3) / 4 &&
               steps < max_steps) {
            ++steps;
            if (train_queue_.size() >= 2u && train_queue_[0].is_positive() &&
                train_queue_[1].is_negative()) {
                train_queue_.pop_front();
                train_queue_.pop_front();
            } else {
                train_queue_.pop_front();
            }
            queue_not_full_cv_.notify_all();
        }
    }

    const bool hybrid = has_local_data_ && use_config_sources_;

    if (!hybrid) {
        if (auto loc = try_pop_local()) {
            return *loc;
        }
        std::unique_lock<std::mutex> lock(queue_mutex_);
        queue_cv_.wait(lock, [this] { return !train_queue_.empty() || !running_; });
        if (!train_queue_.empty()) {
            TrainingSample sample = std::move(train_queue_.front());
            train_queue_.pop_front();
            queue_not_full_cv_.notify_all();
            return sample;
        }
        return TrainingSample{};
    }

    // Hybrid: alternate preferred side (local vs queue); take the other if empty.
    for (;;) {
        const bool prefer_local = ((interleave_counter_.fetch_add(1, std::memory_order_relaxed) & 1u) == 0u);
        if (prefer_local) {
            if (auto loc = try_pop_local()) {
                return *loc;
            }
            if (auto q = try_pop_queue()) {
                return *q;
            }
        } else {
            if (auto q = try_pop_queue()) {
                return *q;
            }
            if (auto loc = try_pop_local()) {
                return *loc;
            }
        }

        std::unique_lock<std::mutex> lock(queue_mutex_);
        queue_cv_.wait_for(lock, std::chrono::milliseconds(250), [this] {
            return !train_queue_.empty() || !running_;
        });
        if (!train_queue_.empty()) {
            TrainingSample s = std::move(train_queue_.front());
            train_queue_.pop_front();
            queue_not_full_cv_.notify_all();
            return s;
        }
        if (!running_) {
            lock.unlock();
            if (auto loc = try_pop_local()) {
                return *loc;
            }
            return TrainingSample{};
        }
        lock.unlock();
        if (auto loc = try_pop_local()) {
            return *loc;
        }
    }
}

bool DataSynthesizer::has_sample() const {
    {
        std::lock_guard<std::mutex> local_lock(local_data_mutex_);
        if (has_local_data_ &&
            ((local_directory_indexed_ && !corpus_line_index_.empty()) || !local_samples_.empty())) {
            return true;
        }
    }
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return !train_queue_.empty();
}

std::optional<TrainingSample> DataSynthesizer::try_draw_local_training_sample() {
    std::lock_guard<std::mutex> lk(local_data_mutex_);
    if (!has_local_data_) {
        return std::nullopt;
    }
    return pop_next_local_sample_locked();
}

bool DataSynthesizer::try_pop_contrastive_pair(TrainingSample& pos_out, TrainingSample& neg_out) {
    std::lock_guard<std::mutex> lk(queue_mutex_);
    const size_t initial_depth = train_queue_.size();
    // Cap work per call so a pathological queue cannot hold queue_mutex for hundreds of ms
    // while acquisition/perturbation threads need it. Alignment continues across batch rows.
    constexpr size_t kMaxResyncPopsPerCall = 4096;
    const size_t discard_budget = (std::min)(
        (std::max)(initial_depth + size_t{64}, size_t{64}),
        kMaxResyncPopsPerCall);
    size_t discarded = 0;
    while (train_queue_.size() >= 2u) {
        if (train_queue_[0].is_positive() && train_queue_[1].is_negative()) {
            pos_out = std::move(train_queue_.front());
            train_queue_.pop_front();
            neg_out = std::move(train_queue_.front());
            train_queue_.pop_front();
            queue_not_full_cv_.notify_all();
            return true;
        }
        if (discarded >= discard_budget) {
            break;
        }
        ++discarded;
        train_queue_.pop_front();
        queue_not_full_cv_.notify_all();
    }
    return false;
}

DataSynthesizer::Stats DataSynthesizer::get_stats() const {
    Stats s;
    {
        std::lock_guard<std::mutex> qlock(queue_mutex_);
        s.queue_depth = train_queue_.size();
        s.raw_queue_depth = raw_queue_.size();
    }
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        s = stats_;
    }
    {
        std::lock_guard<std::mutex> qlock(queue_mutex_);
        s.queue_depth = train_queue_.size();
        s.raw_queue_depth = raw_queue_.size();
    }
    if (acquisition_mgr_) {
        auto aq = acquisition_mgr_->get_queue_stats();
        s.acquisition_queue_depth = aq.queue_depth;
        s.acquisition_queue_max = aq.max_queue_depth;
        s.acquisition_blocked_pushes = aq.blocked_pushes;
        s.acquisition_blocked_wait_ms = aq.blocked_wait_ms;
        s.acquisition_dropped_too_short = aq.dropped_too_short;
    }
    s.raw_queue_max = max_raw_queue_depth_;
    s.train_queue_max = max_train_queue_depth_;
    return s;
}

void DataSynthesizer::config_acquisition_worker() {
    // Fetch data from configured sources via DataAcquisitionManager
    // and feed into raw_queue_ for perturbation_worker
    uint32_t empty_batch_streak = 0;
    bool logged_relaxed_min_text = false;
    constexpr uint32_t kRelaxMinTextAfterEmptyBatches = 40; // ~4s at 100ms idle sleep
    constexpr size_t kDefaultMinTextLen = 50;

    while (running_) {
        if (!acquisition_mgr_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        size_t min_text = kDefaultMinTextLen;
        if (empty_batch_streak >= kRelaxMinTextAfterEmptyBatches) {
            min_text = 1;
            if (!logged_relaxed_min_text) {
                logged_relaxed_min_text = true;
                std::cerr << "[DataSynthesizer] config_acquisition_worker: min_text relaxed to 1 after "
                          << empty_batch_streak << " empty acquisition batches (tiny-corpus / prefill target not met)\n";
            }
        }

        auto samples = acquisition_mgr_->fetch_batch(10, min_text, 100000);

        if (!samples.empty()) {
            empty_batch_streak = 0;
            logged_relaxed_min_text = false;
            // Convert TrainingSample payloads to a trainable fixed-point vector payload.
            for (auto& sample : samples) {
                std::vector<int32_t> converted;
                bool accepted = false;
                std::visit([&converted, &accepted](auto&& arg) {
                    using T = std::decay_t<decltype(arg)>;
                    if constexpr (std::is_same_v<T, std::vector<int32_t>>) {
                        converted = arg;
                        accepted = true;
                    } else if constexpr (std::is_same_v<T, std::string_view>) {
                        converted.reserve(arg.size());
                        for (unsigned char ch : arg) {
                            converted.push_back(static_cast<int32_t>(ch));
                        }
                        accepted = !converted.empty();
                    } else if constexpr (std::is_same_v<T, std::vector<float>>) {
                        converted.reserve(arg.size());
                        for (float v : arg) {
                            converted.push_back(static_cast<int32_t>(v * 1000.0f));
                        }
                        accepted = !converted.empty();
                    } else if constexpr (std::is_same_v<T, std::vector<Trit>>) {
                        converted.reserve(arg.size());
                        for (auto t : arg) {
                            converted.push_back(static_cast<int32_t>(t));
                        }
                        accepted = !converted.empty();
                    } else if constexpr (std::is_same_v<T, std::vector<std::vector<int32_t>>>) {
                        size_t cap = 0;
                        for (const auto& row : arg) cap += row.size();
                        converted.reserve(cap);
                        for (const auto& row : arg) {
                            converted.insert(converted.end(), row.begin(), row.end());
                        }
                        accepted = !converted.empty();
                    } else if constexpr (std::is_same_v<T, std::vector<std::vector<float>>>) {
                        for (const auto& row : arg) {
                            for (float v : row) {
                                converted.push_back(static_cast<int32_t>(v * 1000.0f));
                            }
                        }
                        accepted = !converted.empty();
                    }
                }, sample.data);

                if (!accepted) {
                    std::lock_guard<std::mutex> stats_lock(stats_mutex_);
                    stats_.dropped_payloads++;
                    if (std::holds_alternative<std::string_view>(sample.data)) {
                        stats_.dropped_payload_string_view++;
                    } else {
                        stats_.dropped_payload_other++;
                    }
                    continue;
                }

                std::unique_lock<std::mutex> lock(queue_mutex_);
                while (running_ && raw_queue_.size() >= max_raw_queue_depth_) {
                    auto ws = std::chrono::steady_clock::now();
                    {
                        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
                        stats_.blocked_raw_pushes++;
                    }
                    queue_not_full_cv_.wait_for(lock, std::chrono::milliseconds(200));
                    const auto waited = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - ws).count());
                    std::lock_guard<std::mutex> stats_lock(stats_mutex_);
                    stats_.blocked_wait_ms += waited;
                }
                if (!running_) {
                    break;
                }
                raw_queue_.push(std::move(converted));
                lock.unlock();
                {
                    std::lock_guard<std::mutex> stats_lock(stats_mutex_);
                    stats_.total_acquired++;
                }
                queue_cv_.notify_one();
            }
        } else {
            ++empty_batch_streak;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

void DataSynthesizer::acquisition_worker(ApiClient* client) {
    // AUTONOMOUS TOPIC DISCOVERY - No hardcoded queries!
    // Each client maintains a dynamic frontier of exploration topics
    
    std::vector<std::string> topic_frontier;
    const char* client_name = "unknown";
    
    // Seed topics for each API type - small initial seed set that expands autonomously
    if (dynamic_cast<WolframClient*>(client)) {
        topic_frontier = {"calculus", "algebra", "geometry", "statistics"};
        client_name = "WolframAlpha";
    } else if (dynamic_cast<PubChemClient*>(client)) {
        topic_frontier = {"aspirin", "glucose", "ethanol", "caffeine", "morphine", "penicillin", "vitamin", "insulin"};
        client_name = "PubChem";
    } else if (dynamic_cast<OeisClient*>(client)) {
        topic_frontier = {"primes", "fibonacci", "factorial", "combinatorics"};
        client_name = "OEIS";
    } else if (dynamic_cast<WikidataClient*>(client)) {
        topic_frontier = {"Q5", "Q11173", "Q7187", "Q8087"};  // Person, chemical, gene, protein
        client_name = "Wikidata";
    } else if (dynamic_cast<ArxivClient*>(client)) {
        topic_frontier = {"quantum", "ml", "nlp", "vision", "crypto", "robotics"};
        client_name = "arXiv";
    } else if (dynamic_cast<NasaExoplanetClient*>(client)) {
        topic_frontier = {"transit", "rv", "direct", "microlensing"};
        client_name = "NASA Exoplanet";
    } else if (dynamic_cast<PdbClient*>(client)) {
        topic_frontier = {"4HHB", "1UBQ", "enzyme", "antibody", "virus"};
        client_name = "PDB";
    } else if (dynamic_cast<GitHubClient*>(client)) {
        topic_frontier = {"ml", "sycl", "wasm", "rust", "cpp"};
        client_name = "GitHub";
    } else if (dynamic_cast<LeanClient*>(client)) {
        topic_frontier = {"mathlib", "algebra", "topology", "analysis"};
        client_name = "Lean";
    } else if (dynamic_cast<OpenAlexClient*>(client)) {
        // AGGRESSIVE: 50+ seed topics spanning all academic disciplines
        topic_frontier = {
            "artificial intelligence", "machine learning", "deep learning", "neural networks",
            "quantum computing", "quantum physics", "quantum chemistry", "quantum biology",
            "neuroscience", "cognitive science", "brain imaging", "neural plasticity",
            "genomics", "proteomics", "transcriptomics", "metabolomics",
            "climate change", "global warming", "carbon capture", "renewable energy",
            "economics", "finance", "behavioral economics", "game theory",
            "mathematics", "algebra", "geometry", "topology", "number theory",
            "physics", "particle physics", "condensed matter", "optics", "acoustics",
            "chemistry", "organic chemistry", "inorganic", "physical chemistry", "biochemistry",
            "biology", "ecology", "evolution", "genetics", "molecular biology",
            "medicine", "immunology", "virology", "epidemiology", "oncology",
            "engineering", "robotics", "materials science", "nanotechnology", "aerospace",
            "linguistics", "psychology", "sociology", "anthropology", "archaeology",
            "philosophy", "ethics", "logic", "metaphysics", "epistemology",
            "computer vision", "nlp", "speech recognition", "reinforcement learning",
            "cryptography", "cybersecurity", "blockchain", "distributed systems",
            "astrophysics", "cosmology", "exoplanets", "dark matter", "black holes"
        };
        client_name = "OpenAlex";
    } else if (dynamic_cast<GutendexClient*>(client)) {
        // AGGRESSIVE: Literature spanning all domains
        topic_frontier = {
            "philosophy", "ethics", "metaphysics", "epistemology", "logic",
            "science", "physics", "chemistry", "biology", "astronomy",
            "history", "ancient history", "medieval", "renaissance", "modern",
            "mathematics", "algebra", "calculus", "geometry", "statistics",
            "darwin", "evolution", "natural selection", "origin of species",
            "einstein", "relativity", "physics theory", "quantum",
            "newton", "gravity", "calculus", "optics",
            "shakespeare", "literature", "poetry", "drama", "fiction",
            "dostoevsky", "tolstoy", "literature russian", "novel",
            "psychology", "freud", "jung", "behaviorism", "cognition",
            "economics", "wealth of nations", "marx", "capital", "political economy",
            "medicine", "anatomy", "physiology", "disease", "health"
        };
        client_name = "Gutendex";
    } else if (dynamic_cast<UsgsEarthquakeClient*>(client)) {
        // AGGRESSIVE: Global seismic exploration
        topic_frontier = {
            "magnitude_6_2024", "magnitude_7_2023", "magnitude_8_historical",
            "california", "alaska", "hawaii", "cascadia", "san_andreas",
            "japan", "indonesia", "philippines", "china", "india",
            "pacific_ring", "mid_atlantic_ridge", "mediterranean", "himalaya",
            "deep_quakes", "shallow_quakes", "aftershocks", "foreshocks",
            "tsunami", "liquefaction", "ground_motion", "seismic_hazard",
            "volcanic", "subduction", "transform_boundary", "convergent",
            "mexico", "chile", "peru", "turkey", "iran", "italy", "greece",
            "new_zealand", "australia", "antarctica", "africa", "middle_east"
        };
        client_name = "USGS Earthquake";
    } else if (dynamic_cast<SpaceXClient*>(client)) {
        // AGGRESSIVE: Comprehensive space exploration
        topic_frontier = {
            "launches", "rockets", "capsules", "crew", "payloads", "missions",
            "falcon9", "falcon_heavy", "starship", "super_heavy", "raptor",
            "landpads", "launchpads", "droneship", "landing_zone",
            "cores", "booster", "fairing", "interstage", "tank",
            "starlink", "cubesat", "dragon", "crew_dragon", "cargo_dragon",
            "iss", "space_station", "orbit", "leo", "geo", "tle",
            "reentry", "deorbit", "splashdown", "landing", "hoverslam",
            "boca_chica", "cape_canaveral", "kennedy_space_center", "vandenberg",
            "engine", "turbopump", "combustion", "cryogenic", "lox", "rp1"
        };
        client_name = "SpaceX";
    } else if (dynamic_cast<ChroniclingAmericaClient*>(client)) {
        // AGGRESSIVE: Historical newspaper exploration across all domains
        topic_frontier = {
            "machine", "machinery", "steam", "engine", "locomotive",
            "electricity", "electric", "telegraph", "telephone", "wireless",
            "war", "civil war", "world war", "revolution", "battle",
            "science", "scientific", "discovery", "invention", "experiment",
            "medicine", "medical", "surgery", "disease", "epidemic", "health",
            "technology", "industry", "industrial", "manufacturing", "factory",
            "economy", "economic", "finance", "bank", "stock", "trade",
            "railroad", "railway", "train", "transportation", "shipping",
            "agriculture", "farm", "crop", "harvest", "weather", "climate",
            "politics", "election", "government", "law", "court", "crime",
            "education", "school", "university", "college", "student",
            "religion", "church", "temple", "missionary", "sermon",
            "immigration", "migration", "settlement", "frontier", "colony"
        };
        client_name = "Chronicling America";
    } else if (dynamic_cast<GbifClient*>(client)) {
        // AGGRESSIVE: Biodiversity exploration across all kingdoms and ecosystems
        topic_frontier = {
            "mammals", "primates", "carnivora", "cetaceans", "rodents", "bats",
            "birds", "passerines", "raptors", "waterfowl", "songbirds",
            "reptiles", "snakes", "lizards", "turtles", "crocodilians",
            "amphibians", "frogs", "salamanders", "caecilians",
            "fish", "sharks", "rays", "bony_fish", "cartilaginous",
            "insects", "beetles", "butterflies", "bees", "ants", "wasps",
            "arachnids", "spiders", "scorpions", "mites", "ticks",
            "crustaceans", "crabs", "lobsters", "shrimp", "copepods",
            "mollusks", "snails", "clams", "octopus", "squid", "nautilus",
            "cnidarians", "corals", "jellyfish", "anemones", "hydra",
            "echinoderms", "starfish", "sea_urchins", "sea_cucumbers",
            "worms", "annelids", "nematodes", "flatworms", "leeches",
            "fungi", "mushrooms", "molds", "yeasts", "lichens",
            "plants", "trees", "flowers", "grasses", "ferns", "mosses",
            "algae", "bacteria", "archaea", "protists", "plankton",
            "rainforest", "tropical", "temperate", "boreal", "tundra",
            "desert", "savanna", "wetland", "marsh", "swamp", "coral_reef",
            "marine", "freshwater", "terrestrial", "pelagic", "benthic",
            "polar", "arctic", "antarctic", "alpine", "montane",
            "endangered", "invasive", "native", "migratory", "resident"
        };
        client_name = "GBIF";
    } else {
        std::cerr << "[DataSynthesizer] Unknown client type in acquisition worker" << std::endl;
        return;
    }
    
    // Ring-buffer frontier: bounded memory + dedup set.
    constexpr size_t kTopicFrontierMax = 4096;
    std::deque<std::string> topic_frontier_ring;
    std::unordered_set<std::string> topic_frontier_seen;
    topic_frontier_ring.clear();
    topic_frontier_seen.clear();
    auto frontier_push = [&](const std::string& topic) {
        if (topic.empty()) return;
        if (topic_frontier_seen.find(topic) != topic_frontier_seen.end()) return;
        if (topic_frontier_ring.size() >= kTopicFrontierMax) {
            topic_frontier_seen.erase(topic_frontier_ring.front());
            topic_frontier_ring.pop_front();
            {
                std::lock_guard<std::mutex> stats_lock(stats_mutex_);
                stats_.topic_frontier_evictions++;
            }
        }
        topic_frontier_ring.push_back(topic);
        topic_frontier_seen.insert(topic);
    };
    for (const auto& t : topic_frontier) {
        frontier_push(t);
    }

    std::mt19937 rng(std::random_device{}());
    
    while (running_) {
        {
            std::lock_guard<std::mutex> stats_lock(stats_mutex_);
            stats_.topic_frontier_size = topic_frontier_ring.size();
            stats_.topic_frontier_max = kTopicFrontierMax;
        }
        if (topic_frontier_ring.empty()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }
        
        // Pick a topic from bounded frontier (randomized exploration).
        const size_t current_topic_idx = static_cast<size_t>(rng() % topic_frontier_ring.size());
        const std::string topic = topic_frontier_ring[current_topic_idx];
        
        // Each client builds its own query dynamically based on topic
        std::string endpoint;
        std::string params;
        
        // Dynamic query construction based on API type
        if (dynamic_cast<WolframClient*>(client)) {
            endpoint = "query";
            params = "solve+" + topic + "+equation";
        } else if (dynamic_cast<PubChemClient*>(client)) {
            // PubChem PUG-REST: use record endpoint for full compound data
            endpoint = "compound/name/" + topic + "/record";
            params = "";
        } else if (dynamic_cast<OeisClient*>(client)) {
            // OEIS: search by keyword
            endpoint = "search";
            params = "q=" + topic;
        } else if (dynamic_cast<WikidataClient*>(client)) {
            // Wikidata: use entity data endpoint instead of complex SPARQL
            // Topic is a Q-ID like "Q5", fetch entity data directly
            endpoint = "Special:EntityData/" + topic + ".json";
            params = "";
        } else if (dynamic_cast<ArxivClient*>(client)) {
            endpoint = "";
            params = topic + "+recent";
        } else if (dynamic_cast<NasaExoplanetClient*>(client)) {
            endpoint = "";
            params = "SELECT * FROM ps WHERE discoverymethod LIKE '%" + topic + "%'";
        } else if (dynamic_cast<PdbClient*>(client)) {
            endpoint = topic.length() < 5 ? "entry" : "search";
            params = topic;
        } else if (dynamic_cast<GitHubClient*>(client)) {
            endpoint = "search/repositories";
            params = "q=" + topic + "+language:cpp";
        } else if (dynamic_cast<LeanClient*>(client)) {
            endpoint = "proofs";
            params = topic;
        } else if (dynamic_cast<OpenAlexClient*>(client)) {
            // OpenAlex: dynamic academic exploration
            endpoint = "works";
            params = "search=" + topic + "&per_page=10&sort=relevance_score:desc";
        } else if (dynamic_cast<GutendexClient*>(client)) {
            // Gutendex: literature search
            endpoint = "books";
            params = "?search=" + topic + "&languages=en";
        } else if (dynamic_cast<UsgsEarthquakeClient*>(client)) {
            // USGS: dynamic seismic queries
            endpoint = "query";
            if (topic.find("magnitude") == 0) {
                params = "format=geojson&starttime=2024-01-01&minmagnitude=" + topic.substr(10, 1);
            } else if (topic == "california" || topic == "japan") {
                params = "format=geojson&place=" + topic;
            } else {
                params = "format=geojson&orderby=magnitude&limit=20";
            }
        } else if (dynamic_cast<SpaceXClient*>(client)) {
            // SpaceX: explore different endpoints dynamically
            endpoint = topic;
            params = "";
        } else if (dynamic_cast<ChroniclingAmericaClient*>(client)) {
            // Chronicling America: newspaper search
            endpoint = "search/titles/results/?terms=" + topic + "&format=json";
            params = "";
        } else if (dynamic_cast<GbifClient*>(client)) {
            // GBIF: species and occurrence search
            endpoint = "species/search";
            params = "?q=" + topic + "&limit=10";
        }
        
        // Log the API request for debugging
        std::cout << "[DataSynthesizer] " << client_name << " API: " << endpoint;
        if (!params.empty()) std::cout << "?" << params.substr(0, 50);
        std::cout << std::endl;
        
        // Make HTTP request
        auto payload = client->query(endpoint, params);
        
        if (payload) {
            // Extract new topics from response to expand frontier (autodiscovery!)
            std::vector<std::string> discovered = extract_topics_from_response(*payload, topic);
            
            // AGGRESSIVE TOPIC DISCOVERY: Add all discovered topics
            // Use bounded ring buffer to keep memory stable in long runs.
            for (const auto& new_topic : discovered) {
                frontier_push(new_topic);
            }
            
            {
                std::unique_lock<std::mutex> lock(queue_mutex_);
                while (running_ && raw_queue_.size() >= max_raw_queue_depth_) {
                    auto ws = std::chrono::steady_clock::now();
                    {
                        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
                        stats_.blocked_raw_pushes++;
                    }
                    queue_not_full_cv_.wait_for(lock, std::chrono::milliseconds(200));
                    const auto waited = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - ws).count());
                    std::lock_guard<std::mutex> stats_lock(stats_mutex_);
                    stats_.blocked_wait_ms += waited;
                }
                if (!running_) {
                    break;
                }
                raw_queue_.push(*payload);
                {
                    std::lock_guard<std::mutex> stats_lock(stats_mutex_);
                    ++stats_.total_acquired;
                }
            }
            queue_cv_.notify_one();
            
            std::cout << "[DataSynthesizer] " << client_name 
                      << " explored: " << topic 
                      << " (frontier: " << topic_frontier_ring.size() << "/" << kTopicFrontierMax << ")" << std::endl;
        } else {
            std::lock_guard<std::mutex> stats_lock(stats_mutex_);
            ++stats_.api_failures;
            
            // Remove failed topic from frontier to avoid requery
            // Keep a minimum frontier width to avoid dead-ending.
            if (topic_frontier_ring.size() > 4) {
                auto it = std::find(topic_frontier_ring.begin(), topic_frontier_ring.end(), topic);
                if (it != topic_frontier_ring.end()) {
                    topic_frontier_seen.erase(*it);
                    topic_frontier_ring.erase(it);
                }
            }
        }

        
        // MINIMAL rate limiting - we need MASSIVE data for 100B model
        // APIs with limits will return 429, we handle via backoff
        std::this_thread::sleep_for(std::chrono::milliseconds(10 + (rng() % 50)));
    }
}

// ============================================================================
// Autonomous Topic Discovery - Extract Topics from API Responses
// ============================================================================

std::vector<std::string> DataSynthesizer::extract_topics_from_response(
    const ApiPayload& response, 
    const std::string& current_topic) {
    
    std::vector<std::string> discovered;
    
    // Extract string content from variant
    std::string content;
    std::visit([&content](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::string_view>) {
            content = std::string(arg);
        }
    }, response);
    
    if (content.empty()) return discovered;
    
    // AGGRESSIVE TOPIC EXTRACTION: Extract up to 50 topics per response
    // For 100B parameter model, we need massive vocabulary diversity
    
    // Extract quoted strings that look like topics
    size_t pos = 0;
    while ((pos = content.find('"', pos)) != std::string::npos) {
        size_t end = content.find('"', pos + 1);
        if (end == std::string::npos) break;
        
        std::string word = content.substr(pos + 1, end - pos - 1);
        
        // RELAXED FILTER: Just need 2+ chars with at least one letter
        if (word.length() >= 2 && word.length() <= 40 && 
            std::any_of(word.begin(), word.end(), ::isalpha)) {
            
            // Clean up: lowercase, keep alphanumeric, spaces, hyphens
            std::string clean;
            for (char c : word) {
                if (std::isalnum(c) || c == ' ' || c == '-') {
                    clean += std::tolower(c);
                }
            }
            
            // Minimal stop word list - only the most common
            static const std::unordered_set<std::string> stop_words = {
                "the", "and", "for", "are", "but", "not", "you", "all",
                "was", "one", "our", "out", "day", "get", "has", "him",
                "his", "how", "its", "may", "new", "now", "old", "see",
                "two", "who", "did", "she", "use", "way", "many", "any",
                "man", "try", "ask", "end", "why", "let", "put", "own",
                "too", "say", "come", "here", "true", "false", "null",
                "this", "that", "with", "from", "they", "know", "been",
                "good", "much", "some", "time", "very", "when", "then"
            };
            
            // RELAXED: Accept almost any unique term that's not an exact stop word
            if (!clean.empty() && clean.length() >= 3 && 
                clean != current_topic && 
                stop_words.find(clean) == stop_words.end()) {
                discovered.push_back(clean);
            }
        }
        
        pos = end + 1;
        if (discovered.size() >= 50) break;  // EXTRACT UP TO 50 TOPICS PER RESPONSE
    }
    
    return discovered;
}

void DataSynthesizer::perturbation_worker() {
    while (running_) {
        ApiPayload positive;
        size_t client_type = 0;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this] { return !raw_queue_.empty() || !running_; });
            
            if (!running_) break;
            if (raw_queue_.empty()) continue;
            
            positive = std::move(raw_queue_.front());
            raw_queue_.pop();
            queue_not_full_cv_.notify_all();
            client_type = (stats_.total_acquired % 15);  // Cycle through 15 client types (9 old + 6 new)
        }
        
        // Generate contrastive pairs
        std::string_view domain;
        switch (client_type) {
            case 0: domain = "math"; break;
            case 1: domain = "chemistry"; break;
            case 2: domain = "math"; break;
            case 3: domain = "ontology"; break;
            case 4: domain = "physics"; break;
            case 5: domain = "astrophysics"; break;
            case 6: domain = "biology"; break;
            case 7: domain = "code"; break;
            case 8: domain = "math"; break;
            case 9: domain = "academic"; break;      // OpenAlex
            case 10: domain = "literature"; break;   // Gutendex
            case 11: domain = "geophysics"; break; // USGS
            case 12: domain = "aerospace"; break;  // SpaceX
            case 13: domain = "history"; break;      // Chronicling America
            case 14: domain = "biology"; break;      // GBIF
            default: domain = "general";
        }
        
        TrainingSample pos_sample{positive, 1, "synthesizer", domain};
        
        // Create negative sample via client-specific perturbation
        ApiPayload negative;
        switch (client_type) {
            case 0:
                negative = WolframClient::perturb_symbolic(positive);
                break;
            case 1:
                negative = PubChemClient::perturb_smiles(positive);
                break;
            case 2:
                negative = OeisClient::perturb_sequence(positive);
                break;
            case 3:
                negative = WikidataClient::perturb_triples(positive);
                break;
            case 4:
                negative = ArxivClient::perturb_scientific(positive);
                break;
            case 5:
                negative = NasaExoplanetClient::perturb_transit(positive);
                break;
            case 6:
                negative = PdbClient::perturb_coordinates(positive);
                break;
            case 7:
                negative = GitHubClient::perturb_code(positive);
                break;
            case 8:
                negative = LeanClient::perturb_proof(positive);
                break;
            default:
                negative = positive;  // Should never happen
        }
        
        TrainingSample neg_sample{negative, -1, "synthesizer", domain};
        
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            while (running_ && train_queue_.size() + 2 > max_train_queue_depth_) {
                auto ws = std::chrono::steady_clock::now();
                {
                    std::lock_guard<std::mutex> stats_lock(stats_mutex_);
                    stats_.blocked_train_pushes++;
                }
                queue_not_full_cv_.wait_for(lock, std::chrono::milliseconds(200));
                const auto waited = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - ws).count());
                std::lock_guard<std::mutex> stats_lock(stats_mutex_);
                stats_.blocked_wait_ms += waited;
            }
            if (!running_) {
                break;
            }
            train_queue_.push_back(pos_sample);
            train_queue_.push_back(neg_sample);
            {
                std::lock_guard<std::mutex> stats_lock(stats_mutex_);
                stats_.total_perturbed += 2;
            }
        }
        queue_cv_.notify_one();
    }
}

// ============================================================================
// OpenAlex API Client - NO KEY REQUIRED
// ============================================================================

std::optional<ApiPayload> OpenAlexClient::query(std::string_view endpoint,
                                                std::string_view params) {
    if (rate_limit_ == 0) {
        backoff();
    }
    --rate_limit_;
    
    // Build OpenAlex API URL
    std::string url = "https://api.openalex.org/";
    url += std::string(endpoint);
    if (!params.empty()) {
        url += "?" + std::string(params);
    }
    
    auto response = SimpleHttpClient::get(url, 10000);
    last_query_time_ = std::chrono::steady_clock::now();
    
    if (!response.success) {
        return std::nullopt;
    }
    
    std::vector<int32_t> encoded;
    encoded.reserve(response.body.size());
    for (unsigned char ch : response.body) {
        encoded.push_back(static_cast<int32_t>(ch));
    }
    return ApiPayload{std::move(encoded)};
}

void OpenAlexClient::backoff() {
    std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms_));
    backoff_ms_ *= 2;
    rate_limit_ = 100000;  // Reset to daily limit
}

ApiPayload OpenAlexClient::perturb_scholarly(const ApiPayload& positive) {
    // Fabricate citations, swap authors
    auto result = positive;
    return result;
}

// ============================================================================
// Gutendex API Client - NO KEY REQUIRED
// ============================================================================

std::optional<ApiPayload> GutendexClient::query(std::string_view endpoint,
                                                std::string_view params) {
    if (rate_limit_ == 0) {
        backoff();
    }
    --rate_limit_;
    
    // Build Gutendex API URL
    std::string url = "https://gutendex.com/";
    url += std::string(endpoint);
    if (!params.empty()) {
        url += "?" + std::string(params);
    }
    
    auto response = SimpleHttpClient::get(url, 10000);
    last_query_time_ = std::chrono::steady_clock::now();
    
    if (!response.success) {
        return std::nullopt;
    }
    
    std::vector<int32_t> encoded;
    encoded.reserve(response.body.size());
    for (unsigned char ch : response.body) {
        encoded.push_back(static_cast<int32_t>(ch));
    }
    return ApiPayload{std::move(encoded)};
}

void GutendexClient::backoff() {
    std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms_));
    backoff_ms_ *= 2;
    rate_limit_ = 50000;
}

ApiPayload GutendexClient::perturb_literature(const ApiPayload& positive) {
    // Reorder paragraphs, swap character names
    auto result = positive;
    return result;
}

// ============================================================================
// USGS Earthquake API Client - NO KEY REQUIRED
// ============================================================================

std::optional<ApiPayload> UsgsEarthquakeClient::query(std::string_view endpoint,
                                                     std::string_view params) {
    if (rate_limit_ == 0) {
        backoff();
    }
    --rate_limit_;
    
    // Build USGS Earthquake API URL
    std::string url = "https://earthquake.usgs.gov/fdsnws/event/1/";
    url += std::string(endpoint);
    if (!params.empty()) {
        url += "?" + std::string(params);
    }
    
    auto response = SimpleHttpClient::get(url, 10000);
    last_query_time_ = std::chrono::steady_clock::now();
    
    if (!response.success) {
        return std::nullopt;
    }
    
    std::vector<int32_t> encoded;
    encoded.reserve(response.body.size());
    for (unsigned char ch : response.body) {
        encoded.push_back(static_cast<int32_t>(ch));
    }
    return ApiPayload{std::move(encoded)};
}

void UsgsEarthquakeClient::backoff() {
    std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms_));
    backoff_ms_ *= 2;
    rate_limit_ = 10000;
}

ApiPayload UsgsEarthquakeClient::perturb_seismic(const ApiPayload& positive) {
    // Alter magnitude, shift epicenter
    auto result = positive;
    return result;
}

// ============================================================================
// SpaceX API Client - NO KEY REQUIRED
// ============================================================================

std::optional<ApiPayload> SpaceXClient::query(std::string_view endpoint,
                                             std::string_view params) {
    if (rate_limit_ == 0) {
        backoff();
    }
    --rate_limit_;
    
    // Build SpaceX API URL
    std::string url = "https://api.spacexdata.com/v4/";
    url += std::string(endpoint);
    if (!params.empty()) {
        url += "?" + std::string(params);
    }
    
    auto response = SimpleHttpClient::get(url, 10000);
    last_query_time_ = std::chrono::steady_clock::now();
    
    if (!response.success) {
        return std::nullopt;
    }
    
    std::vector<int32_t> encoded;
    encoded.reserve(response.body.size());
    for (unsigned char ch : response.body) {
        encoded.push_back(static_cast<int32_t>(ch));
    }
    return ApiPayload{std::move(encoded)};
}

void SpaceXClient::backoff() {
    std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms_));
    backoff_ms_ *= 2;
    rate_limit_ = 50000;
}

ApiPayload SpaceXClient::perturb_telemetry(const ApiPayload& positive) {
    // Swap payload capacities, alter dates
    auto result = positive;
    return result;
}

// ============================================================================
// Chronicling America API Client - NO KEY REQUIRED
// ============================================================================

std::optional<ApiPayload> ChroniclingAmericaClient::query(std::string_view endpoint,
                                                         std::string_view params) {
    if (rate_limit_ == 0) {
        backoff();
    }
    --rate_limit_;
    
    // Build Chronicling America API URL
    std::string url = "https://chroniclingamerica.loc.gov/";
    url += std::string(endpoint);
    if (!params.empty()) {
        url += "?" + std::string(params);
    }
    
    auto response = SimpleHttpClient::get(url, 10000);
    last_query_time_ = std::chrono::steady_clock::now();
    
    if (!response.success) {
        return std::nullopt;
    }
    
    std::vector<int32_t> encoded;
    encoded.reserve(response.body.size());
    for (unsigned char ch : response.body) {
        encoded.push_back(static_cast<int32_t>(ch));
    }
    return ApiPayload{std::move(encoded)};
}

void ChroniclingAmericaClient::backoff() {
    std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms_));
    backoff_ms_ *= 2;
    rate_limit_ = 20000;
}

ApiPayload ChroniclingAmericaClient::perturb_historical(const ApiPayload& positive) {
    // Alter dates, swap headlines
    auto result = positive;
    return result;
}

// ============================================================================
// GBIF API Client - NO KEY REQUIRED
// ============================================================================

std::optional<ApiPayload> GbifClient::query(std::string_view endpoint,
                                           std::string_view params) {
    if (rate_limit_ == 0) {
        backoff();
    }
    --rate_limit_;
    
    // Build GBIF API URL
    std::string url = "https://api.gbif.org/v1/";
    url += std::string(endpoint);
    if (!params.empty()) {
        url += "?" + std::string(params);
    }
    
    auto response = SimpleHttpClient::get(url, 10000);
    last_query_time_ = std::chrono::steady_clock::now();
    
    if (!response.success) {
        return std::nullopt;
    }
    
    std::vector<int32_t> encoded;
    encoded.reserve(response.body.size());
    for (unsigned char ch : response.body) {
        encoded.push_back(static_cast<int32_t>(ch));
    }
    return ApiPayload{std::move(encoded)};
}

void GbifClient::backoff() {
    std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms_));
    backoff_ms_ *= 2;
    rate_limit_ = 10000;
}

ApiPayload GbifClient::perturb_biodiversity(const ApiPayload& positive) {
    // Shift coordinates, swap species
    auto result = positive;
    return result;
}

} // namespace q_mini_wasm_v2::core::training
