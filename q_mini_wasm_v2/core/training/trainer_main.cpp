#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <chrono>
#include <thread>
#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <cstdio>
#include <map>
#include <variant>
#include <memory>
#include <random>
#include "../ternary/trit.hpp"
#include "../network.hpp"
#include "../moe/self_organizing_expert.hpp"
#include "data_synthesizer.hpp"
#include "dataset_storage.hpp"
#include "../ingestion/trit_binary_loader.hpp"

// ============================================================================
// Native HTTP Client - No external dependencies
// ============================================================================

#ifdef _WIN32
    #include <windows.h>
    #include <winhttp.h>
    #pragma comment(lib, "winhttp.lib")
    
    struct HttpResponse {
        int status_code = 0;
        std::string body;
        bool success = false;
        std::string error;
    };
    
    HttpResponse http_get(const std::string& url, int timeout_ms = 30000) {
        HttpResponse response;
        
        // Auto-add https:// prefix if URL lacks scheme
        std::string full_url = url;
        if (url.find("://") == std::string::npos) {
            full_url = "https://" + url;
        }
        
        // Parse URL to get host and path
        std::string protocol, host, path = "/";
        int port = 443; // Default HTTPS
        
        size_t protocol_end = full_url.find("://");
        if (protocol_end == std::string::npos) {
            response.error = "Invalid URL format";
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
        
        // Check for port in host
        size_t port_colon = host.find(':');
        if (port_colon != std::string::npos) {
            port = std::stoi(host.substr(port_colon + 1));
            host = host.substr(0, port_colon);
        }
        
        BOOL use_https = (protocol == "https");
        
        // Initialize WinHTTP
        HINTERNET hSession = WinHttpOpen(L"qminiwasm-trainer/1.0", 
                                         WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                         WINHTTP_NO_PROXY_NAME, 
                                         WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession) {
            response.error = "Failed to initialize WinHTTP session";
            return response;
        }
        
        // Set timeouts
        WinHttpSetTimeouts(hSession, timeout_ms / 5, timeout_ms / 5, 
                           timeout_ms, timeout_ms);
        
        // Convert host to wide string
        std::wstring whost(host.begin(), host.end());
        
        // Connect
        HINTERNET hConnect = WinHttpConnect(hSession, whost.c_str(), 
                                           use_https ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT, 0);
        if (!hConnect) {
            response.error = "Failed to connect to host: " + host;
            WinHttpCloseHandle(hSession);
            return response;
        }
        
        // Convert path to wide string
        std::wstring wpath(path.begin(), path.end());
        if (wpath.empty()) wpath = L"/";
        
        // Create request
        HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", wpath.c_str(),
                                               NULL, WINHTTP_NO_REFERER,
                                               WINHTTP_DEFAULT_ACCEPT_TYPES,
                                               use_https ? WINHTTP_FLAG_SECURE : 0);
        if (!hRequest) {
            response.error = "Failed to create HTTP request";
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return response;
        }
        
        // Send request
        if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                               WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
            response.error = "Failed to send HTTP request";
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return response;
        }
        
        // Receive response
        if (!WinHttpReceiveResponse(hRequest, NULL)) {
            response.error = "Failed to receive HTTP response";
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return response;
        }
        
        // Get status code
        DWORD status_code = 0;
        DWORD status_code_size = sizeof(status_code);
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                           WINHTTP_HEADER_NAME_BY_INDEX, &status_code, &status_code_size, WINHTTP_NO_HEADER_INDEX);
        response.status_code = static_cast<int>(status_code);
        
        // Read response body
        DWORD bytes_available = 0;
        DWORD bytes_read = 0;
        std::vector<char> buffer;
        
        while (WinHttpQueryDataAvailable(hRequest, &bytes_available) && bytes_available > 0) {
            
            buffer.resize(bytes_available);
            if (WinHttpReadData(hRequest, buffer.data(), bytes_available, &bytes_read)) {
                response.body.append(buffer.data(), bytes_read);
            }
        }
        
        response.success = (response.status_code >= 200 && response.status_code < 300);
        if (!response.success && response.error.empty()) {
            response.error = "HTTP " + std::to_string(response.status_code);
        }
        
        // Cleanup
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        
        return response;
    }
#else
    // Linux/Mac fallback using libcurl or popen
    struct HttpResponse {
        int status_code = 0;
        std::string body;
        bool success = false;
        std::string error;
    };
    
    HttpResponse http_get(const std::string& url, int timeout_ms = 30000) {
        HttpResponse response;
        
        #ifdef HAS_LIBCURL
        // Use libcurl if available
        CURL* curl = curl_easy_init();
        if (!curl) {
            response.error = "Failed to initialize CURL";
            return response;
        }
        
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, +[](char* ptr, size_t size, size_t nmemb, std::string* data) -> size_t {
            data->append(ptr, size * nmemb);
            return size * nmemb;
        });
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "qminiwasm-trainer/1.0");
        
        CURLcode res = curl_easy_perform(curl);
        if (res == CURLE_OK) {
            long code = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
            response.status_code = static_cast<int>(code);
            response.success = (response.status_code >= 200 && response.status_code < 300);
        } else {
            response.error = curl_easy_strerror(res);
        }
        
        curl_easy_cleanup(curl);
        #else
        // Fallback to curl command
        std::string cmd = "curl -s -L --max-time " + std::to_string(timeout_ms / 1000) + 
                         " --user-agent \"qminiwasm-trainer/1.0\" \"" + url + "\" 2>/dev/null";
        FILE* pipe = popen(cmd.c_str(), "r");
        if (pipe) {
            char buffer[4096];
            while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
                response.body += buffer;
            }
            int status = pclose(pipe);
            response.success = (status == 0 && !response.body.empty());
            response.status_code = response.success ? 200 : 0;
        } else {
            response.error = "Failed to execute curl command";
        }
        #endif
        
        return response;
    }
#endif

using namespace q_mini_wasm_v2::core;
using namespace q_mini_wasm_v2::core::training;

// Simple JSON text extraction for dataset loading
// Handles: {"text": "content"} or {"text":"content"}
std::string extract_json_text(const std::string& line) {
    // Look for "text" key (with optional whitespace and colon)
    size_t key_pos = line.find("\"text\"");
    if (key_pos == std::string::npos) return "";
    
    // Move past the key and any whitespace/colon
    size_t start = key_pos + 6; // skip "text"
    
    // Skip whitespace
    while (start < line.length() && isspace(line[start])) start++;
    
    // Expect colon
    if (start >= line.length() || line[start] != ':') return "";
    start++; // skip colon
    
    // Skip whitespace after colon
    while (start < line.length() && isspace(line[start])) start++;
    
    // Expect opening quote
    if (start >= line.length() || line[start] != '"') return "";
    start++; // skip opening quote
    
    // Find closing quote (handle escaped quotes by simple skip)
    size_t end = start;
    while (end < line.length()) {
        if (line[end] == '"' && (end == start || line[end-1] != '\\')) {
            break; // Found unescaped closing quote
        }
        end++;
    }
    
    if (end >= line.length()) return "";
    
    return line.substr(start, end - start);
}

// ============================================================================
// TOPIC DETECTION & EXPERT SPECIALIZATION
// ============================================================================

// Domain keywords for topic classification
typedef std::vector<std::pair<std::string, std::vector<std::string>>> DomainKeywords;

