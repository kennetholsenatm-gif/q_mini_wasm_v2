#include <iostream>
#include <cassert>
#include <vector>

#include "../core/ternary/trit.hpp"
#include "../core/stabilizer/tableau.hpp"
#include "../core/moe/router.hpp"
#include "../core/learning/forward_forward.hpp"
#include "../runtime/orchestrator.hpp"
#include "../dll/q_mini_wasm_v2_api.hpp"

using namespace q_mini_wasm_v2;

void test_dll_integration() {
    std::cout << "Testing DLL Integration..." << std::endl;
    
    // Test version
    const char* version = q_mini_wasm_v2_version();
    assert(version != nullptr);
    std::cout << "  Version: " << version << std::endl;
    
    // Test trit operations via DLL
    int8_t result = trit_add(1, 1);  // 1 + 1 = 2 -> -1 mod 3
    assert(result == -1);
    
    result = trit_multiply(1, -1);  // 1 * -1 = -1
    assert(result == -1);
    
    // Test packing
    int8_t trits[5] = {1, 0, -1, 1, 0};
    uint8_t packed = trit_pack_5(trits);
    int8_t unpacked[5];
    trit_unpack_5(packed, unpacked);
    for (int i = 0; i < 5; ++i) {
        assert(trits[i] == unpacked[i]);
    }
    
    std::cout << "  DLL integration: PASSED" << std::endl;
}

void test_tableau_via_dll() {
    std::cout << "Testing Tableau via DLL..." << std::endl;
    
    // Create tableau
    void* tableau = tableau_create(4);
    assert(tableau != nullptr);
    
    // Check validity
    assert(tableau_is_valid(tableau) == 1);
    
    // Apply gates
    assert(tableau_apply_hadamard(tableau, 0) == 0);
    assert(tableau_apply_phase(tableau, 1) == 0);
    assert(tableau_apply_csum(tableau, 0, 1) == 0);
    
    // Check validity after operations
    assert(tableau_is_valid(tableau) == 1);
    
    // Measure
    int8_t outcomes[4];
    size_t count = tableau_measure_all(tableau, outcomes, 4);
    assert(count == 4);
    
    // Get qutrit count
    assert(tableau_num_qutrits(tableau) == 4);
    
    // Cleanup
    tableau_destroy(tableau);
    
    std::cout << "  Tableau via DLL: PASSED" << std::endl;
}

void test_moe_via_dll() {
    std::cout << "Testing MoE via DLL..." << std::endl;
    
    // Create router
    void* router = moe_router_create(8, 2, 4);
    assert(router != nullptr);
    
    // Route
    int8_t input[4] = {1, 0, -1, 1};
    size_t selected[2];
    size_t count = moe_router_route_topk(router, input, 4, selected, 2);
    assert(count == 2);
    
    // Check capacity
    size_t capacity = moe_router_capacity(router);
    assert(capacity == 28);  // C(8,2) = 28
    
    // Cleanup
    moe_router_destroy(router);
    
    std::cout << "  MoE via DLL: PASSED" << std::endl;
}

void test_ff_via_dll() {
    std::cout << "Testing Forward-Forward via DLL..." << std::endl;
    
    // Create learner
    void* learner = ff_learner_create(2, 8, 0.1);
    assert(learner != nullptr);
    
    // Forward pass
    int8_t input[4] = {1, 0, -1, 1};
    int8_t output[8];
    size_t count = ff_learner_forward(learner, input, 4, output, 8);
    assert(count == 8);
    
    // Compute goodness
    double goodness = ff_learner_goodness(learner, output, 8);
    assert(goodness >= 0.0);
    
    // Cleanup
    ff_learner_destroy(learner);
    
    std::cout << "  Forward-Forward via DLL: PASSED" << std::endl;
}

void test_orchestrator_via_dll() {
    std::cout << "Testing Orchestrator via DLL..." << std::endl;
    
    // Create orchestrator
    void* orchestrator = orchestrator_create(4);
    assert(orchestrator != nullptr);
    
    // Check pending
    assert(orchestrator_has_pending(orchestrator) == 0);
    
    // Wait all (should return immediately)
    orchestrator_wait_all(orchestrator);
    
    // Cleanup
    orchestrator_destroy(orchestrator);
    
    std::cout << "  Orchestrator via DLL: PASSED" << std::endl;
}

void test_full_pipeline() {
    std::cout << "Testing Full Pipeline..." << std::endl;
    
    // 1. Create tableau
    auto tableau = core::stabilizer::create_tableau(8);
    
    // 2. Apply entanglement gates
    tableau->apply_hadamard(0);
    tableau->apply_csum(0, 1);
    tableau->apply_csum(1, 2);
    
    // 3. Create MoE router
    core::moe::ExpertConfig config{16, 4, 8};
    auto router = core::moe::create_moe_router(config);
    
    // 4. Route input
    std::vector<core::ternary::Trit> input = {
        core::ternary::Trit::POSITIVE,
        core::ternary::Trit::ZERO,
        core::ternary::Trit::NEGATIVE,
        core::ternary::Trit::POSITIVE,
        core::ternary::Trit::ZERO,
        core::ternary::Trit::POSITIVE,
        core::ternary::Trit::NEGATIVE,
        core::ternary::Trit::ZERO
    };
    auto experts = router->route_topk(input);
    assert(experts.size() == 4);
    
    // 5. Create learner
    core::learning::FFConfig ff_config{3, 16, 0.01, 1.0, -1.0};
    auto learner = core::learning::create_ff_learner(ff_config);
    
    // 6. Forward pass
    auto output = learner->forward(input);
    assert(output.size() == 16);
    
    // 7. Compute goodness
    double goodness = learner->compute_goodness(output);
    assert(goodness >= 0.0);
    
    // 8. Create orchestrator
    runtime::RuntimeConfig rt_config{4, 100, true, false};
    auto orchestrator = runtime::create_orchestrator(rt_config);
    
    // 9. Submit async task
    auto future = orchestrator->submit_async<double>([&learner, &input]() {
        auto out = learner->forward(input);
        return learner->compute_goodness(out);
    });
    
    double result = future.get();
    assert(result >= 0.0);
    
    orchestrator->wait_all();
    
    std::cout << "  Full pipeline: PASSED" << std::endl;
}

int main() {
    std::cout << "=== q_mini_wasm_v2 Integration Tests ===" << std::endl;
    std::cout << std::endl;
    
    try {
        test_dll_integration();
        test_tableau_via_dll();
        test_moe_via_dll();
        test_ff_via_dll();
        test_orchestrator_via_dll();
        test_full_pipeline();
        
        std::cout << std::endl;
        std::cout << "=== ALL INTEGRATION TESTS PASSED ===" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Integration test failed with exception: " << e.what() << std::endl;
        return 1;
    }
}