#include <iostream>
#include <fstream>
#include <windows.h>

int main() {
    std::cerr << "[TEST] Starting file write test" << std::endl;
    
    std::ofstream test_file("C:/q_mini_data/test_direct.txt");
    if (test_file.is_open()) {
        test_file << "Direct test successful" << std::endl;
        test_file.flush();
        test_file.close();
        std::cerr << "[TEST] File written successfully" << std::endl;
        return 0;
    } else {
        std::cerr << "[TEST] Failed to open file" << std::endl;
        return 1;
    }
}