DomainKeywords get_domain_keywords() {
    return {
        {"quantum", {"quantum", "qubit", "superposition", "entanglement", "decoherence", "hamiltonian", "wavefunction", "schrodinger", "heisenberg", "measurement", "observable", "eigenstate", "tensor", "hilbert", "operator"}},
        {"mathematics", {"theorem", "proof", "lemma", "conjecture", "axiom", "corollary", "equation", "formula", "integral", "derivative", "matrix", "vector", "tensor", "manifold", "topology", "algebra", "geometry", "analysis"}},
        {"physics", {"newton", "einstein", "relativity", "electromagnetism", "thermodynamics", "statistical", "mechanics", "optics", "particle", "field", "gravity", "force", "energy", "momentum", "velocity", "acceleration"}},
        {"chemistry", {"molecule", "atom", "bond", "reaction", "catalyst", "enzyme", "organic", "inorganic", "polymer", "synthesis", "spectroscopy", "chromatography", "molecular", "compound", "element", "periodic"}},
        {"biology", {"cell", "protein", "dna", "rna", "gene", "genome", "enzyme", "metabolism", "organism", "species", "evolution", "ecosystem", "tissue", "organ", "microorganism", "bacteria", "virus"}},
        {"computer_science", {"algorithm", "complexity", "computation", "recursive", "parallel", "distributed", "compiler", "interpreter", "memory", "processor", "binary", "hexadecimal", "opcode", "register", "cache", "pipeline"}},
        {"ml_ai", {"neural", "network", "gradient", "backpropagation", "optimization", "convergence", "training", "inference", "model", "parameter", "hyperparameter", "regularization", "overfitting", "generalization", "attention", "transformer"}},
        {"formal_methods", {"proof", "theorem", "verification", "validation", "specification", "invariant", "precondition", "postcondition", "logic", "predicate", "modal", "temporal", "liveness", "safety", "correctness"}},
    };
}

// Detect topic domain from text
std::string detect_topic_domain(const std::string& text) {
    std::string lower_text = text;
    std::transform(lower_text.begin(), lower_text.end(), lower_text.begin(), ::tolower);
    
    auto domains = get_domain_keywords();
    std::vector<std::pair<std::string, int>> scores;
    
    for (const auto& [domain, keywords] : domains) {
        int score = 0;
        for (const auto& keyword : keywords) {
            size_t pos = 0;
            while ((pos = lower_text.find(keyword, pos)) != std::string::npos) {
                score++;
                pos += keyword.length();
            }
        }
        scores.push_back({domain, score});
    }
    
    // Return domain with highest score, or "general" if no matches
    std::string best_domain = "general";
    int best_score = 0;
    for (const auto& [domain, score] : scores) {
        if (score > best_score) {
            best_score = score;
            best_domain = domain;
        }
    }
    
    return best_score > 0 ? best_domain : "general";
}

// Map domain to expert index range for specialization
// Divides 512 experts into 8 domain groups of 64 experts each
struct ExpertSpecialization {
    std::string domain;
    size_t start_idx;
    size_t count;
    size_t samples_trained;
    double avg_goodness;
};

std::vector<ExpertSpecialization> initialize_expert_specialization(size_t total_experts) {
    auto domains = get_domain_keywords();
    size_t domains_count = domains.size();
    size_t experts_per_domain = total_experts / domains_count;
    
    std::vector<ExpertSpecialization> spec;
    for (size_t i = 0; i < domains_count; ++i) {
        spec.push_back({
            domains[i].first,
            i * experts_per_domain,
            experts_per_domain,
            0,
            0.0
        });
    }
    return spec;
}

// Get expert indices for a specific domain (top-k within domain)
std::vector<size_t> get_domain_experts(const std::string& domain, 
                                          const std::vector<ExpertSpecialization>& spec,
                                          size_t top_k) {
    for (const auto& s : spec) {
        if (s.domain == domain) {
            std::vector<size_t> experts;
            for (size_t i = 0; i < top_k && i < s.count; ++i) {
                experts.push_back(s.start_idx + i);
            }
            return experts;
        }
    }
    // Fallback: return first top_k experts
    std::vector<size_t> fallback;
    for (size_t i = 0; i < top_k; ++i) {
        fallback.push_back(i);
    }
    return fallback;
}

// Convert text string to ternary trits (64-dimensional)
// Uses position-sensitive hashing to create more variance from uniform data
std::vector<ternary::Trit> text_to_trits(const std::string& text, size_t dim = 64) {
    std::vector<ternary::Trit> result(dim, ternary::Trit::ZERO);

    for (size_t i = 0; i < dim && i < text.size(); ++i) {
        // Position-sensitive hash: combine char value with position
        // This ensures "(a)" at position 0 differs from "(a)" at position 10
        uint32_t hash = static_cast<unsigned char>(text[i]) * 2654435761u + static_cast<uint32_t>(i) * 2246822519u;
        int val = hash % 3;
        if (val == 0) result[i] = ternary::Trit::NEGATIVE;
        else if (val == 1) result[i] = ternary::Trit::ZERO;
        else result[i] = ternary::Trit::POSITIVE;
    }

    return result;
}

// Simple TOML config loader - no external deps
class ConfigLoader {
    std::string raw_content;
    
    size_t find_in_section(const std::string& section, const std::string& key) {
        std::string section_header = "[" + section + "]";
        size_t section_start = raw_content.find(section_header);
        if (section_start == std::string::npos) return std::string::npos;
        
        size_t next_section = raw_content.find("[", section_start + section_header.length());
        size_t search_end = (next_section == std::string::npos) ? raw_content.length() : next_section;
        
        std::string key_search = key + " =";
        size_t key_pos = raw_content.find(key_search, section_start);
        if (key_pos == std::string::npos || key_pos > search_end) return std::string::npos;
        
        return key_pos + key_search.length();
    }
    
public:
    bool load(const std::string& path) {
        std::ifstream file(path);
        if (!file.is_open()) {
            std::cerr << "[Config] Failed to load: " << path << std::endl;
            return false;
        }
        std::stringstream buffer;
        buffer << file.rdbuf();
        raw_content = buffer.str();
        std::cout << "[Config] Loaded: " << path << std::endl;
        return true;
    }
    
    size_t get_size_t(const std::string& section, const std::string& key, bool sse_mode = false) {
        size_t val_pos = find_in_section(section, key);
        if (val_pos == std::string::npos) {
            if (sse_mode) {
                std::cout << "data: {\"status\": \"error\", \"step\": \"Config key not found: [" << section << "] " << key << "\"}\n\n" << std::flush;
            } else {
                std::cerr << "[Config] Key not found: [" << section << "] " << key << std::endl;
            }
            return 0;
        }

        // Skip whitespace and find number
        size_t num_start = raw_content.find_first_of("0123456789", val_pos);
        if (num_start == std::string::npos) {
            if (sse_mode) {
                std::cout << "data: {\"status\": \"error\", \"step\": \"Config no number found: [" << section << "] " << key << "\"}\n\n" << std::flush;
            } else {
                std::cerr << "[Config] No number found for: [" << section << "] " << key << std::endl;
            }
            return 0;
        }

        size_t num_end = raw_content.find_first_not_of("0123456789", num_start);
        std::string num_str = raw_content.substr(num_start, num_end - num_start);
        size_t value = std::stoull(num_str);
        if (sse_mode) {
            std::cout << "data: {\"status\": \"debug\", \"step\": \"Config read: [" << section << "] " << key << " = " << value << "\"}\n\n" << std::flush;
        } else {
            std::cout << "[Config] Read: [" << section << "] " << key << " = " << value << std::endl;
        }
        return value;
    }
    
    std::string get_string(const std::string& section, const std::string& key) {
        size_t val_pos = find_in_section(section, key);
        if (val_pos == std::string::npos) return "";
        
        // Find quoted string
        size_t quote_start = raw_content.find('"', val_pos);
        if (quote_start == std::string::npos) return "";
        
        size_t quote_end = raw_content.find('"', quote_start + 1);
        if (quote_end == std::string::npos) return "";
        
        return raw_content.substr(quote_start + 1, quote_end - quote_start - 1);
    }
    
    bool has_key(const std::string& section, const std::string& key) {
        return find_in_section(section, key) != std::string::npos;
    }
    
    bool get_bool(const std::string& section, const std::string& key, bool default_val = false) {
        size_t val_pos = find_in_section(section, key);
        if (val_pos == std::string::npos) return default_val;
        
        // Find value
        size_t val_start = raw_content.find_first_not_of(" \t", val_pos);
        if (val_start == std::string::npos) return default_val;
        
        std::string val = raw_content.substr(val_start, 5);
        return val.substr(0, 4) == "true" || val.substr(0, 1) == "1";
    }
    
