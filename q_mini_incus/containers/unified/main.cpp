#include <iostream>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>
#include <memory>
#include <signal.h>

#include "../../runtime/qminid/init_daemon.hpp"
#include "../inference/inference_service.hpp"

using namespace q_mini_incus::runtime::qminid;
using namespace q_mini_incus::containers::inference;

static InitDaemon* g_daemon = nullptr;

void SignalHandler(int sig) {
    std::cout << "[unified] Received signal " << sig << ", shutting down...\n";
    if (g_daemon) {
        g_daemon->Shutdown();
    }
}

int main(int argc, char* argv[]) {
    std::cout << "╔══════════════════════════════════════════════════════════════════╗\n";
    std::cout << "║       Q-Mini Incus - Unified Ternary-Native Container           ║\n";
    std::cout << "║        All Services in Single Ternary-Native Namespace            ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════════╝\n\n";
    
    // Force ternary-native mode
    setenv("Q_MINI_INCUS_NATIVE", "1", 1);
    setenv("Q_MINI_NO_WASM_BRIDGE", "1", 1);
    setenv("Q_MINI_NAMESPACE_BOUNDARY", "1", 1);
    setenv("Q_MINI_UNIFIED_MODE", "1", 1);
    
    std::cout << "[unified] Starting qminid (ternary-native init daemon)...\n";
    
    // Create init daemon in unified mode
    auto daemon = CreateInitDaemon(false, true);  // unified_mode = true
    g_daemon = daemon.get();
    
    // Set up signal handlers
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);
    
    // Initialize qminid
    if (!daemon->Initialize()) {
        std::cerr << "[unified] Failed to initialize qminid\n";
        return 1;
    }
    
    // Register services
    ServiceConfig inference_config;
    inference_config.name = "inference";
    inference_config.executable_path = "/usr/local/bin/qmini-infer";
    inference_config.enable_ternary_ipc = true;
    inference_config.auto_restart = true;
    daemon->RegisterService(inference_config);
    
    ServiceConfig training_config;
    training_config.name = "training";
    training_config.executable_path = "/usr/local/bin/qmini-trainer";
    training_config.enable_ternary_ipc = true;
    daemon->RegisterService(training_config);
    
    ServiceConfig wui_config;
    wui_config.name = "wui";
    wui_config.executable_path = "/usr/local/bin/qmini-wui";
    wui_config.enable_ternary_ipc = true;
    daemon->RegisterService(wui_config);
    
    ServiceConfig agent_config;
    agent_config.name = "agents";
    agent_config.executable_path = "/usr/local/bin/qmini-agents";
    agent_config.enable_ternary_ipc = true;
    daemon->RegisterService(agent_config);
    
    // Start all services
    std::cout << "[unified] Starting services...\n";
    daemon->StartService("inference");
    daemon->StartService("training");
    daemon->StartService("wui");
    daemon->StartService("agents");
    
    std::cout << "[unified] All services started. Running main loop.\n";
    std::cout << "          Press Ctrl+C to shutdown.\n\n";
    
    // Run main event loop
    daemon->Run();
    
    std::cout << "\n[unified] Shutdown complete\n";
    g_daemon = nullptr;
    
    return 0;
}
