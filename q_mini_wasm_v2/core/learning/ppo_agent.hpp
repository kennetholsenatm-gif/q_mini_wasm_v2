#pragma once

#include <vector>
#include <cstdint>
#include <memory>
#include <functional>
#include <random>
#include <deque>
#include "../stabilizer/tableau.hpp"
#include "../ternary/trit.hpp"
#include "forward_forward.hpp"
#include <map>
#include <string>

namespace q_mini_wasm_v2::core::learning {

struct PPOConfig {
    double learning_rate = 0.0003;
    double value_learning_rate = 0.001;
    double gamma = 0.99;
    double lambda = 0.95;
    double epsilon = 0.2;
    double entropy_coeff = 0.01;
    double value_loss_coeff = 0.5;
    double max_grad_norm = 0.5;
    size_t num_epochs = 4;
    size_t batch_size = 64;
    size_t buffer_size = 2048;
    size_t horizon = 128;
    size_t num_qutrits = 8;
    size_t max_circuit_depth = 16;
    double reward_scale = 1.0;
    bool use_mixed_precision = false;
    size_t update_frequency = 1;
    double early_stop_threshold = 0.01;
};

struct PPOExperience {
    std::vector<int8_t> state;
    int action;
    double reward;
    std::vector<int8_t> next_state;
    double value;
    double log_prob;
    bool done;
};

struct PPOStats {
    double policy_loss = 0.0;
    double value_loss = 0.0;
    double entropy = 0.0;
    double total_loss = 0.0;
    double mean_reward = 0.0;
    double episode_length = 0.0;
    double kl_divergence = 0.0;
    size_t num_updates = 0;
};

class PPOAgent {
public:
    explicit PPOAgent(const PPOConfig& config);
    ~PPOAgent();
    
    std::pair<int, double> select_action(const std::vector<int8_t>& state);
    std::pair<double, double> evaluate_action(const std::vector<int8_t>& state, int action) const;
    double get_value(const std::vector<int8_t>& state) const;
    void store_experience(const PPOExperience& experience);
    PPOStats update_policy();
    std::vector<double> compute_gae(const std::vector<double>& rewards, const std::vector<double>& values, const std::vector<bool>& dones) const;
    double compute_clipped_objective(double ratio, double advantage) const;
    void save_model(const std::string& path) const;
    void load_model(const std::string& path);
    void reset();
    void set_learning_rate(double lr) { config_.learning_rate = lr; }
    const PPOConfig& config() const { return config_; }
    const PPOStats& get_stats() const { return stats_; }
    
private:
    PPOConfig config_;
    PPOStats stats_;
    std::vector<double> policy_weights_;
    std::vector<double> value_weights_;
    std::deque<PPOExperience> replay_buffer_;
    mutable std::mt19937 rng_;
    
    std::vector<double> compute_policy_logits(const std::vector<int8_t>& state) const;
    std::vector<double> softmax(const std::vector<double>& logits) const;
    int sample_action(const std::vector<double>& probs) const;
    double compute_log_prob(const std::vector<double>& probs, int action) const;
    void initialize_weights();
    double compute_kl_divergence(const std::vector<double>& old_probs, const std::vector<double>& new_probs) const;
};

class StabilizerEnvironment {
public:
    StabilizerEnvironment(size_t num_qutrits, const std::vector<int8_t>& target_state);
    ~StabilizerEnvironment();
    
    std::vector<int8_t> reset();
    std::tuple<std::vector<int8_t>, double, bool, std::string> step(int action);
    const std::vector<int8_t>& get_state() const { return current_state_; }
    const std::vector<int8_t>& get_target() const { return target_state_; }
    size_t get_num_actions() const;
    std::string get_action_name(int action) const;
    
private:
    size_t num_qutrits_;
    std::vector<int8_t> target_state_;
    std::vector<int8_t> current_state_;
    size_t step_count_;
    size_t max_steps_;
    std::unique_ptr<stabilizer::StabilizerTableau> tableau_;
    
    double compute_reward(const std::vector<int8_t>& current, const std::vector<int8_t>& target) const;
    double jaccard_distance(const std::vector<int8_t>& state_a, const std::vector<int8_t>& state_b) const;
    void apply_gate(int action);
    std::vector<int8_t> tableau_to_state() const;
};

class ContinuousLearner {
public:
    ContinuousLearner(const PPOConfig& ppo_config, const FFConfig& ff_config);
    ~ContinuousLearner();
    
    std::vector<PPOStats> train(size_t num_episodes, const std::vector<int8_t>& target_state);
    PPOStats run_episode(StabilizerEnvironment& env);
    std::vector<int8_t> optimize_circuit(const std::map<std::string, double>& task_description);
    const PPOAgent& get_agent() const { return agent_; }
    void save_circuit(const std::string& path) const;
    void load_circuit(const std::string& path);
    
private:
    PPOAgent agent_;
    FFConfig ff_config_;
    std::deque<PPOExperience> continuous_buffer_;
    std::vector<double> reward_history_;
    std::vector<size_t> circuit_lengths_;
    
    double integrate_learning_signals(double ppo_rewards, double ff_goodness) const;
    void adapt_learning_rate(double performance);
};

std::unique_ptr<PPOAgent> create_ppo_agent(const PPOConfig& config);
std::unique_ptr<StabilizerEnvironment> create_stabilizer_environment(size_t num_qutrits, const std::vector<int8_t>& target_state);
std::unique_ptr<ContinuousLearner> create_continuous_learner(const PPOConfig& ppo_config, const FFConfig& ff_config);

} // namespace q_mini_wasm_v2::core::learning