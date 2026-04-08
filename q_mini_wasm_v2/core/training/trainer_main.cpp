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
#include "../ternary/trit.hpp"
#include "../network.hpp"
#include "data_synthesizer.hpp"

using namespace q_mini_wasm_v2::core;

// Simple JSON text extraction for dataset loading
std::string extract_json_text(const std::string& line) {
    // Look for "text":"..." pattern
    size_t start = line.find("\"text\":\"");
    if (start == std::string::npos) return "";
    start += 8; // skip "text":"

    size_t end = line.find("\",\"", start);
    if (end == std::string::npos) {
        end = line.find("\"}", start);
    }
    if (end == std::string::npos) return "";

    return line.substr(start, end - start);
}

// Convert text string to ternary trits (64-dimensional)
std::vector<ternary::Trit> text_to_trits(const std::string& text, size_t dim = 64) {
    std::vector<ternary::Trit> result(dim, ternary::Trit::ZERO);

    for (size_t i = 0; i < dim && i < text.size(); ++i) {
        // Map char to ternary: hash and mod 3
        int val = static_cast<unsigned char>(text[i]) % 3;
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
    
    size_t get_size_t(const std::string& section, const std::string& key) {
        size_t val_pos = find_in_section(section, key);
        if (val_pos == std::string::npos) return 0;
        
        // Skip whitespace and find number
        size_t num_start = raw_content.find_first_of("0123456789", val_pos);
        if (num_start == std::string::npos) return 0;
        
        size_t num_end = raw_content.find_first_not_of("0123456789", num_start);
        std::string num_str = raw_content.substr(num_start, num_end - num_start);
        return std::stoull(num_str);
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
    size_t epochs = has_config ? config.get_size_t("training", "epochs") : 100;
    size_t batch_size = has_config ? config.get_size_t("training", "batch_size") : 8192;
    size_t checkpoint_interval = has_config ? config.get_size_t("training", "checkpoint_interval") : 5;
    
    ternary::ProbTrit learning_rate = ternary::ProbTrit::LOW_PROB;
    size_t context_window = has_config ? config.get_size_t("model", "context_window") : 4096;
    size_t entanglement_tokens = has_config ? config.get_size_t("model", "entanglement_tokens") : 256;
    size_t moe_experts = has_config ? config.get_size_t("model", "moe_experts") : 243;
    size_t moe_top_k = has_config ? config.get_size_t("model", "moe_top_k") : 16;
    bool steane_correction = has_config ? config.get_bool("features", "steane_correction") : true;
    bool flash_cim = has_config ? config.get_bool("features", "flash_cim") : true;
    
    std::string dataset_path = "";
    std::string base_model_path = has_config ? config.get_string("paths", "base_model") : "flash_cim_243expert/checkpoints/final_model.json";
    std::string output_path = "";  // Auto-generated if empty

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

    log_progress("{\"status\": \"init\", \"message\": \"Config loaded: epochs=" + std::to_string(epochs) + ", batch=" + std::to_string(batch_size) + ", experts=" + std::to_string(moe_experts) + "\"}");

    // Configure the network based on the research paper parameters
    NetworkConfig net_config;
    net_config.input_dim = context_window;
    net_config.shadow_dim = 1024;
    net_config.hash_dim = entanglement_tokens;
    net_config.num_layers = 3;  // Common standard for FF layers
    net_config.neurons_per_layer = 128;
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
            std::cout << "{\"status\": \"progress\", \"step\": \"Loading dataset from " << dataset_path << "\"}\n";
            std::cout.flush();
            
            std::ifstream file(dataset_path);
            if (file.is_open()) {
                std::string line;
                size_t loaded = 0;
                while (std::getline(file, line) && loaded < batch_size) {
                    // Extract text from JSON and convert to trits
                    std::string text = extract_json_text(line);
                    if (!text.empty()) {
                        auto sample = text_to_trits(text, context_window);
                        dataset.push_back(sample);
                        loaded++;
                    }
                }
                file.close();
                
            log_progress("{\"status\": \"progress\", \"step\": \"Loaded " + std::to_string(dataset.size()) + " samples from dataset\"}");
            } else {
                log_progress("{\"status\": \"warning\", \"step\": \"Could not open dataset file, using simulation\"}");
            }
        }
        
        // If no dataset loaded, generate synthetic data
        if (dataset.empty()) {
            log_progress("{\"status\": \"progress\", \"step\": \"No dataset provided. Simulating Quantum Distribution...\"}");
            
            // Generate ternary dataset using deterministic seed
            uint32_t seed = 42;
            dataset.resize(batch_size, std::vector<ternary::Trit>(context_window));
            for (auto& row : dataset) {
                for (auto& val : row) {
                    // Simple deterministic ternary generation
                    seed = (seed * 1103515245 + 12345) & 0x7fffffff;
                    int trit_val = seed % 3;
                    if (trit_val == 0) val = ternary::Trit::NEGATIVE;
                    else if (trit_val == 1) val = ternary::Trit::ZERO;
                    else val = ternary::Trit::POSITIVE;
                }
            }
        }
        
        log_progress("{\"status\": \"progress\", \"step\": \"Commencing Forward-Forward Entropy Training...\"}");

        auto start_time = std::chrono::steady_clock::now();
        
        // Training loop - per batch per epoch for API-based training
        for (size_t epoch = 0; epoch < epochs; ++epoch) {
            // Convert dataset to double format for this epoch
            std::vector<std::vector<double>> batch_data;
            for (const auto& sample : dataset) {
                std::vector<double> sample_vec;
                sample_vec.reserve(sample.size());
                for (const auto& trit : sample) {
                    sample_vec.push_back(static_cast<double>(trit));
                }
                batch_data.push_back(std::move(sample_vec));
            }
            
            if (!batch_data.empty()) {
                // Train on batch for 1 epoch
                tnn.train(batch_data, 1);
            }
            
            log_progress("{\"status\": \"epoch\", \"epoch\": " + std::to_string(epoch + 1) + ", \"samples\": " + std::to_string(batch_data.size()) + "}");
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
