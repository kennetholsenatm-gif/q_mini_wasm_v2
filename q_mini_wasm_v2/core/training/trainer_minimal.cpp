#include <iostream>
#include <fstream>

int main(int argc, char* argv[]) {
    // Write to SSE file immediately
    std::ofstream sse_file("C:/q_mini_data/trainer_minimal_sse.txt", std::ios::trunc);
    if (sse_file.is_open()) {
        sse_file << "{\"status\": \"minimal_test\", \"message\": \"Success!\"}" << std::endl;
        sse_file.flush();
    }
    
    // Write to debug log
    std::ofstream debug_log("C:/q_mini_data/trainer_minimal_debug.txt", std::ios::trunc);
    debug_log << "[MINIMAL] Success" << std::endl;
    debug_log.flush();
    
    return 0;
}
