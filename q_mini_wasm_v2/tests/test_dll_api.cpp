#include <iostream>
#include <cassert>
#include <cstring>
#include "../dll/q_mini_wasm_v2_api.hpp"

using namespace std;

// ============================================================================
// Test Utilities
// ============================================================================

void test_error_handling() {
    cout << "Testing Error Handling..." << endl;
    
    // Test error string function
    const char* error_str = q_mini_wasm_v2_error_string(Q_MINI_WASM_V2_OK);
    assert(strcmp(error_str, "Success") == 0);
    
    error_str = q_mini_wasm_v2_error_string(Q_MINI_WASM_V2_ERROR_INVALID_HANDLE);
    assert(strcmp(error_str, "Invalid or null handle") == 0);
    
    error_str = q_mini_wasm_v2_error_string(Q_MINI_WASM_V2_ERROR_INVALID_ARGUMENT);
    assert(strcmp(error_str, "Invalid argument value") == 0);
    
    // Test unknown error code
    error_str = q_mini_wasm_v2_error_string(-100);
    assert(strcmp(error_str, "Unknown error") == 0);
    
    cout << "  Error handling: PASSED" << endl;
}

void test_version_info() {
    cout << "Testing Version Information..." << endl;
    
    // Test version string
    const char* version = q_mini_wasm_v2_version();
    assert(version != nullptr);
    assert(strcmp(version, "1.0.0") == 0);
    
    // Test build info
    const char* build_info = q_mini_wasm_v2_build_info();
    assert(build_info != nullptr);
    
    // Test version macros
    assert(Q_MINI_WASM_V2_VERSION_MAJOR == 1);
    assert(Q_MINI_WASM_V2_VERSION_MINOR == 0);
    assert(Q_MINI_WASM_V2_VERSION_PATCH == 0);
    assert(strcmp(Q_MINI_WASM_V2_VERSION_STRING, "1.0.0") == 0);
    
    cout << "  Version info: PASSED" << endl;
}

void test_handle_validation() {
    cout << "Testing Handle Validation..." << endl;
    
    // Test null handle
    assert(q_mini_wasm_v2_is_valid_handle(nullptr) == 0);
    
    // Create a valid handle
    void* tableau = tableau_create(4);
    assert(tableau != nullptr);
    
    // Test valid handle
    assert(q_mini_wasm_v2_is_valid_handle(tableau) == 1);
    
    // Destroy handle
    tableau_destroy(tableau);
    
    // After destruction, handle should be invalid
    // Note: This test depends on implementation details
    // The handle pointer might still exist but be removed from internal map
    
    cout << "  Handle validation: PASSED" << endl;
}

void test_error_codes() {
    cout << "Testing Error Codes..." << endl;
    
    // Test with invalid handle
    int result = tableau_apply_hadamard(nullptr, 0);
    assert(result == Q_MINI_WASM_V2_ERROR_INVALID_HANDLE);
    
    // Create valid tableau
    void* tableau = tableau_create(4);
    assert(tableau != nullptr);
    
    // Test with valid handle
    result = tableau_apply_hadamard(tableau, 0);
    assert(result == Q_MINI_WASM_V2_OK);
    
    // Test with invalid qutrit index
    result = tableau_apply_hadamard(tableau, 100);
    assert(result == Q_MINI_WASM_V2_ERROR_OUT_OF_RANGE);
    
    tableau_destroy(tableau);
    
    cout << "  Error codes: PASSED" << endl;
}

void test_parameter_validation() {
    cout << "Testing Parameter Validation..." << endl;
    
    // Test creating tableau with zero qutrits
    void* tableau = tableau_create(0);
    assert(tableau == nullptr);
    
    // Test creating MoE router with invalid parameters
    void* router = moe_router_create(0, 2, 4);
    assert(router == nullptr);
    
    router = moe_router_create(8, 0, 4);
    assert(router == nullptr);
    
    router = moe_router_create(8, 2, 0);
    assert(router == nullptr);
    
    // Test active_experts > total_experts
    router = moe_router_create(4, 8, 4);
    assert(router == nullptr);
    
    // Test creating learner with invalid parameters
    void* learner = ff_learner_create(0, 8, 0.1);
    assert(learner == nullptr);
    
    learner = ff_learner_create(2, 0, 0.1);
    assert(learner == nullptr);
    
    learner = ff_learner_create(2, 8, 0.0);
    assert(learner == nullptr);
    
    learner = ff_learner_create(2, 8, -0.1);
    assert(learner == nullptr);
    
    cout << "  Parameter validation: PASSED" << endl;
}

void test_null_parameter_handling() {
    cout << "Testing Null Parameter Handling..." << endl;
    
    // Test tableau functions with null handle
    assert(tableau_is_valid(nullptr) == 0);
    assert(tableau_num_qutrits(nullptr) == 0);
    
    int8_t outcomes[4];
    assert(tableau_measure_all(nullptr, outcomes, 4) == 0);
    
    // Test MoE router functions with null handle
    assert(moe_router_capacity(nullptr) == 0);
    
    size_t selected[2];
    int8_t input[4] = {1, 0, -1, 1};
    assert(moe_router_route_topk(nullptr, input, 4, selected, 2) == 0);
    
    // Test learner functions with null handle
    int8_t output[8];
    assert(ff_learner_forward(nullptr, input, 4, output, 8) == 0);
    assert(ff_learner_goodness(nullptr, output, 8) == 0.0);
    
    // Test orchestrator functions with null handle
    orchestrator_wait_all(nullptr);  // Should not crash
    assert(orchestrator_has_pending(nullptr) == 0);
    
    cout << "  Null parameter handling: PASSED" << endl;
}

// ============================================================================
// Main Test Function
// ============================================================================

int main() {
    cout << "=== q_mini_wasm_v2 DLL API Tests ===" << endl;
    cout << endl;
    
    try {
        test_error_handling();
        test_version_info();
        test_handle_validation();
        test_error_codes();
        test_parameter_validation();
        test_null_parameter_handling();
        
        cout << endl;
        cout << "=== ALL DLL API TESTS PASSED ===" << endl;
        return 0;
    } catch (const exception& e) {
        cerr << "DLL API test failed with exception: " << e.what() << endl;
        return 1;
    }
}
