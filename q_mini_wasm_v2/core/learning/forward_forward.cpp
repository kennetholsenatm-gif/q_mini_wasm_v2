#include "forward_forward.hpp"
#include <algorithm>
#include <numeric>
#include <cmath>
#include <random>

namespace q_mini_wasm_v2::core::learning {

ForwardForwardLearner::ForwardForwardLearner(const FFConfig& config)
    : config_(config)
    , layer_weights_(config.num_layers)
    , layer_biases_(config.num_layers)
{
    initialize_weights();
}

ForwardForwardLearner::~ForwardForwardLearner() = default;

// ============================================================================
// Training Operations
// ============================================================================

LayerGoodness ForwardForwardLearner::train_layer(
    size_t layer_idx,
    const std::vector<std::vector<ternary::Trit>>& positive_data,
    const std::vector<std::vector<ternary::Trit>>& negative_data
) {
    if (layer_idx >= config_.num_layers) {
        throw std::out_of_range("Layer index out of range");
    }
    
    LayerGoodness goodness{0.0, 0.0, 0.0};
    
    // Compute goodness for positive data
    double pos_sum = 0.0;
    for (const auto& sample : positive_data) {
        auto activations = forward_layer(layer_idx, sample);
        pos_sum += compute_goodness(activations);
    }
    goodness.positive_goodness = pos_sum / positive_data.size();
    
    // Compute goodness for negative data
    double neg_sum = 0.0;
    for (const auto& sample : negative_data) {
        auto activations = forward_layer(layer_idx, sample);
        neg_sum += compute_goodness(activations);
    }
    goodness.negative_goodness = neg_sum / negative_data.size();
    
    // Compute delta (positive should be higher than negative)
    goodness.delta = goodness.positive_goodness - goodness.negative_goodness;
    
    // Update weights if positive goodness exceeds negative
    if (goodness.delta > 0) {
        // Reinforce weights that contributed to positive goodness
        for (size_t i = 0; i < positive_data.size(); ++i) {
            update_weights_hebbian(layer_idx, positive_data[i], goodness.delta);
        }
    } else {
        // Weaken weights that led to negative goodness
        for (size_t i = 0; i < negative_data.size(); ++i) {
            update_weights_hebbian(layer_idx, negative_data[i], goodness.delta);
        }
    }
    
    return goodness;
}

std::vector<std::vector<ternary::Trit>> ForwardForwardLearner::generate_negative_samples(
    const std::vector<std::vector<ternary::Trit>>& positive_data
) {
    std::vector<std::vector<ternary::Trit>> negative_samples;
    negative_samples.reserve(positive_data.size());
    
    static std::mt19937 rng(std::random_device{}());
    
    for (const auto& sample : positive_data) {
        std::vector<ternary::Trit> corrupted = sample;
        
        // Corrupt random elements
        std::uniform_int_distribution<size_t> pos_dist(0, sample.size() - 1);
        std::uniform_int_distribution<int> val_dist(-1, 1);
        
        size_t num_corruptions = std::max(size_t(1), sample.size() / 10);
        for (size_t i = 0; i < num_corruptions; ++i) {
            size_t pos = pos_dist(rng);
            corrupted[pos] = static_cast<ternary::Trit>(val_dist(rng));
        }
        
        negative_samples.push_back(corrupted);
    }
    
    return negative_samples;
}

double ForwardForwardLearner::compute_goodness(const std::vector<ternary::Trit>& activations) const {
    // Goodness = sum of squared activations (tropical inner product with itself)
    double goodness = 0.0;
    for (const auto& act : activations) {
        double val = static_cast<double>(act);
        goodness += val * val;
    }
    return goodness;
}

// ============================================================================
// Forward Pass
// ============================================================================

std::vector<ternary::Trit> ForwardForwardLearner::forward(const std::vector<ternary::Trit>& input) {
    std::vector<ternary::Trit> current = input;
    
    for (size_t layer = 0; layer < config_.num_layers; ++layer) {
        current = forward_layer(layer, current);
    }
    
    return current;
}

std::vector<ternary::Trit> ForwardForwardLearner::forward_layer(
    size_t layer_idx,
    const std::vector<ternary::Trit>& input
) {
    if (layer_idx >= config_.num_layers) {
        throw std::out_of_range("Layer index out of range");
    }
    
    const auto& weights = layer_weights_[layer_idx];
    const auto& biases = layer_biases_[layer_idx];
    
    size_t output_size = weights.size();
    std::vector<ternary::Trit> output(output_size);
    
    for (size_t o = 0; o < output_size; ++o) {
        // Compute weighted sum using ternary arithmetic
        int sum = static_cast<int>(biases[o]);
        
        for (size_t i = 0; i < input.size() && i < weights[o].size(); ++i) {
            // Ternary multiplication: result is -1, 0, or +1
            int product = static_cast<int>(input[i]) * static_cast<int>(weights[o][i]);
            sum += product;
        }
        
        // Apply ternary activation
        output[o] = ternary_activation(sum);
    }
    
    return output;
}

std::vector<ternary::Trit> ForwardForwardLearner::entangled_forward(
    stabilizer::StabilizerTableau& tableau,
    const std::vector<ternary::Trit>& input
) {
    // Apply entanglement gates based on input
    for (size_t i = 0; i < std::min(input.size(), tableau.num_qutrits()); ++i) {
        if (input[i] == ternary::Trit::POSITIVE) {
            tableau.apply_hadamard(i);
        } else if (input[i] == ternary::Trit::NEGATIVE) {
            tableau.apply_phase(i);
        }
    }
    
    // Apply CSUM gates for entanglement
    for (size_t i = 0; i + 1 < tableau.num_qutrits(); ++i) {
        tableau.apply_csum(i, i + 1);
    }
    
    // Measure to get entangled activations
    auto measurements = tableau.measure_all();
    
    // Convert measurements to ternary activations
    std::vector<ternary::Trit> activations(measurements.size());
    for (size_t i = 0; i < measurements.size(); ++i) {
        // Map GF(3) {0,1,2} to ternary {-1,0,1}
        activations[i] = static_cast<ternary::Trit>(measurements[i] - 1);
    }
    
    return activations;
}

// ============================================================================
// Weight Management
// ============================================================================

const std::vector<std::vector<ternary::Trit>>& ForwardForwardLearner::get_weights(size_t layer_idx) const {
    if (layer_idx >= config_.num_layers) {
        throw std::out_of_range("Layer index out of range");
    }
    return layer_weights_[layer_idx];
}

void ForwardForwardLearner::update_weights_hebbian(
    size_t layer_idx,
    const std::vector<ternary::Trit>& activations,
    double delta
) {
    if (layer_idx >= config_.num_layers) {
        throw std::out_of_range("Layer index out of range");
    }
    
    auto& weights = layer_weights_[layer_idx];
    
    // Hebbian learning: Δw = η * δ * pre * post
    // For ternary, we clip updates to {-1, 0, +1}
    
    double update_magnitude = config_.learning_rate * delta;
    
    for (size_t o = 0; o < weights.size(); ++o) {
        for (size_t i = 0; i < weights[o].size() && i < activations.size(); ++i) {
            // Compute weight update
            int pre = static_cast<int>(activations[i]);
            int current_w = static_cast<int>(weights[o][i]);
            
            // Hebbian update with ternary clipping
            int update = static_cast<int>(std::round(update_magnitude * pre));
            int new_w = current_w + update;
            
            // Clip to ternary range
            weights[o][i] = clip_to_ternary(new_w);
        }
    }
}

void ForwardForwardLearner::reset_weights() {
    initialize_weights();
}

// ============================================================================
// Internal Helpers
// ============================================================================

void ForwardForwardLearner::initialize_weights() {
    static std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(-1, 1);
    
    for (size_t layer = 0; layer < config_.num_layers; ++layer) {
        size_t input_size = (layer == 0) ? config_.neurons_per_layer : config_.neurons_per_layer;
        size_t output_size = config_.neurons_per_layer;
        
        layer_weights_[layer].resize(output_size, std::vector<ternary::Trit>(input_size));
        layer_biases_[layer].resize(output_size, ternary::Trit::ZERO);
        
        for (auto& row : layer_weights_[layer]) {
            for (auto& w : row) {
                w = static_cast<ternary::Trit>(dist(rng));
            }
        }
        
        for (auto& bias : layer_biases_[layer]) {
            bias = static_cast<ternary::Trit>(dist(rng));
        }
    }
}

ternary::Trit ForwardForwardLearner::ternary_activation(double x) {
    // Ternary activation: sign function with zero threshold
    if (x > 0.5) return ternary::Trit::POSITIVE;
    if (x < -0.5) return ternary::Trit::NEGATIVE;
    return ternary::Trit::ZERO;
}

ternary::Trit ForwardForwardLearner::compute_weight_update(
    ternary::Trit pre_synaptic,
    ternary::Trit post_synaptic
) {
    // Hebbian rule: Δw = pre * post
    int pre = static_cast<int>(pre_synaptic);
    int post = static_cast<int>(post_synaptic);
    int update = pre * post;
    
    return clip_to_ternary(update);
}

ternary::Trit ForwardForwardLearner::clip_to_ternary(int value) {
    if (value > 1) return ternary::Trit::POSITIVE;
    if (value < -1) return ternary::Trit::NEGATIVE;
    return static_cast<ternary::Trit>(value);
}

std::unique_ptr<ForwardForwardLearner> create_ff_learner(const FFConfig& config) {
    return std::make_unique<ForwardForwardLearner>(config);
}

} // namespace q_mini_wasm_v2::core::learning