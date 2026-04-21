// MINIMAL TRAINER TEST
#include <iostream>
#include <fstream>
#include <windows.h>

int main(int argc, char* argv[]) {
    // Write to Windows Event log equivalent (just a debug file)
    FILE* debug = fopen("C:/q_mini_data/trainer_debug_startup.txt", "w");
    if (debug) {
        fprintf(debug, "[DEBUG] Process started\n");
        fclose(debug);
    }
    
    std::ofstream log("C:/q_mini_data/trainer_test.log", std::ios::trunc);
    if (!log.is_open()) {
        FILE* err = fopen("C:/q_mini_data/trainer_error.txt", "w");
        if (err) {
            fprintf(err, "[ERROR] Failed to open trainer_test.log\n");
            fclose(err);
        }
        return 1;
    }
    log << "[TEST] Minimal trainer started" << std::endl;
    log.flush();
    
    std::ofstream sse("C:/q_mini_data/trainer_sse_output.txt", std::ios::trunc);
    if (!sse.is_open()) {
        log << "[ERROR] Failed to open trainer_sse_output.txt" << std::endl;
        return 1;
    }
    sse << "{\"status\": \"init\", \"message\": \"Minimal trainer running\"}" << std::endl;
    sse.flush();
    
    log << "[TEST] Success - files written" << std::endl;
    
    return 0;
}
