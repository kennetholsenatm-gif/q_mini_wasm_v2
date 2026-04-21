#include <iostream>
#include <fstream>
#include <chrono>
#include <thread>

int main() {
    // Write to static init log
    std::ofstream static_log("C:/q_mini_data/trainer_static_init.log", std::ios::trunc);
    static_log << "[StaticInit] Starting global initialization..." << std::endl;
    static_log.flush();
    
    // Write to SSE file
    std::ofstream sse_file("C:/q_mini_data/trainer_sse_output.txt", std::ios::trunc);
    if (sse_file.is_open()) {
        sse_file << "{\"status\": \"init\", \"message\": \"Trainer starting...\"}" << std::endl;
        sse_file.flush();
    }
    
    // Write to debug log
    std::ofstream debug_log("C:/q_mini_data/trainer_debug.log", std::ios::trunc);
    debug_log << "[STANDALONE] Minimal trainer started" << std::endl;
    debug_log.flush();
    
    static_log << "[StaticInit] Global init complete, about to call main..." << std::endl;
    static_log.close();
    
    // Success
    return 0;
}
