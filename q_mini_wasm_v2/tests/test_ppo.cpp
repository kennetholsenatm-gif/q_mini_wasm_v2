#include <iostream>
#include <cassert>
#include <vector>
#include "../core/learning/ppo_agent.hpp"
#include "../core/learning/forward_forward.hpp"

using namespace q_mini_wasm_v2;

void test_ppo_config() {
    std::cout << "Testing PPO Configuration..." << std::endl;
    
    core::learning::PPOConfig config;
    
    assert(config.learning_rate > 0);
    assert(config.gamma > 0 && config.gamma < 1);
    assert(config.epsilon > 0);
    assert(config.num_qutrits > 0);
    
    std::cout << "  PPO Configuration: PASSED" << std::endl;
}

void test_ppo_agent() {
    std::cout << "Testing PPO Agent..." << std::endl;
    
    using namespace core::learning;
    
    PPOConfig config;
    config.num_qutrits = 4;
    config.horizon = 32;
    config.buffer_size = 100;
    config.batch_size = 16;
    
    auto agent = create_ppo_agent(config);
    
    // Test action selection
    std::vector<int8_t> state = {0, 1, 0, -1, 1, 0, -1, 0, 1, 0, -1, 0, 0, 1, 0, -1};
    auto [action, log_prob] = agent->select_action(state);
    
    assert(action >= 0 && action < 8);
    assert(log_prob < 0);  // Log probability should be negative
    
    // Test value estimation
    double value = agent->get_value(state);
    assert(value == value);  // Check for NaN
    
    // Test experience storage
    PPOExperience exp;
    exp.state = state;
    exp.action = action;
    exp.reward = 1.0;
    exp.next_state = state;
    exp.value = value;
    exp.log_prob = log_prob;
    exp.done = false;
    
    agent->store_experience(exp);
    
    std::cout << "  PPO Agent: PASSED" << std::endl;
}

void test_stabilizer_environment() {
    std::cout << "Testing Stabilizer Environment..." << std::endl;
    
    using namespace core::learning;
    
    size_t num_qutrits = 4;
    std::vector<int8_t> target(num_qutrits * num_qutrits * 4, 1);
    
    auto env = create_stabilizer_environment(num_qutrits, target);
    
    // Test reset
    auto state = env->reset();
    assert(state.size() == 2 * num_qutrits * 2 * num_qutrits);
    
    // Test step
    auto [next_state, reward, done, info] = env->step(0);  // Apply Hadamard
    assert(next_state.size() == state.size());
    assert(reward >= -1.0 && reward <= 1.0);
    
    // Test action names
    assert(env->get_action_name(0) == "Hadamard");
    assert(env->get_action_name(1) == "Phase");
    assert(env->get_action_name(7) == "Identity");
    
    std::cout << "  Stabilizer Environment: PASSED" << std::endl;
}

void test_continuous_learner() {
    std::cout << "Testing Continuous Learner..." << std::endl;
    
    using namespace core::learning;
    
    PPOConfig ppo_config;
    ppo_config.num_qutrits = 4;
    ppo_config.horizon = 16;
    ppo_config.buffer_size = 50;
    ppo_config.batch_size = 10;
    ppo_config.num_epochs = 2;
    
    FFConfig ff_config{2, 8, 0.1, 1.0, -1.0};
    
    auto learner = create_continuous_learner(ppo_config, ff_config);
    
    // Test training
    std::vector<int8_t> target(ppo_config.num_qutrits * ppo_config.num_qutrits * 4, 1);
    auto stats = learner->train(5, target);
    
    assert(stats.size() == 5);
    
    // Test circuit optimization
    std::map<std::string, double> task = {{"priority", 1.0}};
    auto circuit = learner->optimize_circuit(task);
    
    assert(!circuit.empty());
    
    std::cout << "  Continuous Learner: PASSED" << std::endl;
}

void test_integration_with_forward_forward() {
    std::cout << "Testing Integration with Forward-Forward..." << std::endl;
    
    using namespace core::learning;
    
    // Create Forward-Forward learner
    FFConfig ff_config{2, 8, 0.1, 1.0, -1.0};
    auto ff_learner = create_ff_learner(ff_config);
    
    // Create PPO agent
    PPOConfig ppo_config;
    ppo_config.num_qutrits = 4;
    
    auto ppo_agent = create_ppo_agent(ppo_config);
    
    // Test that both systems can coexist
    std::vector<std::vector<core::ternary::Trit>> positive_data = {
        {core::ternary::Trit::POSITIVE, core::ternary::Trit::ZERO, 
         core::ternary::Trit::NEGATIVE, core::ternary::Trit::POSITIVE}
    };
    
    auto negative_data = ff_learner->generate_negative_samples(positive_data);
    auto goodness = ff_learner->train_layer(0, positive_data, negative_data);
    
    assert(goodness.delta != 0.0);
    
    std::vector<int8_t> ppo_state = {0, 1, 0, -1, 1, 0, -1, 0, 1, 0, -1, 0, 0, 1, 0, -1};
    auto [action, log_prob] = ppo_agent->select_action(ppo_state);
    
    assert(action >= 0 && action < 8);
    
    std::cout << "  Integration: PASSED" << std::endl;
}

int main() {
    std::cout << "=== PPO Reinforcement Learning Test Suite ===" << std::endl;
    std::cout << std::endl;
    
    try {
        test_ppo_config();
        test_ppo_agent();
        test_stabilizer_environment();
        test_continuous_learner();
        test_integration_with_forward_forward();
        
        std::cout << std::endl;
        std::cout << "=== ALL PPO TESTS PASSED ===" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
}