    double get_double(const std::string& section, const std::string& key, double default_val = 0.0, bool sse_mode = false) {
        size_t val_pos = find_in_section(section, key);
        if (val_pos == std::string::npos) return default_val;
        
        // Find value start (skip whitespace)
        size_t val_start = raw_content.find_first_not_of(" \t", val_pos);
        if (val_start == std::string::npos) return default_val;
        
        // Find end of number (digits, decimal point, scientific notation)
        size_t val_end = val_start;
        while (val_end < raw_content.length() && 
               (std::isdigit(raw_content[val_end]) || raw_content[val_end] == '.' || 
                raw_content[val_end] == 'e' || raw_content[val_end] == 'E' || 
                raw_content[val_end] == '-' || raw_content[val_end] == '+')) {
            val_end++;
        }
        
        try {
            double value = std::stod(raw_content.substr(val_start, val_end - val_start));
            if (sse_mode) {
                std::cout << "data: {\"status\": \"debug\", \"step\": \"Config read: [" << section << "] " << key << " = " << value << "}\n\n" << std::flush;
            }
            return value;
        } catch (...) {
            return default_val;
        }
    }
    
    std::vector<std::string> get_string_array(const std::string& section, const std::string& key) {
        std::vector<std::string> result;
        size_t val_pos = find_in_section(section, key);
        if (val_pos == std::string::npos) return result;
        
        // Find opening bracket
        size_t bracket_start = raw_content.find('[', val_pos);
        if (bracket_start == std::string::npos) return result;
        
        // Find closing bracket
        size_t bracket_end = raw_content.find(']', bracket_start);
        if (bracket_end == std::string::npos) return result;
        
        // Extract array content
        std::string array_content = raw_content.substr(bracket_start + 1, bracket_end - bracket_start - 1);
        
        // Parse quoted strings
        size_t pos = 0;
        while (pos < array_content.length()) {
            // Find opening quote
            size_t quote_start = array_content.find('"', pos);
            if (quote_start == std::string::npos) break;
            
            // Find closing quote
            size_t quote_end = array_content.find('"', quote_start + 1);
            if (quote_end == std::string::npos) break;
            
            result.push_back(array_content.substr(quote_start + 1, quote_end - quote_start - 1));
            pos = quote_end + 1;
        }
        
        return result;
    }
};

