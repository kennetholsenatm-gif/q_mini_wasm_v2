#include <iostream>
#include <cstdlib>
#include <string>
#include <signal.h>
#include "inference_service.hpp"

using namespace q_mini_incus::containers::inference;

// Global for signal handling
static InferenceService* g_service = nullptr;

void SignalHandler(int sig) {
    std::cout << "[inference] Received signal " << sig << ", shutting down...\n";
    if (g_service) {
        g_service->Shutdown();
    }
}

int main(int argc, char* argv[]) {
    std::cout << "╔═══════════════════════════════════════════════════════════╗\n";
    std::cout << "║     Q-Mini Incus - Ternary-Native Inference Service       ║\n";
    std::cout << "║           GF(3) Computation in Linux Namespace            ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════════╝\n\n";
    
    // Check if running in ternary-native mode
    const char* incus_native = std::getenv("Q_MINI_INCUS_NATIVE");
    if (!incus_native || std::string(incus_native) != "1") {
        std::cerr << "[inference] WARNING: Not in ternary-native mode\n";
        std::cerr << "[inference] Set Q_MINI_INCUS_NATIVE=1 for proper operation\n";
    }
    
    // Parse configuration
    InferenceService::Config config;
    
    if (const char* experts = std::getenv("QMINI_EXPERTS")) {
        config.num_experts = std::stoul(experts);
    }
    if (const char* active = std::getenv("QMINI_ACTIVE_EXPERTS")) {
        config.active_experts = std::stoul(active);
    }
    if (const char* model = std::getenv("QMINI_MODEL_PATH")) {
        config.model_path = model;
    }
    
    std::cout << "[inference] Configuration:\n";
    std::cout << "  - Experts: " << config.num_experts << "\n";
    std::cout << "  - Active: " << config.active_experts << "\n";
    std::cout << "  - Model: " << (config.model_path.empty() ? "(none)" : config.model_path) << "\n";
    
    // Create and run service
    auto service = CreateInferenceService(config);
    g_service = service.get();
    
    // Set up signal handlers
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);
    
    std::cout << "[inference] Initializing...\n";
    if (!service->Initialize()) {
        std::cerr << "[inference] Failed to initialize\n";
        return 1;
    }
    
    std::cout << "[inference] Running (ternary-native mode)\n";
    service->Run();
    
    std::cout << "[inference] Shutdown complete\n";
    g_service = nullptr;
    
    return 0;
}
