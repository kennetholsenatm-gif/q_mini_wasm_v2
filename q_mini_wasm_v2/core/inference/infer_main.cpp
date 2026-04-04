#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <map>
#include <iomanip>
#include <chrono>
#include "../inference/inference_pipeline.hpp"
#include "../inference/entanglement_token.hpp"
#include "../inference/geometric_context.hpp"
#include "../inference/householder_synthesizer.hpp"
#include "../moe/router.hpp"

using namespace q_mini_wasm_v2::core::inference;

// A simple mock vocabulary to translate words to/from pseudo-tokens
std::map<std::string, size_t> word_to_id;
std::map<size_t, std::string> id_to_word;
size_t next_id = 1;

size_t get_or_create_token(const std::string& word) {
    if (word_to_id.find(word) == word_to_id.end()) {
        word_to_id[word] = next_id;
        id_to_word[next_id] = word;
        next_id++;
    }
    return word_to_id[word];
}

std::string get_word(size_t id) {
    if (id_to_word.find(id) != id_to_word.end()) {
        return id_to_word[id];
    }
    return "token_" + std::to_string(id);
}

int main(int argc, char* argv[]) {
    std::string prompt = "";
    size_t max_tokens = 50;
    double temperature = 0.7;
    std::string agent = "chat";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--prompt" && i + 1 < argc) {
            prompt = argv[++i];
        } else if (arg == "--max-tokens" && i + 1 < argc) {
            max_tokens = std::stoull(argv[++i]);
        } else if (arg == "--temperature" && i + 1 < argc) {
            temperature = std::stod(argv[++i]);
        } else if (arg == "--agent" && i + 1 < argc) {
            agent = argv[++i];
        }
    }

    if (prompt.empty()) {
        std::cout << "{\"error\": \"No prompt provided\"}\n";
        return 1;
    }

    // Tokenize prompt
    std::vector<size_t> prompt_tokens;
    std::stringstream ss(prompt);
    std::string word;
    while (ss >> word) {
        prompt_tokens.push_back(get_or_create_token(word));
    }

    // Configure and create inference pipeline
    InferencePipelineConfig config;
    config.token_config.vocab_size = 10000;
    config.token_config.embedding_dim = 16;
    config.token_config.max_sequence_length = 64;
    config.token_config.entanglement_depth = 2;
    
    config.context_config.max_context_length = 64;
    config.context_config.multivector_dim = 3;
    config.context_config.manifold_tolerance = 0.01;
    config.context_config.enable_manifold_normalization = true;
    
    config.synthesis_config.max_qutrits = 100;
    config.synthesis_config.target_precision = 0.99;
    config.synthesis_config.max_reflection_depth = 10;
    config.synthesis_config.enable_parallel_synthesis = false;
    
    config.moe_config.total_experts = 8;
    config.moe_config.active_experts = 2;
    config.moe_config.routing_qutrits = 4;
    
    config.target_latency_ms = 100.0;
    config.num_experts_to_activate = 2;

    try {
        auto pipeline = create_inference_pipeline(config);
        pipeline->warmup(1);

        InferenceInput input;
        input.token_ids = prompt_tokens;
        input.max_output_length = max_tokens;
        input.temperature = temperature;
        input.use_entanglement = true;
        input.use_geometric_context = true;

        auto output = pipeline->infer(input);

        std::string generated_text;
        
        // Add a role-specific flavor based on agent
        if (agent == "code") {
            generated_text += "```cpp\n// Generated context based on qutrit inference\n";
        } else if (agent == "chat") {
            generated_text += "I've analyzed your prompt using geometric entanglement. Here is the response: ";
        }
        
        for (size_t id : output.output_tokens) {
            // For untrained network, it might generate out of vocab ids. Map them nicely.
            generated_text += get_word((id % next_id == 0) ? 1 : (id % next_id)) + " ";
        }
        
        if (agent == "code") {
            generated_text += "\n```";
        }
        
        // Escape quotes for JSON
        std::string escaped_text = "";
        for (char c : generated_text) {
            if (c == '\"') escaped_text += "\\\"";
            else if (c == '\\') escaped_text += "\\\\";
            else if (c == '\n') escaped_text += "\\n";
            else if (c == '\r') escaped_text += "\\r";
            else escaped_text += c;
        }

        std::cout << "{"
                  << "\"text\": \"" << escaped_text << "\", "
                  << "\"prompt_tokens\": " << prompt_tokens.size() << ", "
                  << "\"completion_tokens\": " << output.output_tokens.size() << ", "
                  << "\"total_latency_ms\": " << output.total_latency_ms 
                  << "}\n";
                  
    } catch (const std::exception& e) {
        std::string err = e.what();
        std::string escaped_err = "";
        for (char c : err) {
            if (c == '\"') escaped_err += "\\\"";
            else if (c == '\\') escaped_err += "\\\\";
            else if (c == '\n') escaped_err += "\\n";
            else escaped_err += c;
        }
        std::cout << "{\"error\": \"" << escaped_err << "\"}\n";
        return 1;
    }

    return 0;
}
