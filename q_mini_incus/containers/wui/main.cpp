#include <iostream>
#include <cstdlib>
#include <string>
#include <signal.h>

// Stub WUI service main
void SignalHandler(int sig) {
    std::cout << "[wui] Received signal " << sig << ", shutting down...\n";
}

int main(int argc, char* argv[]) {
    std::cout << "╔═══════════════════════════════════════════════════════════╗\n";
    std::cout << "║     Q-Mini Incus - Ternary-Native WUI Service             ║\n";
    std::cout << "║           GF(3) Computation in Linux Namespace            ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════════╝\n\n";
    
    const char* incus_native = std::getenv("Q_MINI_INCUS_NATIVE");
    if (!incus_native || std::string(incus_native) != "1") {
        std::cerr << "[wui] WARNING: Not in ternary-native mode\n";
    }
    
    const char* port = std::getenv("QMINI_WUI_PORT");
    std::cout << "[wui] WUI service starting on port " << (port ? port : "3000") << " (stub)\n";
    
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);
    
    // Stub main loop
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        std::cout << "[wui] Heartbeat...\n";
    }
    
    return 0;
}
