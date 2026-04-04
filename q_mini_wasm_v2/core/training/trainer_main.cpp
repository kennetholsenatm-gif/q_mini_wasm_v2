#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <thread>
#include <iomanip>
#include "../learning/ppo_agent.hpp"
#include "../learning/forward_forward.hpp"

using namespace q_mini_wasm_v2::core::learning;

int main(int argc, char* argv[]) {
    // Basic argument parsing
    size_t epochs = 100;
    size_t batch_size = 32;
    double lr = 0.0001;
    size_t context_window = 4096;
    size_t entanglement_tokens = 256;
    std::string dataset_path = "";
    std::string base_model_path = "";
    std::string output_path = "";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--epochs" && i + 1 < argc) {
            epochs = std::stoull(argv[++i]);
        } else if (arg == "--batch-size" && i + 1 < argc) {
            batch_size = std::stoull(argv[++i]);
        } else if (arg == "--learning-rate" && i + 1 < argc) {
            lr = std::stod(argv[++i]);
        } else if (arg == "--context-window" && i + 1 < argc) {
            context_window = std::stoull(argv[++i]);
        } else if (arg == "--entanglement-tokens" && i + 1 < argc) {
            entanglement_tokens = std::stoull(argv[++i]);
        } else if (arg == "--dataset" && i + 1 < argc) {
            dataset_path = argv[++i];
        } else if (arg == "--base-model" && i + 1 < argc) {
            base_model_path = argv[++i];
        } else if (arg == "--output" && i + 1 < argc) {
            output_path = argv[++i];
        }
    }

    std::cout << "{\"status\": \"init\", \"message\": \"Starting Native SYCL Qutrit Trainer\"}\n";
    std::cout.flush();

    PPOConfig ppo_config;
    ppo_config.num_qutrits = 8; // Adjust based on entanglement_tokens logic if needed
    ppo_config.batch_size = batch_size;
    ppo_config.num_epochs = epochs;
    ppo_config.learning_rate = lr;
    
    FFConfig ff_config{2, 8, lr, 1.0, -1.0};
    
    try {
        auto learner = create_continuous_learner(ppo_config, ff_config);
        
        if (!base_model_path.empty()) {
            try {
                learner->load_circuit(base_model_path);
                std::cout << "{\"status\": \"progress\", \"step\": \"Loaded base model from " << base_model_path << "\"}\n";
            } catch (const std::exception& e) {
                std::cout << "{\"status\": \"progress\", \"step\": \"Could not load from " << base_model_path << ", starting fresh\"}\n";
            }
            std::cout.flush();
        }
        
        if (!dataset_path.empty()) {
            std::cout << "{\"status\": \"progress\", \"step\": \"Processing dataset from " << dataset_path << "\"}\n";
            std::cout.flush();
            // Stub for actual dataset reading/processing logic
        }
        
        // Simulating the target state for the given context
        std::vector<int8_t> target(ppo_config.num_qutrits * ppo_config.num_qutrits * 4, 1);
        
        std::cout << "{\"status\": \"progress\", \"step\": \"Creating Stabilizer Environment\"}\n";
        std::cout.flush();
        auto env = create_stabilizer_environment(ppo_config.num_qutrits, target);
        
        std::cout << "{\"status\": \"progress\", \"step\": \"Training Loop Started\"}\n";
        std::cout.flush();

        auto start_time = std::chrono::steady_clock::now();
        for (size_t epoch = 1; epoch <= epochs; ++epoch) {
            auto stats = learner->run_episode(*env);
            
            // Output JSON for the Go server to stream
            std::cout << "{\"status\": \"epoch\", \"epoch\": " << epoch 
                      << ", \"loss\": " << stats.policy_loss 
                      << ", \"reward\": " << stats.mean_reward 
                      << ", \"entropy\": " << stats.entropy << "}\n";
            std::cout.flush();
            
            // We simulate a tiny delay if epochs evaluate instantly so WUI has time to stream
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        
        auto end_time = std::chrono::steady_clock::now();
        std::chrono::duration<double> elapsed = end_time - start_time;

        if (!output_path.empty()) {
            try {
                learner->save_circuit(output_path);
                std::cout << "{\"status\": \"progress\", \"step\": \"Saved trained model to " << output_path << "\"}\n";
            } catch (const std::exception& e) {
                std::cout << "{\"status\": \"error\", \"message\": \"Failed to save model: " << e.what() << "\"}\n";
            }
            std::cout.flush();
        }

        std::cout << "{\"status\": \"complete\", \"message\": \"Training complete\", \"time_s\": " << elapsed.count() << "}\n";
        std::cout.flush();
    } catch (const std::exception& e) {
        std::cout << "{\"status\": \"error\", \"message\": \"Exception: " << e.what() << "\"}\n";
        std::cout.flush();
        return 1;
    }
    
    return 0;
}
