/**
 * Smallest end-to-end check: TernaryNeuralNetwork::train then ::infer (core path:
 * ingestion, MoE route, FF experts, orchestrator). One sample, one epoch, tiny graph.
 */
#include "../core/network.hpp"
#include <iostream>
#include <random>
#include <vector>

int main() {
    using namespace q_mini_wasm_v2::core;

    NetworkConfig config{};
    config.input_dim = 8;
    config.shadow_dim = 8;
    config.hash_dim = 4;
    config.num_layers = 1;
    config.neurons_per_layer = 4;
    config.total_experts = 2;
    config.active_experts = 1;
    config.routing_qutrits = 4;
    config.learning_rate_shift = 2;
    config.worker_threads = 1;
    config.enable_steane = false;
    config.enable_flash_cim = false;

    std::mt19937 rng(7);
    std::uniform_real_distribution<double> dist(-1.0, 1.0);

    std::vector<std::vector<double>> positive_data(1);
    positive_data[0].resize(config.input_dim);
    for (double& v : positive_data[0]) {
        v = dist(rng);
    }

    std::vector<double> infer_in(config.input_dim);
    for (double& v : infer_in) {
        v = dist(rng);
    }

    try {
        TernaryNeuralNetwork net(config);
        std::cout << "e2e_smoke: training (1 sample, 1 epoch)...\n";
        net.train(positive_data, 1);
        std::cout << "e2e_smoke: inferring...\n";
        const std::vector<ternary::Trit> out = net.infer(infer_in);
        if (out.size() != config.neurons_per_layer) {
            std::cerr << "e2e_smoke: bad output size " << out.size() << " expected " << config.neurons_per_layer << '\n';
            return 2;
        }
        std::cout << "e2e_smoke: ok output_len=" << out.size() << '\n';
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "e2e_smoke: " << ex.what() << '\n';
        return 1;
    }
}