int main(int argc, char* argv[]) {
    // Load config from file FIRST
    std::string config_path = "config/training_config.toml";
    
    // SSE mode flag for WUI live streaming
    bool sse_mode = false;
    
    // Parse CLI arguments - first pass for config and flags
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc) {
            config_path = argv[++i];
        } else if (arg == "--sse-mode") {
            sse_mode = true;
        }
    }
    
    // Helper lambda for SSE-compatible output
    auto log_progress = [&](const std::string& json_msg) {
        if (sse_mode) {
            std::cout << "data: " << json_msg << "\n\n" << std::flush;
        } else {
            std::cout << json_msg << "\n" << std::flush;
        }
    };
    
    ConfigLoader config;
    bool has_config = config.load(config_path);
    
    // Default parameters - loaded from config if available
    size_t epochs = has_config ? config.get_size_t("training", "epochs", sse_mode) : 100;
    size_t batch_size = has_config ? config.get_size_t("training", "batch_size", sse_mode) : 8192;
    size_t checkpoint_interval = has_config ? config.get_size_t("training", "checkpoint_interval", sse_mode) : 5;
    
    ternary::ProbTrit learning_rate = ternary::ProbTrit::LOW_PROB;
    size_t context_window = has_config ? config.get_size_t("model", "context_window", sse_mode) : 4096;
    size_t entanglement_tokens = has_config ? config.get_size_t("model", "entanglement_tokens", sse_mode) : 256;
    size_t moe_experts = has_config ? config.get_size_t("model", "moe_experts", sse_mode) : 0;  // Changed default from 243 to 0 to force error if config not read
    size_t moe_top_k = has_config ? config.get_size_t("model", "moe_top_k", sse_mode) : 16;
    bool steane_correction = has_config ? config.get_bool("features", "steane_correction", true) : true;
    bool flash_cim = has_config ? config.get_bool("features", "flash_cim", true) : true;

    // Fail fast if config not loaded properly
    if (moe_experts == 0) {
        std::cerr << "[ERROR] moe_experts not found in config file or config not loaded. Please check " << config_path << std::endl;
        return 1;
    }
    
    std::string dataset_path = "";
    std::string base_model_path = has_config ? config.get_string("paths", "base_model") : "checkpoints/final_model.json";
    std::string output_path = "";  // Auto-generated if empty
    std::string output_dir = has_config ? config.get_string("paths", "output_dir") : "checkpoints/";
    
    // Read API enablement from config [apis] section (default to true)
    // Web crawling via APIs is a core feature - enable by default
    bool enable_web_apis = has_config ? config.get_bool("apis", "enabled", true) : true;
    bool continuous_mode = false;  // Run indefinitely, accumulating data and training

    // Parse CLI arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--epochs" && i + 1 < argc) {
            epochs = std::stoull(argv[++i]);
        } else if (arg == "--batch-size" && i + 1 < argc) {
            batch_size = std::stoull(argv[++i]);
        } else if (arg == "--learning-rate" && i + 1 < argc) {
            std::string val = argv[++i];
            int lr_int = std::stoi(val);
            if (lr_int <= 33) learning_rate = ternary::ProbTrit::LOW_PROB;
            else if (lr_int <= 66) learning_rate = ternary::ProbTrit::MED_PROB;
            else learning_rate = ternary::ProbTrit::HIGH_PROB;
        } else if (arg == "--context-window" && i + 1 < argc) {
            context_window = std::stoull(argv[++i]);
        } else if (arg == "--entanglement-tokens" && i + 1 < argc) {
            entanglement_tokens = std::stoull(argv[++i]);
        } else if (arg == "--moe-experts" && i + 1 < argc) {
            moe_experts = std::stoull(argv[++i]);
        } else if (arg == "--moe-top-k" && i + 1 < argc) {
            moe_top_k = std::stoull(argv[++i]);
        } else if (arg == "--steane-correction" && i + 1 < argc) {
            std::string val = argv[++i];
            steane_correction = (val == "true" || val == "1");
        } else if (arg == "--flash-cim" && i + 1 < argc) {
            std::string val = argv[++i];
            flash_cim = (val == "true" || val == "1");
        } else if (arg == "--dataset" && i + 1 < argc) {
            dataset_path = argv[++i];
            // Log what we received for debugging
            log_progress("{\"status\": \"debug\", \"step\": \"Dataset argument received: " + dataset_path + "\"}");
        } else if (arg == "--base-model" && i + 1 < argc) {
            base_model_path = argv[++i];
        } else if (arg == "--output" && i + 1 < argc) {
            output_path = argv[++i];
        } else if (arg == "--checkpoint-every" && i + 1 < argc) {
            checkpoint_interval = std::stoull(argv[++i]);
        } else if (arg == "--sse-mode") {
            sse_mode = true;
        } else if (arg == "--config" && i + 1 < argc) {
            // Already processed above, skip
            ++i;
        } else if (arg == "--enable-web-apis" && i + 1 < argc) {
            std::string val = argv[++i];
            enable_web_apis = (val == "true" || val == "1" || val == "yes");
        } else if (arg == "--disable-web-apis") {
            enable_web_apis = false;
        } else if (arg == "--continuous") {
            continuous_mode = true;
        }
    }

    // Read model dimensions from config
    size_t shadow_dim = has_config ? config.get_size_t("model", "shadow_dim", sse_mode) : 1024;
    size_t num_layers = has_config ? config.get_size_t("model", "num_layers", sse_mode) : 3;
    size_t neurons_per_layer = has_config ? config.get_size_t("model", "neurons_per_layer", sse_mode) : 128;

    log_progress("{\"status\": \"init\", \"message\": \"Config loaded: epochs=" + std::to_string(epochs) + ", batch=" + std::to_string(batch_size) + ", experts=" + std::to_string(moe_experts) + ", layers=" + std::to_string(num_layers) + ", neurons=" + std::to_string(neurons_per_layer) + "\"}");
    
    // Configure the network based on the research paper parameters
    NetworkConfig net_config;
    net_config.input_dim = context_window;
    net_config.shadow_dim = shadow_dim;
    net_config.hash_dim = entanglement_tokens;
    net_config.num_layers = num_layers;
    net_config.neurons_per_layer = neurons_per_layer;
    net_config.total_experts = moe_experts;
    net_config.active_experts = moe_top_k;
    // Read network configuration from TOML (previously hardcoded)
    net_config.routing_qutrits = has_config ? config.get_size_t("network", "routing_qutrits", sse_mode) : 8;
    if (net_config.routing_qutrits == 0) net_config.routing_qutrits = 8;
    
    net_config.learning_rate_shift = has_config ? config.get_size_t("model", "learning_rate_shift", sse_mode) : 2;
    if (net_config.learning_rate_shift == 0) net_config.learning_rate_shift = 2;
    
    net_config.worker_threads = has_config ? config.get_size_t("features", "worker_threads", sse_mode) : 4;
    if (net_config.worker_threads == 0) net_config.worker_threads = 4;
    net_config.enable_steane = steane_correction;
    net_config.enable_flash_cim = flash_cim;

    try {
        // Calculate actual memory: sparse tropical edges for active experts only
        // Tropical geometry: configurable sparsity, ternary weights (2 bits), lazy init
        size_t sparsity_percent = has_config ? config.get_size_t("model.expert", "sparsity", sse_mode) : 5;
        if (sparsity_percent == 0) sparsity_percent = 5;  // Default 5% non-zero edges
        
        // Read text filter config (previously hardcoded 50 and 100000)
        size_t min_text_length = has_config ? config.get_size_t("training.data_filter", "min_text_length", sse_mode) : 50;
        if (min_text_length == 0) min_text_length = 50;
        size_t max_text_length = has_config ? config.get_size_t("training.data_filter", "max_text_length", sse_mode) : 100000;
        if (max_text_length == 0) max_text_length = 100000;
        
        const size_t active_experts = (std::min)(moe_top_k, moe_experts);
        const size_t edges_per_layer = (neurons_per_layer * neurons_per_layer * sparsity_percent) / 100;
        const size_t total_edges = active_experts * num_layers * edges_per_layer;
        const size_t memory_mb = (total_edges * 2) / (1024 * 1024);  // 2 bytes per edge (uint32 target + int8 weight)
        
        log_progress("{\"status\": \"progress\", \"step\": \"Initializing neural network: " + std::to_string(moe_experts) + " experts (" + std::to_string(active_experts) + " active), " + std::to_string(num_layers) + " layers, " + std::to_string(neurons_per_layer) + " neurons each...\"}");
        log_progress("{\"status\": \"progress\", \"step\": \"Active memory: ~" + std::to_string(memory_mb) + "MB (sparse tropical, " + std::to_string(sparsity_percent) + "% density, lazy init)\"}");
        
        TernaryNeuralNetwork tnn(net_config);
        
        log_progress("{\"status\": \"progress\", \"step\": \"Network configured. Architecture: MoE Routing, FF Layers, Steane Polling\"}");

        // Dataset loading or simulation
        std::vector<std::vector<ternary::Trit>> dataset;
        log_progress("{\"status\": \"debug\", \"step\": \"Dataset loading section reached\"}");
        if (!dataset_path.empty()) {
            log_progress("{\"status\": \"progress\", \"step\": \"Loading dataset from: " + dataset_path + "\"}");
            
            // Check if it's a binary trit file (.t3b) - case insensitive
            bool is_t3b = false;
            std::string ext_debug = "N/A";
            if (dataset_path.size() > 4) {
                std::string ext = dataset_path.substr(dataset_path.size() - 4);
                ext_debug = ext;
                // Convert to lowercase for comparison
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                is_t3b = (ext == ".t3b");
            }
            
            log_progress("{\"status\": \"debug\", \"step\": \"Extension [" + ext_debug + "] is_t3b=" + std::to_string(is_t3b) + "\"}");
            
            if (is_t3b) {
                // Load binary trit format (fast!)
                q::ingestion::T3BLoader loader;
                if (loader.open(dataset_path)) {
                    size_t loaded = 0;
                    std::vector<int8_t> trits;
                    
                    // Stream samples
                    loader.stream_samples([&](uint64_t index, const std::vector<int8_t>& trits) -> bool {
                        if (loaded >= batch_size) return false;
                        
                        // Convert int8_t trits to ternary::Trit
                        std::vector<ternary::Trit> sample;
                        sample.reserve(trits.size());
                        for (int8_t t : trits) {
                            if (t == -1) sample.push_back(ternary::Trit::NEGATIVE);
                            else if (t == 0) sample.push_back(ternary::Trit::ZERO);
                            else sample.push_back(ternary::Trit::POSITIVE);
                        }
                        dataset.push_back(std::move(sample));
                        loaded++;
                        
                        if (loaded % 10000 == 0) {
                            log_progress("{\"status\": \"progress\", \"step\": \"Loaded " + std::to_string(loaded) + " samples from T3B...\"}");
                        }
                        return true;
                    });
                    
                    log_progress("{\"status\": \"progress\", \"step\": \"Loaded " + std::to_string(dataset.size()) + " samples from binary T3B format\"}");
                } else {
                    log_progress("{\"status\": \"error\", \"step\": \"Failed to open T3B file: " + dataset_path + "\"}");
                }
            } else {
                // Load JSONL text format
                std::ifstream file(dataset_path);
                if (file.is_open()) {
                    std::string line;
                    size_t loaded = 0;
                    size_t lines_read = 0;
                    while (std::getline(file, line) && loaded < batch_size) {
                        lines_read++;
                        // Extract text from JSON and convert to trits
                        std::string text = extract_json_text(line);
                        if (!text.empty()) {
                            auto sample = text_to_trits(text, context_window);
                            dataset.push_back(sample);
                            loaded++;
                        }
                    }
                    file.close();
                    
                    if (dataset.empty()) {
                        log_progress("{\"status\": \"warning\", \"step\": \"Opened file but loaded 0 samples from " + std::to_string(lines_read) + " lines - check JSON format\"}");
                    } else {
                        log_progress("{\"status\": \"progress\", \"step\": \"Loaded " + std::to_string(dataset.size()) + " samples from " + std::to_string(lines_read) + " lines\"}");
                    }
                } else {
                    log_progress("{\"status\": \"error\", \"step\": \"Could not open dataset file: " + dataset_path + "\"}");
                }
            }
        }
        
        // === DATASYNTHESIZER V2 - API-BASED DATA ACQUISITION ===
        // This replaces the old broken HTML scraping with proper API clients
        std::unique_ptr<DataSynthesizer> synthesizer;
        
        if (enable_web_apis) {
            try {
                log_progress("{\"status\": \"progress\", \"step\": \"=== DATASYNTHESIZER V2 INITIALIZING ===\"}");
                log_progress("{\"status\": \"progress\", \"step\": \"APIs: OpenAlex, Gutendex, USGS, SpaceX, Chronicling America, GBIF, arXiv, Wikidata, OEIS, PubChem, NASA, PDB, GitHub, Lean\"}");
                
                synthesizer = std::make_unique<DataSynthesizer>();
                
                // Start data acquisition threads (initializes all API clients)
                synthesizer->start(4, 2);  // 4 acquisition threads, 2 perturbation threads
                log_progress("{\"status\": \"progress\", \"step\": \"DataSynthesizer started - APIs actively fetching data...\"}");
            } catch (const std::exception& e) {
                log_progress("{\"status\": \"error\", \"step\": \"DataSynthesizer initialization failed: " + std::string(e.what()) + "\"}");
                synthesizer.reset();
            }
        }
        
        // === PERSISTENT DATASET STORAGE ===
        // Load existing samples from disk, supplement with APIs, save back
        DatasetStorage storage(output_dir + "api_dataset");
        
        // First: Load any existing samples from previous runs
        if (storage.has_data()) {
            std::vector<TopologicalSample> stored = storage.load_samples();
            for (const auto& s : stored) {
                if (dataset.size() < batch_size) {
                    dataset.push_back(s.data);
                } else {
                    break;
                }
            }
            log_progress("{\"status\": \"progress\", \"step\": \"Loaded " + std::to_string(dataset.size()) + " samples from persistent storage\"}");
        }
        
        // Second: Fetch samples from DataSynthesizer until batch is full
        size_t web_samples_added = 0;
        std::vector<TopologicalSample> new_samples_to_save;
        
        if (enable_web_apis && synthesizer && dataset.size() < batch_size) {
            size_t needed = batch_size - dataset.size();
            log_progress("{\"status\": \"progress\", \"step\": \"Fetching " + std::to_string(needed) + " more samples from APIs (no time limit)...\"}");
            
            // Wait for first sample to arrive (no time limit - fetch until batch is full)
            while (!synthesizer->has_sample()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            
            // Pull samples from DataSynthesizer queue until batch is full
            while (dataset.size() < batch_size && synthesizer->has_sample()) {
                auto sample = synthesizer->get_sample();
                
                // DEFENSIVE: Skip invalid/empty samples from failed API calls
                if (sample.data.index() == std::variant_npos) {
                    continue;  // Empty variant - API failed
                }
                
                std::vector<ternary::Trit> trits;
                
                // Handle both text and vector<float> data from APIs
                if (std::holds_alternative<std::string_view>(sample.data)) {
                    std::string text = std::string(std::get<std::string_view>(sample.data));
                    // DEFENSIVE: Require minimum text length to avoid garbage (loaded from config)
                    if (text.length() >= min_text_length && text.length() <= max_text_length) {
                        trits = text_to_trits(text, context_window);
                    }
                } else if (std::holds_alternative<std::vector<float>>(sample.data)) {
                    const auto& vec = std::get<std::vector<float>>(sample.data);
                    // DEFENSIVE: Validate vector size to prevent buffer issues
                    if (!vec.empty() && vec.size() <= 10000) {
                        size_t reserve_size = context_window < vec.size() ? context_window : vec.size();
                        trits.reserve(reserve_size);
                        for (float f : vec) {
                            if (f < -0.3f) trits.push_back(ternary::Trit::NEGATIVE);
                            else if (f > 0.3f) trits.push_back(ternary::Trit::POSITIVE);
                            else trits.push_back(ternary::Trit::ZERO);
                        }
                        // Pad or trim to exact context_window size
                        while (trits.size() < context_window) trits.push_back(ternary::Trit::ZERO);
                        if (trits.size() > context_window) trits.resize(context_window);
                    }
                }
                
                // DEFENSIVE: Only add valid samples with expected size
                if (trits.size() == context_window) {
                    dataset.push_back(trits);
                    web_samples_added++;
                    
                    // Store for persistence
                    TopologicalSample stored;
                    stored.data = trits;
                    stored.source_api = std::string(sample.source_api);
                    stored.domain = std::string(sample.domain);
                    stored.timestamp = std::chrono::system_clock::now().time_since_epoch().count();
                    new_samples_to_save.push_back(std::move(stored));
                }
                
                // Progress log every 100 samples
                if (web_samples_added % 100 == 0 && web_samples_added > 0) {
                    log_progress("{\"status\": \"progress\", \"step\": \"Fetched " + std::to_string(web_samples_added) + " API samples...\"}");
                }
            }
            
            log_progress("{\"status\": \"progress\", \"step\": \"Added " + std::to_string(web_samples_added) + " samples from APIs\"}");
            
            // Save new samples to persistent storage
            if (!new_samples_to_save.empty()) {
                storage.save_samples(new_samples_to_save);
                log_progress("{\"status\": \"progress\", \"step\": \"Saved " + std::to_string(new_samples_to_save.size()) + " samples to persistent storage\"}");
            }
            
            // Get DataSynthesizer stats
            auto ds_stats = synthesizer->get_stats();
            log_progress("{\"status\": \"data_source_summary\", \"total_acquired\": " + 
                        std::to_string(ds_stats.total_acquired) + ", \"total_perturbed\": " + 
                        std::to_string(ds_stats.total_perturbed) + ", \"api_failures\": " + 
                        std::to_string(ds_stats.api_failures) + "}");
        }
        
        // If no dataset loaded, training cannot proceed
        if (dataset.empty()) {
            log_progress("{\"status\": \"error\", \"step\": \"No dataset loaded. Training requires real data.\"}");
            std::cerr << "ERROR: No dataset loaded. Please provide --dataset path to a valid JSONL file or configure web data sources." << std::endl;
            return 1;
        }
        
        log_progress("{\"status\": \"progress\", \"step\": \"Initializing Betti-Guided Self-Organizing Expert Topology...\"}");
        
        // Training metadata for rich reporting
        std::string data_source = dataset_path;
        size_t total_samples = dataset.size();
        std::string source_type = dataset_path.empty() ? "web_crawl" : "jsonl_file";
        
        // Log data source breakdown
        log_progress("{\"status\": \"data_source_summary\", \"source_type\": \"" + source_type + "\", "
                    "\"total_samples\": " + std::to_string(total_samples) + ", "
                    "\"web_samples_added\": " + std::to_string(web_samples_added) + "}");
        
        // Initialize Self-Organizing Expert Manager with NO FIXED MAXIMUM
        // Experts will be created/destroyed dynamically based on Betti numbers
        // All values loaded from TOML config (previously hardcoded)
        moe::SelfOrganizingExpertManager::Config som_config;
        som_config.initial_experts = has_config ? config.get_size_t("som", "initial_experts", sse_mode) : 2;
        if (som_config.initial_experts == 0) som_config.initial_experts = 2;
        
        som_config.split_beta1_threshold = has_config ? config.get_size_t("som", "split_beta1_threshold", sse_mode) : 5;
        if (som_config.split_beta1_threshold == 0) som_config.split_beta1_threshold = 5;
        
        som_config.target_graph_density = has_config ? config.get_double("som", "target_graph_density", 0.15, sse_mode) : 0.15;
        if (som_config.target_graph_density <= 0) som_config.target_graph_density = 0.15;
        
        som_config.max_experts_hard_cap = has_config ? config.get_size_t("som", "max_experts_hard_cap", sse_mode) : 100000;
        if (som_config.max_experts_hard_cap == 0) som_config.max_experts_hard_cap = 100000;
        
        som_config.min_graph_density = has_config ? config.get_double("som", "min_graph_density", 0.05, sse_mode) : 0.05;
        som_config.max_graph_density = has_config ? config.get_double("som", "max_graph_density", 0.40, sse_mode) : 0.40;
        som_config.merge_min_activation_rate = has_config ? config.get_double("som", "merge_min_activation_rate", 0.001, sse_mode) : 0.001;
        
        learning::FFConfig ff_config;
        ff_config.num_layers = num_layers;
        ff_config.neurons_per_layer = neurons_per_layer;
        ff_config.learning_rate_shift = net_config.learning_rate_shift;
        ff_config.lazy_init = true;  // Critical: lazy init to prevent OOM
        
        auto som_manager = std::make_unique<moe::SelfOrganizingExpertManager>(som_config, ff_config);
        
        log_progress("{\"status\": \"progress\", \"step\": \"Self-Organizing Manager initialized with " + std::to_string(som_config.initial_experts) + " seed experts\"}");
        log_progress("{\"status\": \"progress\", \"step\": \"Betti β₁ threshold: " + std::to_string(som_config.split_beta1_threshold) + " - experts split when topology complex\"}");
        
        auto start_time = std::chrono::steady_clock::now();
        
        // AGGRESSIVE checkpointing - saves state every N samples for crash recovery
        // This activates immediately and protects training progress
        // Checkpoint interval loaded from TOML config (previously hardcoded 500)
        size_t checkpoint_counter = 0;
        size_t checkpoint_interval_samples = has_config ? config.get_size_t("training.data_filter", "checkpoint_interval_samples", sse_mode) : 500;
        if (checkpoint_interval_samples == 0) checkpoint_interval_samples = 500;
        
        auto save_checkpoint = [&](size_t current_epoch, size_t samples_done, size_t total_experts) -> bool {
            if (output_dir.empty()) {
                log_progress("{\"status\": \"checkpoint_error\", \"error\": \"output_dir is empty\"}");
                return false;
            }
            
            try {
                std::filesystem::create_directories(output_dir);
                std::string state_path = output_dir + "training_state_live.json";
                std::ofstream state_file(state_path);
                if (!state_file.is_open()) {
                    log_progress("{\"status\": \"checkpoint_error\", \"error\": \"Failed to open " + state_path + "\"}");
                    return false;
                }
                
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::steady_clock::now() - start_time).count();
                state_file << "{\n";
                state_file << "  \"checkpoint_version\": 1,\n";
                state_file << "  \"epoch\": " << current_epoch << ",\n";
                state_file << "  \"samples_processed_in_epoch\": " << samples_done << ",\n";
                state_file << "  \"total_samples_per_epoch\": " << total_samples << ",\n";
                state_file << "  \"experts_active\": " << total_experts << ",\n";
                state_file << "  \"elapsed_seconds\": " << elapsed << ",\n";
                state_file << "  \"checkpoint_time\": \"" << std::time(nullptr) << "\"\n";
                state_file << "}\n";
                state_file.close();
                
                log_progress("{\"status\": \"checkpoint_saved\", \"path\": \"" + state_path + "\", \"samples\": " + std::to_string(samples_done) + "}");
                return true;
            } catch (const std::exception& e) {
                log_progress("{\"status\": \"checkpoint_error\", \"error\": \"" + std::string(e.what()) + "\"}");
                return false;
            }
        };
        
        // Check for resume state from previous run
        size_t resume_epoch = 0;
        size_t resume_sample_index = 0;
        if (output_dir.length() > 0) {
            std::string state_path = output_dir + "training_state_live.json";
            std::ifstream state_file(state_path);
            if (state_file.is_open()) {
                // Simple parsing - look for epoch and samples_processed_in_epoch
                std::string line;
                while (std::getline(state_file, line)) {
                    if (line.find("\"epoch\"") != std::string::npos) {
                        size_t colon = line.find(":");
                        if (colon != std::string::npos) {
                            resume_epoch = std::stoul(line.substr(colon + 1));
                        }
                    }
                    if (line.find("\"samples_processed_in_epoch\"") != std::string::npos) {
                        size_t colon = line.find(":");
                        if (colon != std::string::npos) {
                            resume_sample_index = std::stoul(line.substr(colon + 1));
                        }
                    }
                }
                state_file.close();
                if (resume_epoch > 0) {
                    log_progress("{\"status\": \"resume\", \"epoch\": " + std::to_string(resume_epoch) + 
                                ", \"samples_already_processed\": " + std::to_string(resume_sample_index) + "}");
                }
            }
        }
        
        // Self-Organizing Training Loop
        // Each sample potentially triggers topology updates
        
        // CONTINUOUS MODE: Track global iteration for infinite training
        size_t global_iteration = 0;
        size_t total_samples_across_all_runs = 0;
        bool continuous_running = true;
        thread_local std::mt19937 rng(std::random_device{}());  // For sample replacement in continuous mode
        
        while (continuous_running) {
            // In continuous mode, after each full epoch cycle, we fetch more data and continue
            if (continuous_mode && global_iteration > 0) {
                log_progress("{\"status\": \"continuous_cycle\", \"iteration\": " + std::to_string(global_iteration) + 
                            ", \"message\": \"Fetching more data for continuous training...\"}");
                
                // Fetch more samples from APIs in continuous mode
                if (enable_web_apis && synthesizer) {
                    size_t additional_needed = batch_size;  // Fetch another full batch
                    size_t fetched_this_cycle = 0;
                    std::vector<TopologicalSample> new_samples_to_save;
                    
                    log_progress("{\"status\": \"progress\", \"step\": \"Continuous mode: fetching " + 
                                std::to_string(additional_needed) + " more samples...\"}");
                    
                    // Pull samples until we get the additional batch or queue empties
                    auto fetch_start = std::chrono::steady_clock::now();
                    while (fetched_this_cycle < additional_needed && synthesizer->has_sample()) {
                        auto sample = synthesizer->get_sample();
                        
                        // DEFENSIVE: Skip invalid/empty samples from failed API calls
                        if (sample.data.index() == std::variant_npos) {
                            continue;
                        }
                        
                        std::vector<ternary::Trit> trits;
                        
                        // Handle both text and vector<float> data from APIs
                        if (std::holds_alternative<std::string_view>(sample.data)) {
                            std::string text = std::string(std::get<std::string_view>(sample.data));
                            // DEFENSIVE: Require minimum text length (loaded from config)
                            if (text.length() >= min_text_length && text.length() <= max_text_length) {
                                trits = text_to_trits(text, context_window);
                            }
                        } else if (std::holds_alternative<std::vector<float>>(sample.data)) {
                            const auto& vec = std::get<std::vector<float>>(sample.data);
                            // DEFENSIVE: Validate vector size
                            if (!vec.empty() && vec.size() <= 10000) {
                                size_t reserve_size = context_window < vec.size() ? context_window : vec.size();
                                trits.reserve(reserve_size);
                                for (float f : vec) {
                                    if (f < -0.3f) trits.push_back(ternary::Trit::NEGATIVE);
                                    else if (f > 0.3f) trits.push_back(ternary::Trit::POSITIVE);
                                    else trits.push_back(ternary::Trit::ZERO);
                                }
                                while (trits.size() < context_window) trits.push_back(ternary::Trit::ZERO);
                                if (trits.size() > context_window) trits.resize(context_window);
                            }
                        }
                        
                        // DEFENSIVE: Only process valid samples with exact size
                        if (trits.size() == context_window) {
                            // In continuous mode, replace oldest samples to keep dataset size fixed
                            if (dataset.size() >= batch_size * 2) {
                                // Replace a random sample to maintain diversity
                                std::uniform_int_distribution<size_t> dist(0, dataset.size() - 1);
                                dataset[dist(rng)] = trits;
                            } else {
                                dataset.push_back(trits);
                            }
                            fetched_this_cycle++;
                            total_samples_across_all_runs++;
                            
                            // Store for persistence
                            TopologicalSample stored;
                            stored.data = trits;
                            stored.source_api = std::string(sample.source_api);
                            stored.domain = std::string(sample.domain);
                            stored.timestamp = std::chrono::system_clock::now().time_since_epoch().count();
                            new_samples_to_save.push_back(std::move(stored));
                        }
                        
                        // Progress log every 100 samples
                        if (fetched_this_cycle % 100 == 0 && fetched_this_cycle > 0) {
                            auto elapsed_fetch = std::chrono::duration_cast<std::chrono::seconds>(
                                std::chrono::steady_clock::now() - fetch_start).count();
                            log_progress("{\"status\": \"progress\", \"step\": \"Fetched " + 
                                        std::to_string(fetched_this_cycle) + " additional samples in " + 
                                        std::to_string(elapsed_fetch) + "s...\"}");
                        }
                    }
                    
                    // Save new samples to persistent storage
                    if (!new_samples_to_save.empty()) {
                        storage.save_samples(new_samples_to_save);
                        // Clear vector to free memory after saving
                        new_samples_to_save.clear();
                        new_samples_to_save.shrink_to_fit();
                    }
                    
                    auto fetch_elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::steady_clock::now() - fetch_start).count();
                    log_progress("{\"status\": \"continuous_data_fetched\", \"count\": " + 
                                std::to_string(fetched_this_cycle) + ", \"dataset_size\": " + 
                                std::to_string(dataset.size()) + ", \"time_s\": " + 
                                std::to_string(fetch_elapsed) + "}");
                }
                
                // Reset resume state for new cycle
                resume_epoch = 0;
                resume_sample_index = 0;
            }
            
            // Run the requested number of epochs
            for (size_t epoch = 0; epoch < epochs; ++epoch) {
            size_t samples_this_epoch = 0;
            size_t topology_updates = 0;
            
            // Check if we're resuming this epoch from a checkpoint
            size_t skip_samples = 0;
            if (resume_epoch > 0 && epoch + 1 == resume_epoch && resume_sample_index > 0) {
                skip_samples = resume_sample_index;
                log_progress("{\"status\": \"resuming\", \"epoch\": " + std::to_string(epoch + 1) + 
                            ", \"skip_samples\": " + std::to_string(skip_samples) + "}");
            }
            
            // Process each sample through self-organizing manager
            for (const auto& sample : dataset) {
                // Skip already-processed samples when resuming
                if (samples_this_epoch < skip_samples) {
                    samples_this_epoch++;
                    continue;
                }
                
                // Convert to double format for embedding
                std::vector<double> sample_vec;
                sample_vec.reserve(sample.size());
                for (const auto& trit : sample) {
                    sample_vec.push_back(static_cast<double>(trit));
                }
                
                // Detect topic from sample (for logging visibility)
                std::string detected_topic = detect_topic_domain(std::to_string(sample_vec[0]));
                
                // Process through self-organizing manager
                // This routes to nearest experts, trains them, and may trigger splits
                size_t activated_experts = som_manager->process_sample(sample_vec, sample);
                samples_this_epoch++;
                
                // Log every 100th sample to show topic detection and expert activation
                if (samples_this_epoch % 100 == 0) {
                    auto stats = som_manager->get_stats();
                    log_progress("{\"status\": \"sample_processed\", \"epoch\": " + std::to_string(epoch + 1) + 
                                ", \"samples_processed\": " + std::to_string(samples_this_epoch) +
                                ", \"sample_num\": " + std::to_string(samples_this_epoch) + 
                                ", \"topic\": \"" + detected_topic + "\", "
                                "\"experts_active\": " + std::to_string(stats.num_experts) + 
                                ", \"experts_activated\": " + std::to_string(activated_experts) + "}");
                }
                
                // Periodic topology analysis and progress logging (every 10000 samples)
                if (samples_this_epoch % 10000 == 0) {
                    som_manager->update_topology();
                    topology_updates++;
                    
                    // Log in-epoch progress so UI updates during long epochs
                    auto progress_pct = static_cast<int>(samples_this_epoch * 100 / total_samples);
                    log_progress("{\"status\": \"progress\", \"epoch\": " + std::to_string(epoch + 1) + 
                                ", \"samples_processed\": " + std::to_string(samples_this_epoch) + 
                                ", \"total_samples\": " + std::to_string(total_samples) + 
                                ", \"epoch_progress_pct\": " + std::to_string(progress_pct) + 
                                ", \"topology_updates\": " + std::to_string(topology_updates) + "}");
                }
                
                // AGGRESSIVE Live checkpoint every 1000 samples - protects against crashes
                // This is OUTSIDE the 10000-sample block so it fires reliably
                if (samples_this_epoch % checkpoint_interval_samples == 0) {
                    auto stats = som_manager->get_stats();
                    save_checkpoint(epoch + 1, samples_this_epoch, stats.num_experts);
                    checkpoint_counter++;
                    log_progress("{\"status\": \"live_checkpoint\", \"epoch\": " + std::to_string(epoch + 1) + 
                                ", \"samples\": " + std::to_string(samples_this_epoch) + 
                                ", \"checkpoint_number\": " + std::to_string(checkpoint_counter) + "}");
                }
            }
            
            // Final topology update for this epoch
            som_manager->update_topology();
            topology_updates++;
            
            // Get topology statistics
            auto stats = som_manager->get_stats();
            
            // Rich epoch metadata with self-organizing topology info
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - start_time).count();
            
            std::string epoch_json = "{"
                "\"status\": \"epoch\","
                "\"epoch\": " + std::to_string(epoch + 1) + ","
                "\"total_epochs\": " + std::to_string(epochs) + ","
                "\"phase\": \"SELF_ORGANIZING\","
                "\"samples_processed\": " + std::to_string(samples_this_epoch) + ","
                "\"total_samples\": " + std::to_string(total_samples) + ","
                "\"experts_active\": " + std::to_string(stats.num_experts) + ","
                "\"experts_edges\": " + std::to_string(stats.num_edges) + ","
                "\"graph_density\": " + std::to_string(stats.graph_density) + ","
                "\"avg_beta_1\": " + std::to_string(stats.avg_beta_1) + ","
                "\"max_beta_1\": " + std::to_string(stats.max_beta_1) + ","
                "\"topology_updates\": " + std::to_string(topology_updates) + ","
                "\"generations_max\": " + std::to_string(stats.generations_max) + ","
                "\"splits_total\": " + std::to_string(stats.split_count_total) + ","
                "\"data_source\": \"" + data_source + "\","
                "\"source_type\": \"" + source_type + "\","
                "\"elapsed_seconds\": " + std::to_string(elapsed) + ","
                "\"progress_pct\": " + std::to_string(static_cast<int>((epoch + 1) * 100 / epochs)) + ","
                "\"batch_size\": " + std::to_string(batch_size) + ","
                "\"learning_rate_shift\": " + std::to_string(net_config.learning_rate_shift) + ","
                "\"steane_correction\": " + std::string(steane_correction ? "true" : "false") + ","
                "\"flash_cim\": " + std::string(flash_cim ? "true" : "false") + ","
                "\"context_window\": " + std::to_string(context_window) +
                "}";
            
            log_progress(epoch_json);
            
            // Log topology evolution
            log_progress("{\"status\": \"topology\", \"epoch\": " + std::to_string(epoch + 1) + 
                        ", \"experts\": " + std::to_string(stats.num_experts) + 
                        ", \"edges\": " + std::to_string(stats.num_edges) + 
                        ", \"density\": " + std::to_string(stats.graph_density) + 
                        ", \"max_beta_1\": " + std::to_string(stats.max_beta_1) + "}");
            
            // ALWAYS save lightweight epoch checkpoint at end of every epoch
            // This allows resuming from any epoch, not just checkpoint_interval boundaries
            if (output_dir.length() > 0) {
                std::string epoch_ckpt_path = output_dir + "checkpoint_latest.json";
                std::ofstream epoch_ckpt(epoch_ckpt_path);
                if (epoch_ckpt.is_open()) {
                    epoch_ckpt << "{\n";
                    epoch_ckpt << "  \"checkpoint_type\": \"epoch_end\",\n";
                    epoch_ckpt << "  \"epoch\": " << (epoch + 1) << ",\n";
                    epoch_ckpt << "  \"total_epochs\": " << epochs << ",\n";
                    epoch_ckpt << "  \"experts\": " << stats.num_experts << ",\n";
                    epoch_ckpt << "  \"edges\": " << stats.num_edges << ",\n";
                    epoch_ckpt << "  \"density\": " << stats.graph_density << ",\n";
                    epoch_ckpt << "  \"elapsed_seconds\": " << elapsed << ",\n";
                    epoch_ckpt << "  \"checkpoint_time\": \"" << std::time(nullptr) << "\"\n";
                    epoch_ckpt << "}\n";
                    epoch_ckpt.close();
                }
                
                // Also update the live checkpoint to mark epoch as complete
                save_checkpoint(epoch + 1, total_samples, stats.num_experts);
                
                log_progress("{\"status\": \"epoch_complete\", \"epoch\": " + std::to_string(epoch + 1) + 
                            ", \"experts\": " + std::to_string(stats.num_experts) + "}");
            }
            
            // Save FULL checkpoint with weights if checkpoint_interval is set and this is a checkpoint epoch
            if (checkpoint_interval > 0 && output_dir.length() > 0 && (epoch + 1) % checkpoint_interval == 0) {
                // Create checkpoint filename
                std::string checkpoint_path = output_dir + "checkpoint_epoch" + std::to_string(epoch + 1) + ".bin";
                
                // Ensure output directory exists
                std::filesystem::create_directories(output_dir);
                
                // Save checkpoint
                std::ofstream ckpt_file(checkpoint_path, std::ios::binary);
                if (ckpt_file.is_open()) {
                    ckpt_file << "QMINI_TNN_CHECKPOINT v1.0\n";
                    ckpt_file << "epoch: " << (epoch + 1) << "\n";
                    ckpt_file << "total_epochs: " << epochs << "\n";
                    ckpt_file << "experts: " << moe_experts << "\n";
                    ckpt_file << "top_k: " << moe_top_k << "\n";
                    ckpt_file << "context_window: " << context_window << "\n";
                    ckpt_file << "checkpoint_time_s: " << elapsed << "\n";
                    ckpt_file << "---WEIGHTS---\n";
                    
                    size_t total_params = 0;
                    // Only save experts that have been initialized (lazy init protection)
                    auto initialized_experts = tnn.get_initialized_expert_ids();
                    for (size_t expert_id : initialized_experts) {
                        auto expert = tnn.get_expert(expert_id);
                        if (expert) {
                            auto weights_data = expert->serialize_weights();
                            ckpt_file << "EXPERT_" << expert_id << "\n";
                            ckpt_file << "params: " << expert->parameter_count() << "\n";
                            ckpt_file << "bytes: " << weights_data.size() << "\n";
                            ckpt_file.write(reinterpret_cast<const char*>(weights_data.data()), weights_data.size());
                            ckpt_file << "\n";
                            total_params += expert->parameter_count();
                        }
                    }
                    
                    ckpt_file << "---END---\n";
                    ckpt_file << "total_params: " << total_params << "\n";
                    ckpt_file.close();
                    
                    log_progress("{\"status\": \"full_checkpoint\", \"epoch\": " + std::to_string(epoch + 1) + 
                                ", \"path\": \"" + checkpoint_path + "\", \"experts\": " + 
                                std::to_string(stats.num_experts) + "}");
                }
            }
        }  // End epoch loop
            
            // Save model at end of each full training cycle
            if (!output_path.empty()) {
                std::string cycle_output_path = output_path;
                if (continuous_mode) {
                    // In continuous mode, save with iteration number
                    cycle_output_path = output_path + "_cycle" + std::to_string(global_iteration);
                }
                
                std::ofstream out_file(cycle_output_path, std::ios::binary);
                if (out_file.is_open()) {
                    out_file << "QMINI_TNN_MODEL v1.0\n";
                    out_file << "experts: " << moe_experts << "\n";
                    out_file << "top_k: " << moe_top_k << "\n";
                    out_file << "context_window: " << context_window << "\n";
                    out_file << "epochs_trained: " << epochs << "\n";
                    out_file << "continuous_iteration: " << global_iteration << "\n";
                    out_file << "total_samples: " << dataset.size() << "\n";
                    out_file << "training_time_s: " << std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::steady_clock::now() - start_time).count() << "\n";
                    out_file << "---WEIGHTS---\n";
                    
                    size_t total_params = 0;
                    // Only save experts that have been initialized (lazy init protection)
                    auto initialized_experts = tnn.get_initialized_expert_ids();
                    for (size_t expert_id : initialized_experts) {
                        auto expert = tnn.get_expert(expert_id);
                        if (expert) {
                            auto weights_data = expert->serialize_weights();
                            out_file << "EXPERT_" << expert_id << "\n";
                            out_file << "params: " << expert->parameter_count() << "\n";
                            out_file << "bytes: " << weights_data.size() << "\n";
                            out_file.write(reinterpret_cast<const char*>(weights_data.data()), 
                                         weights_data.size());
                            out_file << "\n";
                            total_params += expert->parameter_count();
                        }
                    }
                    
                    out_file << "---END---\n";
                    out_file << "total_params: " << total_params << "\n";
                    out_file.close();
                    
                    log_progress("{\"status\": \"progress\", \"step\": \"Saved model to " + cycle_output_path + " with " + std::to_string(initialized_experts.size()) + " experts\"}");
                }
            }
            
            // In continuous mode, increment iteration and continue
            if (continuous_mode) {
                global_iteration++;
                auto total_elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::steady_clock::now() - start_time).count();
                log_progress("{\"status\": \"continuous_cycle_complete\", \"iteration\": " + 
                            std::to_string(global_iteration) + ", \"total_samples\": " + 
                            std::to_string(dataset.size()) + ", \"total_time_s\": " + 
                            std::to_string(total_elapsed) + ", \"message\": \"Starting next cycle...\"}");
                
                // Continue the while loop - never exit in continuous mode
                continuous_running = true;
            } else {
                // Non-continuous mode: exit after one cycle
                continuous_running = false;
            }
        }  // End continuous while loop
        
        auto end_time = std::chrono::steady_clock::now();
        std::chrono::duration<double> elapsed = end_time - start_time;

        if (!output_path.empty()) {
            // Save trained model weights to file
            std::ofstream out_file(output_path, std::ios::binary);
            if (out_file.is_open()) {
                // Write model metadata header
                out_file << "QMINI_TNN_MODEL v1.0\n";
                out_file << "experts: " << moe_experts << "\n";
                out_file << "top_k: " << moe_top_k << "\n";
                out_file << "context_window: " << context_window << "\n";
                out_file << "epochs_trained: " << epochs << "\n";
                out_file << "training_time_s: " << elapsed.count() << "\n";
                out_file << "---WEIGHTS---\n";
                
                // Serialize actual model weights from initialized experts only (lazy init protection)
                size_t total_params = 0;
                auto initialized_experts = tnn.get_initialized_expert_ids();
                for (size_t expert_id : initialized_experts) {
                    // Get expert weights from TNN
                    auto expert = tnn.get_expert(expert_id);
                    if (expert) {
                        auto weights_data = expert->serialize_weights();
                        
                        // Write expert header
                        out_file << "EXPERT_" << expert_id << "\n";
                        out_file << "params: " << expert->parameter_count() << "\n";
                        out_file << "bytes: " << weights_data.size() << "\n";
                        
                        // Write binary weight data
                        out_file.write(reinterpret_cast<const char*>(weights_data.data()), 
                                     weights_data.size());
                        out_file << "\n";
                        
                        total_params += expert->parameter_count();
                    }
                }
                
                out_file << "---END---\n";
                out_file << "total_params: " << total_params << "\n";
                out_file.close();
                
                std::string save_msg = "{\"status\": \"progress\", \"step\": \"Saved trained MoE model to " + output_path + " with " + std::to_string(initialized_experts.size()) + " experts\"}";
                log_progress(save_msg);
            } else {
                std::string err_msg = "{\"status\": \"error\", \"step\": \"Failed to save model to " + output_path + "\"}";
                log_progress(err_msg);
            }
            std::cout.flush();
        }

        auto final_elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - start_time).count();
        std::string complete_msg = "{\"status\": \"complete\", \"message\": \"Training complete\", \"time_s\": " + std::to_string(final_elapsed) + "}";
        log_progress(complete_msg);
    } catch (const std::exception& e) {
        log_progress("{\"status\": \"error\", \"message\": \"Exception: " + std::string(e.what()) + "\"}");
        return 1;
    }
    
    return 0;
}
