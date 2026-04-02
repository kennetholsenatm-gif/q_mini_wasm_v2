#include <iostream>
#include <cassert>
#include <vector>

#include "../core/ternary/trit.hpp"
#include "../core/stabilizer/tableau.hpp"
#include "../core/moe/router.hpp"
#include "../core/learning/forward_forward.hpp"
#include "../runtime/orchestrator.hpp"

using namespace q_mini_wasm_v2;

void test_trit_operations() {
    std::cout << "Testing Trit Operations..." << std::endl;
    
    using namespace core::ternary;
    
    // Test basic operations
    assert(trit_ops::add(Trit::POSITIVE, Trit::POSITIVE) == Trit::NEGATIVE);  // 1+1=2 -> -1 mod 3
    assert(trit_ops::add(Trit::POSITIVE, Trit::ZERO) == Trit::POSITIVE);
    assert(trit_ops::add(Trit::NEGATIVE, Trit::POSITIVE) == Trit::ZERO);
    
    // Test multiplication
    assert(trit_ops::multiply(Trit::POSITIVE, Trit::NEGATIVE) == Trit::NEGATIVE);
    assert(trit_ops::multiply(Trit::ZERO, Trit::POSITIVE) == Trit::ZERO);
    
    // Test packing
    TritBlock5 block;
    block.trits[0] = Trit::POSITIVE;
    block.trits[1] = Trit::ZERO;
    block.trits[2] = Trit::NEGATIVE;
    block.trits[3] = Trit::POSITIVE;
    block.trits[4] = Trit::ZERO;
    
    uint8_t packed = block.pack();
    auto unpacked = TritBlock5::unpack(packed);
    
    for (int i = 0; i < 5; ++i) {
        assert(block.trits[i] == unpacked.trits[i]);
    }
    
    std::cout << "  Trit operations: PASSED" << std::endl;
}

void test_stabilizer_tableau() {
    std::cout << "Testing Stabilizer Tableau..." << std::endl;
    
    using namespace core::stabilizer;
    
    auto tableau = create_tableau(4);
    
    // Test initial state is valid
    assert(tableau->is_valid());
    
    // Test Hadamard gate
    tableau->apply_hadamard(0);
    assert(tableau->is_valid());
    
    // Test Phase gate
    tableau->apply_phase(1);
    assert(tableau->is_valid());
    
    // Test CSUM gate
    tableau->apply_csum(0, 1);
    assert(tableau->is_valid());
    
    // Test measurement
    auto outcomes = tableau->measure_all();
    assert(outcomes.size() == 4);
    
    std::cout << "  Stabilizer tableau: PASSED" << std::endl;
}

void test_moe_router() {
    std::cout << "Testing MoE Router..." << std::endl;
    
    using namespace core::moe;
    using namespace core::ternary;
    
    ExpertConfig config{8, 2, 4};
    auto router = create_moe_router(config);
    
    // Test routing
    std::vector<Trit> input = {Trit::POSITIVE, Trit::ZERO, Trit::NEGATIVE, Trit::POSITIVE};
    auto selected = router->route_topk(input);
    
    assert(selected.size() == 2);
    for (auto idx : selected) {
        assert(idx < 8);
    }
    
    // Test hypersimplex capacity
    size_t capacity = router->compute_hypersimplex_capacity();
    assert(capacity == 28);  // C(8,2) = 28
    
    std::cout << "  MoE router: PASSED" << std::endl;
}

void test_forward_forward() {
    std::cout << "Testing Forward-Forward Learning..." << std::endl;
    
    using namespace core::learning;
    using namespace core::ternary;
    
    FFConfig config{2, 8, 0.1, 1.0, -1.0};
    auto learner = create_ff_learner(config);
    
    // Create sample data
    std::vector<std::vector<Trit>> positive_data = {
        {Trit::POSITIVE, Trit::POSITIVE, Trit::ZERO, Trit::NEGATIVE},
        {Trit::ZERO, Trit::POSITIVE, Trit::POSITIVE, Trit::ZERO}
    };
    
    auto negative_data = learner->generate_negative_samples(positive_data);
    assert(negative_data.size() == positive_data.size());
    
    // Test forward pass
    std::vector<Trit> input = {Trit::POSITIVE, Trit::ZERO, Trit::NEGATIVE, Trit::POSITIVE};
    auto output = learner->forward(input);
    assert(output.size() == 8);
    
    // Test goodness computation
    double goodness = learner->compute_goodness(output);
    assert(goodness >= 0.0);
    
    std::cout << "  Forward-Forward learning: PASSED" << std::endl;
}

void test_runtime_orchestrator() {
    std::cout << "Testing Runtime Orchestrator..." << std::endl;
    
    using namespace runtime;
    
    RuntimeConfig config{4, 100, true, false};
    auto orchestrator = create_orchestrator(config);
    
    // Test task submission
    int counter = 0;
    auto future = orchestrator->submit_async<int>([&counter]() {
        ++counter;
        return counter;
    });
    
    int result = future.get();
    assert(result == 1);
    assert(counter == 1);
    
    // Test wait_all
    orchestrator->wait_all();
    assert(!orchestrator->has_pending_tasks());
    
    std::cout << "  Runtime orchestrator: PASSED" << std::endl;
}

int main() {
    std::cout << "=== q_mini_wasm_v2 Test Suite ===" << std::endl;
    std::cout << std::endl;
    
    try {
        test_trit_operations();
        test_stabilizer_tableau();
        test_moe_router();
        test_forward_forward();
        test_runtime_orchestrator();
        
        std::cout << std::endl;
        std::cout << "=== ALL TESTS PASSED ===" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
}