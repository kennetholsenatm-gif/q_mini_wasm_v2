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
#include "../ternary/trit.hpp"
#include "../network.hpp"
#include "../moe/self_organizing_expert.hpp"
#include "data_synthesizer.hpp"

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
    
    bool get_bool(const std::string& section, const std::string& key) {
        size_t val_pos = find_in_section(section, key);
        if (val_pos == std::string::npos) return false;
        
        // Find value
        size_t val_start = raw_content.find_first_not_of(" \t", val_pos);
        if (val_start == std::string::npos) return false;
        
        std::string val = raw_content.substr(val_start, 5);
        return val.substr(0, 4) == "true" || val.substr(0, 1) == "1";
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
    std::string config_path = "flash_cim_243expert/training_config.toml";
    
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
    bool steane_correction = has_config ? config.get_bool("features", "steane_correction") : true;
    bool flash_cim = has_config ? config.get_bool("features", "flash_cim") : true;

    // Fail fast if config not loaded properly
    if (moe_experts == 0) {
        std::cerr << "[ERROR] moe_experts not found in config file or config not loaded. Please check " << config_path << std::endl;
        return 1;
    }
    
    std::string dataset_path = "";
    std::string base_model_path = has_config ? config.get_string("paths", "base_model") : "flash_cim_243expert/checkpoints/final_model.json";
    std::string output_path = "";  // Auto-generated if empty
    std::string output_dir = has_config ? config.get_string("paths", "output_dir") : "flash_cim_243expert/checkpoints/";

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
    net_config.routing_qutrits = 8;
    net_config.learning_rate_shift = 2; // Fixed shift equivalent to old learning rate approximation
    net_config.worker_threads = 4;
    net_config.enable_steane = steane_correction;
    net_config.enable_flash_cim = flash_cim;

    try {
        TernaryNeuralNetwork tnn(net_config);
        
        log_progress("{\"status\": \"progress\", \"step\": \"Network configured. Architecture: MoE Routing, FF Layers, Steane Polling\"}");

        // Dataset loading or simulation
        std::vector<std::vector<ternary::Trit>> dataset;
        if (!dataset_path.empty()) {
            log_progress("{\"status\": \"progress\", \"step\": \"Loading dataset from: " + dataset_path + "\"}");
            
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
        
        // Fetch web data sources from config
        std::vector<std::string> web_sources = config.get_string_array("data_sources", "urls");
        if (!web_sources.empty()) {
            log_progress("{\"status\": \"progress\", \"step\": \"Fetching " + std::to_string(web_sources.size()) + " web data sources...\"}");
            
            for (const auto& url : web_sources) {
                if (url.empty()) continue;
                
                log_progress("{\"status\": \"progress\", \"step\": \"Fetching: " + url + "\"}");
                
                // Fetch using native HTTP client (WinHTTP on Windows)
                auto response = http_get(url, 30000);
                if (response.success) {
                    
                    // Extract text content (strip HTML tags roughly)
                    std::string text;
                    bool in_tag = false;
                    for (char c : response.body) {
                        if (c == '<') in_tag = true;
                        else if (c == '>') in_tag = false;
                        else if (!in_tag && std::isprint(c)) text += c;
                    }
                    
                    // Split into sentences/samples and convert to trits
                    size_t samples_added = 0;
                    std::string sentence;
                    for (char c : text) {
                        sentence += c;
                        if (c == '.' || c == '!' || c == '?') {
                            if (sentence.length() > 20) {  // Min sentence length
                                auto sample = text_to_trits(sentence, context_window);
                                if (!sample.empty() && dataset.size() < batch_size) {
                                    dataset.push_back(sample);
                                    samples_added++;
                                }
                            }
                            sentence.clear();
                        }
                    }
                    
                    log_progress("{\"status\": \"progress\", \"step\": \"Added " + std::to_string(samples_added) + " samples from web source\"}");
                } else {
                    log_progress("{\"status\": \"warning\", \"step\": \"Failed to fetch: " + url + "\"}");
                }
            }
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
                    "\"web_sources_fetched\": " + std::to_string(web_sources.size()) + "}");
        
        // Initialize Self-Organizing Expert Manager with NO FIXED MAXIMUM
        // Experts will be created/destroyed dynamically based on Betti numbers
        moe::SelfOrganizingExpertManager::Config som_config;
        som_config.initial_experts = 8;        // Start with 8 seed experts
        som_config.split_beta1_threshold = 5;  // Split when β₁ > 5 cycles detected
        som_config.target_graph_density = 0.15; // 15% connectivity
        som_config.max_experts_hard_cap = 100000; // Effectively unlimited (100K)
        
        learning::FFConfig ff_config;
        ff_config.num_layers = num_layers;
        ff_config.neurons_per_layer = neurons_per_layer;
        ff_config.learning_rate_shift = net_config.learning_rate_shift;
        
        auto som_manager = std::make_unique<moe::SelfOrganizingExpertManager>(som_config, ff_config);
        
        log_progress("{\"status\": \"progress\", \"step\": \"Self-Organizing Manager initialized with " + std::to_string(som_config.initial_experts) + " seed experts\"}");
        log_progress("{\"status\": \"progress\", \"step\": \"Betti β₁ threshold: " + std::to_string(som_config.split_beta1_threshold) + " - experts split when topology complex\"}");
        
        auto start_time = std::chrono::steady_clock::now();
        
        // Live checkpointing - saves state every 5000 samples for crash recovery
        // This activates immediately and protects remaining training time
        size_t checkpoint_counter = 0;
        auto save_checkpoint = [&](size_t current_epoch, size_t samples_done, size_t total_experts) {
            if (output_dir.length() > 0) {
                std::filesystem::create_directories(output_dir);
                std::string state_path = output_dir + "training_state_live.json";
                std::ofstream state_file(state_path);
                if (state_file.is_open()) {
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
                }
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
                                ", \"sample_num\": " + std::to_string(samples_this_epoch) + 
                                ", \"topic\": \"" + detected_topic + "\", "
                                "\"experts_active\": " + std::to_string(stats.num_experts) + 
                                ", \"experts_activated\": " + std::to_string(activated_experts) + "}");
                }
                
                // Periodic topology analysis and progress logging (every 1000 samples)
                if (samples_this_epoch % 1000 == 0) {
                    som_manager->update_topology();
                    topology_updates++;
                    
                    // Log in-epoch progress so UI updates during long epochs
                    auto progress_pct = static_cast<int>(samples_this_epoch * 100 / total_samples);
                    log_progress("{\"status\": \"progress\", \"epoch\": " + std::to_string(epoch + 1) + 
                                ", \"samples_processed\": " + std::to_string(samples_this_epoch) + 
                                ", \"total_samples\": " + std::to_string(total_samples) + 
                                ", \"epoch_progress_pct\": " + std::to_string(progress_pct) + 
                                ", \"topology_updates\": " + std::to_string(topology_updates) + "}");
                    
                    // Live checkpoint every 5000 samples - protects against crashes
                    if (samples_this_epoch % 5000 == 0) {
                        auto stats = som_manager->get_stats();
                        save_checkpoint(epoch + 1, samples_this_epoch, stats.num_experts);
                        checkpoint_counter++;
                        log_progress("{\"status\": \"checkpoint\", \"epoch\": " + std::to_string(epoch + 1) + 
                                    ", \"samples\": " + std::to_string(samples_this_epoch) + 
                                    ", \"checkpoint_number\": " + std::to_string(checkpoint_counter) + "}");
                    }
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
            
            // Save checkpoint if checkpoint_interval is set and this is a checkpoint epoch
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
                    for (size_t expert_id = 0; expert_id < moe_experts; ++expert_id) {
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
                    
                    log_progress("{\"status\": \"checkpoint\", \"epoch\": " + std::to_string(epoch + 1) + ", \"path\": \"" + checkpoint_path + "\"}");
                }
            }
        }
        
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
                
                // Serialize actual model weights from all experts
                // Each expert is a ForwardForwardLearner with ternary weights
                size_t total_params = 0;
                for (size_t expert_id = 0; expert_id < moe_experts; ++expert_id) {
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
                
                log_progress("{\"status\": \"progress\", \"step\": \"Saved trained MoE model to " + output_path + "\"}");
            } else {
                log_progress("{\"status\": \"error\", \"step\": \"Failed to save model to " + output_path + "\"}");
            }
            std::cout.flush();
        }

        log_progress("{\"status\": \"complete\", \"message\": \"Training complete\", \"time_s\": " + std::to_string(elapsed.count()) + "}");
    } catch (const std::exception& e) {
        log_progress("{\"status\": \"error\", \"message\": \"Exception: " + std::string(e.what()) + "\"}");
        return 1;
    }
    
    return 0;
}
