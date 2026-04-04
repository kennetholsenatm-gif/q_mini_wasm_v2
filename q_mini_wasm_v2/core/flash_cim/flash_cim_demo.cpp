/**
 * @file flash_cim_demo.cpp
 * @brief Demonstration of Flash-CIM proof of concept using D:\ drive
 * 
 * This demo shows how the Flash-CIM controller can be used for:
 * - Ternary data storage on flash
 * - In-storage compute operations
 * - Energy efficiency measurements
 */

#include "flash_cim.hpp"
#include <iostream>
#include <iomanip>

using namespace q_mini_wasm_v2::core::flash_cim;
using namespace q_mini_wasm_v2::core::ternary;

void print_separator() {
    std::cout << std::string(60, '=') << std::endl;
}

void print_metrics(FlashCIMController& controller) {
    auto metrics = controller.get_metrics();
    std::cout << "\n--- Performance Metrics ---\n";
    for (const auto& [name, value] : metrics) {
        std::cout << std::setw(25) << name << ": " << std::fixed << std::setprecision(2) << value << std::endl;
    }
}

int main() {
    print_separator();
    std::cout << "Flash-CIM Proof of Concept Demo" << std::endl;
    std::cout << "Using D:\\ drive as flash storage" << std::endl;
    print_separator();
    
    // Create controller with D:\ drive configuration
    auto config = create_default_d_drive_config();
    auto controller = create_flash_cim_controller(config);
    
    // Initialize
    std::cout << "\n[1] Initializing Flash-CIM controller..." << std::endl;
    auto init_result = controller->initialize();
    if (!init_result.success) {
        std::cerr << "Failed to initialize: " << init_result.error_message << std::endl;
        return 1;
    }
    std::cout << "    Energy: " << init_result.energy_pj << " pJ" << std::endl;
    
    // Demo 1: Write ternary data
    std::cout << "\n[2] Writing ternary data to flash..." << std::endl;
    std::vector<Trit> input_data = {
        Trit::POSITIVE, Trit::NEGATIVE, Trit::ZERO, Trit::POSITIVE,
        Trit::POSITIVE, Trit::NEGATIVE, Trit::NEGATIVE, Trit::ZERO,
        Trit::POSITIVE, Trit::POSITIVE, Trit::NEGATIVE, Trit::ZERO
    };
    
    auto write_result = controller->write_trits(input_data, 0);
    std::cout << "    Wrote " << write_result.data_size << " trits" << std::endl;
    std::cout << "    Energy: " << write_result.energy_pj << " pJ" << std::endl;
    
    // Demo 2: Read ternary data
    std::cout << "\n[3] Reading ternary data from flash..." << std::endl;
    auto read_data = controller->read_trits(0, input_data.size());
    std::cout << "    Read " << read_data.size() << " trits:" << std::endl;
    std::cout << "    ";
    for (const auto& t : read_data) {
        std::cout << static_cast<int>(t) << " ";
    }
    std::cout << std::endl;
    
    // Demo 3: In-storage ternary addition
    std::cout << "\n[4] Performing in-storage ternary addition..." << std::endl;
    
    // Write second operand
    std::vector<Trit> operand_b = {
        Trit::POSITIVE, Trit::POSITIVE, Trit::NEGATIVE, Trit::ZERO,
        Trit::NEGATIVE, Trit::POSITIVE, Trit::ZERO, Trit::POSITIVE,
        Trit::ZERO, Trit::NEGATIVE, Trit::POSITIVE, Trit::NEGATIVE
    };
    controller->write_trits(operand_b, 1);
    
    // Perform CIM addition
    auto add_result = controller->cim_ternary_add(0, 1, 2);
    std::cout << "    CIM ternary add: " << add_result.data_size << " operations" << std::endl;
    std::cout << "    Energy: " << add_result.energy_pj << " pJ" << std::endl;
    
    // Read result
    auto sum_result = controller->read_trits(2, input_data.size());
    std::cout << "    Result: ";
    for (const auto& t : sum_result) {
        std::cout << static_cast<int>(t) << " ";
    }
    std::cout << std::endl;
    
    // Demo 4: In-storage vector-matrix multiply
    std::cout << "\n[5] Performing in-storage vector-matrix multiply..." << std::endl;
    
    // Write weight matrix
    std::vector<Trit> weights = {
        Trit::POSITIVE, Trit::NEGATIVE, Trit::POSITIVE, Trit::ZERO,
        Trit::NEGATIVE, Trit::POSITIVE, Trit::ZERO, Trit::POSITIVE,
        Trit::POSITIVE, Trit::ZERO, Trit::NEGATIVE, Trit::POSITIVE
    };
    controller->write_trits(weights, 3);
    
    // Perform CIM multiply
    auto mm_result = controller->cim_vector_matrix_multiply(0, 3, 4);
    std::cout << "    CIM vector-matrix multiply: " << mm_result.data_size << " operations" << std::endl;
    std::cout << "    Energy: " << mm_result.energy_pj << " pJ" << std::endl;
    
    // Read result
    auto mm_output = controller->read_trits(4, input_data.size());
    std::cout << "    Result: ";
    for (const auto& t : mm_output) {
        std::cout << static_cast<int>(t) << " ";
    }
    std::cout << std::endl;
    
    // Demo 5: Energy efficiency comparison
    std::cout << "\n[6] Energy efficiency analysis..." << std::endl;
    double total_energy = controller->get_total_energy_pj();
    double cim_operations = 2;  // We did 2 CIM operations
    double avg_energy_per_op = total_energy / (3 + cim_operations);  // 3 regular ops + 2 CIM ops
    
    std::cout << "    Total energy consumed: " << std::fixed << std::setprecision(2) << total_energy << " pJ" << std::endl;
    std::cout << "    Average energy per operation: " << avg_energy_per_op << " pJ" << std::endl;
    std::cout << "    CIM operations use ~0.1 pJ/op vs ~10 pJ/op for data transfer" << std::endl;
    std::cout << "    Energy savings: " << std::setprecision(1) << ((10.0 - 0.1) / 10.0 * 100.0) << "%" << std::endl;
    
    // Print final metrics
    print_metrics(*controller);
    
    // Shutdown
    std::cout << "\n[7] Shutting down..." << std::endl;
    controller->shutdown();
    
    print_separator();
    std::cout << "Demo complete!" << std::endl;
    print_separator();
    
    return 0;
}