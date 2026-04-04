#include "ppo_agent.hpp"
#include <algorithm>
#include <numeric>
#include <cmath>
#include <random>
#include <fstream>
#include <iostream>
#include <string>

namespace q_mini_wasm_v2::core::learning {

PPOAgent::PPOAgent(const PPOConfig& config)
    : config_(config)
    , rng_(std::random_device{}())
{
    initialize_weights();
}

PPOAgent::~PPOAgent() = default;

std::pair<int, double> PPOAgent::select_action(const std::vector<int8_t>& state) {
    auto logits = compute_policy_logits(state);
    auto probs = softmax(logits);
    int action = sample_action(probs);
    double log_prob = compute_log_prob(probs, action);
    return {action, log_prob};
}

std::pair<double, double> PPOAgent::evaluate_action(
    const std::vector<int8_t>& state,
    int action
) const {
    auto logits = compute_policy_logits(state);
    auto probs = softmax(logits);
    double log_prob = compute_log_prob(probs, action);
    
    double entropy = 0.0;
    for (double p : probs) {
        if (p > 1e-8) {
            entropy -= p * std::log(p);
        }
    }
    
    return {log_prob, entropy};
}

double PPOAgent::get_value(const std::vector<int8_t>& state) const {
    double value = 0.0;
    for (size_t i = 0; i < state.size() && i < value_weights_.size(); ++i) {
        value += value_weights_[i] * static_cast<double>(state[i]);
    }
    return value;
}

void PPOAgent::store_experience(const PPOExperience& experience) {
    replay_buffer_.push_back(experience);
    if (replay_buffer_.size() > config_.buffer_size) {
        replay_buffer_.pop_front();
    }
}

PPOStats PPOAgent::update_policy() {
    if (replay_buffer_.size() < config_.batch_size) {
        return stats_;
    }
    
    stats_.num_updates++;
    
    std::vector<double> rewards, values;
    std::vector<bool> dones;
    std::vector<double> old_log_probs;
    
    for (const auto& exp : replay_buffer_) {
        rewards.push_back(exp.reward);
        values.push_back(exp.value);
        dones.push_back(exp.done);
        old_log_probs.push_back(exp.log_prob);
    }
    
    auto advantages = compute_gae(rewards, values, dones);
    
    double mean_adv = std::accumulate(advantages.begin(), advantages.end(), 0.0) / advantages.size();
    double sq_sum = 0.0;
    for (double adv : advantages) {
        sq_sum += (adv - mean_adv) * (adv - mean_adv);
    }
    double std_adv = std::sqrt(sq_sum / advantages.size() + 1e-8);
    
    for (double& adv : advantages) {
        adv = (adv - mean_adv) / std_adv;
    }
    
    double total_policy_loss = 0.0;
    double total_value_loss = 0.0;
    double total_entropy = 0.0;
    
    for (size_t epoch = 0; epoch < config_.num_epochs; ++epoch) {
        std::vector<size_t> indices(replay_buffer_.size());
        std::iota(indices.begin(), indices.end(), 0);
        std::shuffle(indices.begin(), indices.end(), rng_);
        
        for (size_t batch_start = 0; batch_start < indices.size(); batch_start += config_.batch_size) {
            size_t batch_end = std::min(batch_start + config_.batch_size, indices.size());
            
            double batch_policy_loss = 0.0;
            double batch_value_loss = 0.0;
            double batch_entropy = 0.0;
            
            for (size_t i = batch_start; i < batch_end; ++i) {
                size_t idx = indices[i];
                const auto& exp = replay_buffer_[idx];
                
                auto [new_log_prob, entropy] = evaluate_action(exp.state, exp.action);
                double ratio = std::exp(new_log_prob - old_log_probs[idx]);
                double advantage = advantages[idx];
                double policy_loss = -compute_clipped_objective(ratio, advantage);
                
                double current_value = get_value(exp.state);
                double target_value = exp.reward + (exp.done ? 0.0 : config_.gamma * current_value);
                double value_loss = 0.5 * (target_value - current_value) * (target_value - current_value);
                
                batch_policy_loss += policy_loss;
                batch_value_loss += value_loss;
                batch_entropy += entropy;
            }
            
            size_t batch_size = batch_end - batch_start;
            total_policy_loss += batch_policy_loss / batch_size;
            total_value_loss += batch_value_loss / batch_size;
            total_entropy += batch_entropy / batch_size;
        }
    }
    
    stats_.policy_loss = total_policy_loss / config_.num_epochs;
    stats_.value_loss = total_value_loss / config_.num_epochs;
    stats_.entropy = total_entropy / config_.num_epochs;
    stats_.total_loss = stats_.policy_loss + config_.value_loss_coeff * stats_.value_loss - config_.entropy_coeff * stats_.entropy;
    
    replay_buffer_.clear();
    
    return stats_;
}

std::vector<double> PPOAgent::compute_gae(
    const std::vector<double>& rewards,
    const std::vector<double>& values,
    const std::vector<bool>& dones
) const {
    size_t n = rewards.size();
    std::vector<double> advantages(n, 0.0);
    
    double last_gae = 0.0;
    double last_value = 0.0;
    
    for (int i = n - 1; i >= 0; --i) {
        double next_value = (i == n - 1) ? last_value : values[i + 1];
        double next_non_terminal = dones[i] ? 0.0 : 1.0;
        
        double delta = rewards[i] + config_.gamma * next_value * next_non_terminal - values[i];
        last_gae = delta + config_.gamma * config_.lambda * next_non_terminal * last_gae;
        advantages[i] = last_gae;
    }
    
    return advantages;
}

double PPOAgent::compute_clipped_objective(double ratio, double advantage) const {
    double unclipped = ratio * advantage;
    double clipped = std::clamp(ratio, 1.0 - config_.epsilon, 1.0 + config_.epsilon) * advantage;
    return std::min(unclipped, clipped);
}

void PPOAgent::save_model(const std::string& path) const {
    std::ofstream file(path, std::ios::binary);
    if (!file) return;
    
    size_t policy_size = policy_weights_.size();
    file.write(reinterpret_cast<const char*>(&policy_size), sizeof(policy_size));
    file.write(reinterpret_cast<const char*>(policy_weights_.data()), policy_size * sizeof(double));
    
    size_t value_size = value_weights_.size();
    file.write(reinterpret_cast<const char*>(&value_size), sizeof(value_size));
    file.write(reinterpret_cast<const char*>(value_weights_.data()), value_size * sizeof(double));
}

void PPOAgent::load_model(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return;
    
    size_t policy_size;
    file.read(reinterpret_cast<char*>(&policy_size), sizeof(policy_size));
    policy_weights_.resize(policy_size);
    file.read(reinterpret_cast<char*>(policy_weights_.data()), policy_size * sizeof(double));
    
    size_t value_size;
    file.read(reinterpret_cast<char*>(&value_size), sizeof(value_size));
    value_weights_.resize(value_size);
    file.read(reinterpret_cast<char*>(value_weights_.data()), value_size * sizeof(double));
}

void PPOAgent::reset() {
    replay_buffer_.clear();
    stats_ = PPOStats();
    initialize_weights();
}

// ============================================================================
// Private Helper Functions
// ============================================================================

std::vector<double> PPOAgent::compute_policy_logits(const std::vector<int8_t>& state) const {
    size_t num_actions = 8;
    std::vector<double> logits(num_actions, 0.0);
    
    for (size_t a = 0; a < num_actions; ++a) {
        for (size_t i = 0; i < state.size() && i < policy_weights_.size() / num_actions; ++i) {
            size_t weight_idx = a * (policy_weights_.size() / num_actions) + i;
            if (weight_idx < policy_weights_.size()) {
                logits[a] += policy_weights_[weight_idx] * static_cast<double>(state[i]);
            }
        }
    }
    
    return logits;
}

std::vector<double> PPOAgent::softmax(const std::vector<double>& logits) const {
    double max_logit = *std::max_element(logits.begin(), logits.end());
    
    std::vector<double> exp_logits(logits.size());
    double sum = 0.0;
    
    for (size_t i = 0; i < logits.size(); ++i) {
        exp_logits[i] = std::exp(logits[i] - max_logit);
        sum += exp_logits[i];
    }
    
    std::vector<double> probs(logits.size());
    for (size_t i = 0; i < logits.size(); ++i) {
        probs[i] = exp_logits[i] / sum;
    }
    
    return probs;
}

int PPOAgent::sample_action(const std::vector<double>& probs) const {
    std::discrete_distribution<int> dist(probs.begin(), probs.end());
    return dist(rng_);
}

double PPOAgent::compute_log_prob(const std::vector<double>& probs, int action) const {
    return std::log(probs[action] + 1e-8);
}

void PPOAgent::initialize_weights() {
    size_t num_actions = 8;
    size_t state_size = config_.num_qutrits * config_.num_qutrits;
    
    policy_weights_.resize(num_actions * state_size);
    std::normal_distribution<double> dist(0.0, 0.1);
    for (auto& w : policy_weights_) {
        w = dist(rng_);
    }
    
    value_weights_.resize(state_size);
    for (auto& w : value_weights_) {
        w = dist(rng_);
    }
}

double PPOAgent::compute_kl_divergence(
    const std::vector<double>& old_probs,
    const std::vector<double>& new_probs
) const {
    double kl = 0.0;
    for (size_t i = 0; i < old_probs.size(); ++i) {
        if (old_probs[i] > 1e-8 && new_probs[i] > 1e-8) {
            kl += old_probs[i] * std::log(old_probs[i] / new_probs[i]);
        }
    }
    return kl;
}

// ============================================================================
// StabilizerEnvironment Implementation
// ============================================================================

StabilizerEnvironment::StabilizerEnvironment(
    size_t num_qutrits,
    const std::vector<int8_t>& target_state
)
    : num_qutrits_(num_qutrits)
    , target_state_(target_state)
    , step_count_(0)
    , max_steps_(num_qutrits * 4)
{
    tableau_ = stabilizer::create_tableau(num_qutrits);
}

StabilizerEnvironment::~StabilizerEnvironment() = default;

std::vector<int8_t> StabilizerEnvironment::reset() {
    step_count_ = 0;
    tableau_->reset();
    current_state_ = tableau_to_state();
    return current_state_;
}

std::tuple<std::vector<int8_t>, double, bool, std::string> StabilizerEnvironment::step(int action) {
    apply_gate(action);
    current_state_ = tableau_to_state();
    
    double reward = compute_reward(current_state_, target_state_);
    bool done = (step_count_ >= max_steps_) || (reward > 0.95);
    
    std::string info = done ? "Episode finished" : "Step " + std::to_string(step_count_);
    step_count_++;
    
    return {current_state_, reward, done, info};
}

size_t StabilizerEnvironment::get_num_actions() const {
    return 8;
}

std::string StabilizerEnvironment::get_action_name(int action) const {
    switch (action) {
        case 0: return "Hadamard";
        case 1: return "Phase";
        case 2: return "CSUM";
        case 3: return "CZ";
        case 4: return "Pauli X";
        case 5: return "Pauli Y";
        case 6: return "Pauli Z";
        case 7: return "Identity";
        default: return "Unknown";
    }
}

double StabilizerEnvironment::compute_reward(
    const std::vector<int8_t>& current,
    const std::vector<int8_t>& target
) const {
    double distance = jaccard_distance(current, target);
    double reward = 1.0 - distance;
    double gate_penalty = 0.01 * step_count_;
    return reward - gate_penalty;
}

double StabilizerEnvironment::jaccard_distance(
    const std::vector<int8_t>& state_a,
    const std::vector<int8_t>& state_b
) const {
    if (state_a.size() != state_b.size()) return 1.0;
    
    size_t intersection = 0;
    size_t union_count = 0;
    
    for (size_t i = 0; i < state_a.size(); ++i) {
        if (state_a[i] != 0 || state_b[i] != 0) {
            union_count++;
            if (state_a[i] == state_b[i]) {
                intersection++;
            }
        }
    }
    
    if (union_count == 0) return 0.0;
    return 1.0 - static_cast<double>(intersection) / union_count;
}

void StabilizerEnvironment::apply_gate(int action) {
    if (step_count_ >= num_qutrits_) return;
    
    size_t qubit = step_count_ % num_qutrits_;
    
    switch (action) {
        case 0: tableau_->apply_hadamard(qubit); break;
        case 1: tableau_->apply_phase(qubit); break;
        case 2:
            if (qubit + 1 < num_qutrits_) {
                tableau_->apply_csum(qubit, qubit + 1);
            }
            break;
        case 3:
            if (qubit + 1 < num_qutrits_) {
                tableau_->apply_cz(qubit, qubit + 1);
            }
            break;
        case 4: tableau_->apply_pauli_x(qubit); break;
        case 5: tableau_->apply_pauli_y(qubit); break;
        case 6: tableau_->apply_pauli_z(qubit); break;
        default: break;
    }
}

std::vector<int8_t> StabilizerEnvironment::tableau_to_state() const {
    std::vector<int8_t> state;
    size_t n = tableau_->num_qutrits();
    
    for (size_t i = 0; i < 2 * n; ++i) {
        for (size_t j = 0; j < 2 * n; ++j) {
            state.push_back(tableau_->get_element(i, j));
        }
    }
    
    return state;
}

// ============================================================================
// ContinuousLearner Implementation
// ============================================================================

ContinuousLearner::ContinuousLearner(
    const PPOConfig& ppo_config,
    const FFConfig& ff_config
)
    : agent_(ppo_config)
    , ff_config_(ff_config)
{
}

ContinuousLearner::~ContinuousLearner() = default;

std::vector<PPOStats> ContinuousLearner::train(
    size_t num_episodes,
    const std::vector<int8_t>& target_state
) {
    std::vector<PPOStats> all_stats;
    
    StabilizerEnvironment env(agent_.config().num_qutrits, target_state);
    
    for (size_t episode = 0; episode < num_episodes; ++episode) {
        auto stats = run_episode(env);
        all_stats.push_back(stats);
        
        reward_history_.push_back(stats.mean_reward);
        
        if (stats.kl_divergence > agent_.config().early_stop_threshold * 2) {
            adapt_learning_rate(stats.mean_reward);
        }
    }
    
    return all_stats;
}

PPOStats ContinuousLearner::run_episode(StabilizerEnvironment& env) {
    auto state = env.reset();
    bool done = false;
    double total_reward = 0.0;
    size_t steps = 0;
    
    while (!done && steps < agent_.config().horizon) {
        auto [action, log_prob] = agent_.select_action(state);
        auto [next_state, reward, episode_done, info] = env.step(action);
        
        double value = agent_.get_value(state);
        
        PPOExperience exp;
        exp.state = state;
        exp.action = action;
        exp.reward = reward;
        exp.next_state = next_state;
        exp.value = value;
        exp.log_prob = log_prob;
        exp.done = episode_done;
        
        agent_.store_experience(exp);
        
        total_reward += reward;
        state = next_state;
        done = episode_done;
        steps++;
    }
    
    circuit_lengths_.push_back(steps);
    
    PPOStats stats = agent_.update_policy();
    stats.mean_reward = total_reward / steps;
    stats.episode_length = static_cast<double>(steps);
    
    return stats;
}

std::vector<int8_t> ContinuousLearner::optimize_circuit(
    const std::map<std::string, double>& task_description
) {
    size_t n = agent_.config().num_qutrits;
    std::vector<int8_t> target(n * n * 4, 1);
    
    auto stats = train(10, target);
    
    StabilizerEnvironment env(n, target);
    auto final_state = env.reset();
    
    return final_state;
}

void ContinuousLearner::save_circuit(const std::string& path) const {
    agent_.save_model(path);
}

void ContinuousLearner::load_circuit(const std::string& path) {
    agent_.load_model(path);
}

double ContinuousLearner::integrate_learning_signals(
    double ppo_rewards,
    double ff_goodness
) const {
    return 0.7 * ppo_rewards + 0.3 * ff_goodness;
}

void ContinuousLearner::adapt_learning_rate(double performance) {
    if (performance < 0) {
        agent_.set_learning_rate(agent_.config().learning_rate * 0.9);
    }
}

// ============================================================================
// Factory Functions
// ============================================================================

std::unique_ptr<PPOAgent> create_ppo_agent(const PPOConfig& config) {
    return std::make_unique<PPOAgent>(config);
}

std::unique_ptr<StabilizerEnvironment> create_stabilizer_environment(
    size_t num_qutrits,
    const std::vector<int8_t>& target_state
) {
    return std::make_unique<StabilizerEnvironment>(num_qutrits, target_state);
}

std::unique_ptr<ContinuousLearner> create_continuous_learner(
    const PPOConfig& ppo_config,
    const FFConfig& ff_config
) {
    return std::make_unique<ContinuousLearner>(ppo_config, ff_config);
}

} // namespace q_mini_wasm_v2::core::learning