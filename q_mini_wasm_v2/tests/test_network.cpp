#include <iostream>
#include <vector>
#include <random>
#include "../core/network.hpp"
#include "../core/ternary/trit.hpp"

using namespace q_mini_wasm_v2::core;

int main() {
    std::cout << "Initializing Ternary Neural Network Integration Test..." << std::endl;

    NetworkConfig config;
    config.input_dim = 16;
    config.shadow_dim = 16;
    config.hash_dim = 8;
    config.num_layers = 3;
    config.neurons_per_layer = 8;
    config.total_experts = 4;
    config.active_experts = 2;
    config.routing_qutrits = 4;
    config.learning_rate_shift = 2;
    config.worker_threads = 4;

    try {
        TernaryNeuralNetwork network(config);

        // Generate some random positive data
        std::mt19937 rng(42);
        std::uniform_real_distribution<double> dist(-2.0, 2.0);

        std::vector<std::vector<double>> positive_data;
        for (size_t i = 0; i < 10; ++i) {
            std::vector<double> sample(config.input_dim);
            for (auto& val : sample) {
                val = dist(rng);
            }
            positive_data.push_back(sample);
        }

        // Train network
        network.train(positive_data, 2); // 2 epochs

        // Run Inference
        std::vector<double> test_input(config.input_dim);
        for (auto& val : test_input) {
            val = dist(rng);
        }

        auto output = network.infer(test_input);
        
        std::cout << "Inference Output: [";
        for (const auto& trit : output) {
            std::cout << static_cast<int>(trit) << ", ";
        }
        std::cout << "]" << std::endl;

        std::cout << "Ternary Neural Network Test Completed Successfully!" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
