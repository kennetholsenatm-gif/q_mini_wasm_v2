#include "forward_forward.hpp"
#include "entropy_goodness.hpp"
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
    
    LayerGoodness goodness{0, 0, 0};
    
    // Compute goodness for positive data
    uint64_t pos_sum = 0;
    for (const auto& sample : positive_data) {
        auto activations = forward_layer(layer_idx, sample);
        pos_sum += compute_goodness(activations);
    }
    goodness.positive_goodness = positive_data.empty() ? 0 : static_cast<uint32_t>(pos_sum / positive_data.size());
    
    // Compute goodness for negative data
    uint64_t neg_sum = 0;
    for (const auto& sample : negative_data) {
        auto activations = forward_layer(layer_idx, sample);
        neg_sum += compute_goodness(activations);
    }
    goodness.negative_goodness = negative_data.empty() ? 0 : static_cast<uint32_t>(neg_sum / negative_data.size());
    
    // Compute delta (positive should be higher than negative)
    goodness.delta = static_cast<int32_t>(goodness.positive_goodness) - static_cast<int32_t>(goodness.negative_goodness);
    
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
        
        // High corruption (50%) for uniform data like dictionaries
        size_t num_corruptions = std::max(size_t(1), sample.size() / 2);
        for (size_t i = 0; i < num_corruptions; ++i) {
            size_t pos = pos_dist(rng);
            corrupted[pos] = static_cast<ternary::Trit>(val_dist(rng));
        }
        
        // Permute the corrupted sample to break local uniformity
        std::vector<ternary::Trit> permuted(corrupted.size());
        for (size_t i = 0; i < corrupted.size(); ++i) {
            size_t permuted_pos = (i * 37 + 17) % corrupted.size();  // Simple permutation
            permuted[permuted_pos] = corrupted[i];
        }
        
        negative_samples.push_back(permuted);
    }
    
    return negative_samples;
}

uint32_t ForwardForwardLearner::compute_goodness(const std::vector<ternary::Trit>& activations) const {
    // Layer-normalized goodness: how many standard deviations above mean activation
    // This prevents saturation - even small differences matter
    uint32_t active_count = 0;
    for (const auto& act : activations) {
        if (act != ternary::Trit::ZERO) {
            active_count++;
        }
    }
    
    // Normalize by layer size (128 neurons) and add resolution for small differences
    // Return 0-255 range instead of 0-128 to give more granularity
    uint32_t normalized = (active_count * 200) / std::max(size_t(1), activations.size());
    return normalized;
}

uint32_t ForwardForwardLearner::compute_entangled_goodness(const stabilizer::StabilizerTableau& tableau) const {
    EntropyGoodnessMetric metric(tableau.num_qutrits());
    return static_cast<uint32_t>(metric.compute_goodness(tableau));
}

LayerGoodness ForwardForwardLearner::train_layer_entangled(
    size_t layer_idx,
    const std::vector<std::vector<ternary::Trit>>& positive_data,
    const std::vector<std::vector<ternary::Trit>>& negative_data
) {
    if (layer_idx >= config_.num_layers) {
        throw std::out_of_range("Layer index out of range");
    }
    
    LayerGoodness goodness{0, 0, 0};
    
    // Compute goodness for positive data
    uint64_t pos_sum = 0;
    for (const auto& sample : positive_data) {
        auto tableau = stabilizer::create_tableau(std::max(sample.size(), config_.neurons_per_layer));
        auto activations = entangled_forward(*tableau, sample);
        pos_sum += compute_goodness(activations);
    }
    goodness.positive_goodness = positive_data.empty() ? 0 : static_cast<uint32_t>(pos_sum / positive_data.size());
    
    // Compute goodness for negative data
    uint64_t neg_sum = 0;
    for (const auto& sample : negative_data) {
        auto tableau = stabilizer::create_tableau(std::max(sample.size(), config_.neurons_per_layer));
        auto activations = entangled_forward(*tableau, sample);
        neg_sum += compute_goodness(activations);
    }
    goodness.negative_goodness = negative_data.empty() ? 0 : static_cast<uint32_t>(neg_sum / negative_data.size());
    
    goodness.delta = static_cast<int32_t>(goodness.positive_goodness) - static_cast<int32_t>(goodness.negative_goodness);
    
    // Update weights if positive goodness exceeds negative
    if (goodness.delta > 0) {
        for (size_t i = 0; i < positive_data.size(); ++i) {
            auto tableau = stabilizer::create_tableau(std::max(positive_data[i].size(), config_.neurons_per_layer));
            auto activations = entangled_forward(*tableau, positive_data[i]);
            update_weights_hebbian(layer_idx, activations, goodness.delta);
        }
    } else {
        for (size_t i = 0; i < negative_data.size(); ++i) {
            auto tableau = stabilizer::create_tableau(std::max(negative_data[i].size(), config_.neurons_per_layer));
            auto activations = entangled_forward(*tableau, negative_data[i]);
            update_weights_hebbian(layer_idx, activations, goodness.delta);
        }
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
    int32_t delta
) {
    if (layer_idx >= config_.num_layers) {
        throw std::out_of_range("Layer index out of range");
    }
    
    auto& weights = layer_weights_[layer_idx];
    
    // Hebbian learning: Δw = η * δ * pre * post
    // magnitude shifted by learning_rate_shift to avoid float
    int32_t update_magnitude = delta >> config_.learning_rate_shift;
    
    if (update_magnitude == 0) return;
    
    int32_t direction = (update_magnitude > 0) ? 1 : -1;
    
    for (size_t o = 0; o < weights.size(); ++o) {
        for (size_t i = 0; i < weights[o].size() && i < activations.size(); ++i) {
            // Compute weight update
            int32_t pre = static_cast<int32_t>(activations[i]);
            int32_t current_w = static_cast<int32_t>(weights[o][i]);
            
            // Hebbian update with ternary clipping
            int32_t update = direction * pre;
            int32_t new_w = current_w + update;
            
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
    // Initialize with more zeros to reduce saturation
    std::discrete_distribution<int> dist({40, 20, 40}); // -1: 40%, 0: 20%, +1: 40%
    
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

ternary::Trit ForwardForwardLearner::ternary_activation(int32_t x) {
    // Threshold activation with noise to break symmetry
    // Require stronger signal to activate (threshold = 2 instead of 0)
    static thread_local std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> noise(-1, 1);
    
    int32_t threshold = 2;  // Higher threshold = sparser activations
    x += noise(rng);  // Add jitter to break ties
    
    if (x > threshold) return ternary::Trit::POSITIVE;
    if (x < -threshold) return ternary::Trit::NEGATIVE;
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

// ============================================================================
// Serialization
// ============================================================================

std::vector<uint8_t> ForwardForwardLearner::serialize_weights() const {
    std::vector<uint8_t> data;
    
    // Header: num_layers (1 byte)
    data.push_back(static_cast<uint8_t>(config_.num_layers));
    
    // For each layer: [output_size][input_size][weights...][biases...]
    for (size_t layer = 0; layer < config_.num_layers; ++layer) {
        const auto& weights = layer_weights_[layer];
        const auto& biases = layer_biases_[layer];
        
        if (weights.empty()) continue;
        
        size_t output_size = weights.size();
        size_t input_size = weights[0].size();
        
        // Layer dimensions (2 bytes each, little-endian)
        data.push_back(static_cast<uint8_t>(output_size & 0xFF));
        data.push_back(static_cast<uint8_t>((output_size >> 8) & 0xFF));
        data.push_back(static_cast<uint8_t>(input_size & 0xFF));
        data.push_back(static_cast<uint8_t>((input_size >> 8) & 0xFF));
        
        // Weights: each stored as int8_t {-1, 0, 1}
        for (const auto& row : weights) {
            for (const auto& w : row) {
                data.push_back(static_cast<int8_t>(w));
            }
        }
        
        // Biases: each stored as int8_t {-1, 0, 1}
        for (const auto& b : biases) {
            data.push_back(static_cast<int8_t>(b));
        }
    }
    
    return data;
}

bool ForwardForwardLearner::deserialize_weights(const std::vector<uint8_t>& data) {
    if (data.empty()) return false;
    
    size_t pos = 0;
    
    // Read num_layers
    if (pos >= data.size()) return false;
    uint8_t num_layers = data[pos++];
    
    if (num_layers != config_.num_layers) {
        // Layer count mismatch - still try to load if compatible
        if (num_layers > config_.num_layers) {
            return false; // Can't load more layers than configured
        }
    }
    
    // Resize weight structures
    layer_weights_.resize(config_.num_layers);
    layer_biases_.resize(config_.num_layers);
    
    for (size_t layer = 0; layer < num_layers; ++layer) {
        // Read dimensions
        if (pos + 4 > data.size()) return false;
        
        uint16_t output_size = data[pos] | (data[pos + 1] << 8);
        uint16_t input_size = data[pos + 2] | (data[pos + 3] << 8);
        pos += 4;
        
        // Validate dimensions match config
        if (output_size != config_.neurons_per_layer) {
            return false;
        }
        
        // Resize weights and biases for this layer
        layer_weights_[layer].resize(output_size, std::vector<ternary::Trit>(input_size));
        layer_biases_[layer].resize(output_size);
        
        // Read weights
        size_t num_weights = output_size * input_size;
        if (pos + num_weights > data.size()) return false;
        
        for (size_t o = 0; o < output_size; ++o) {
            for (size_t i = 0; i < input_size; ++i) {
                int8_t val = static_cast<int8_t>(data[pos++]);
                // Validate ternary value
                if (val < -1 || val > 1) val = 0;
                layer_weights_[layer][o][i] = static_cast<ternary::Trit>(val);
            }
        }
        
        // Read biases
        if (pos + output_size > data.size()) return false;
        
        for (size_t o = 0; o < output_size; ++o) {
            int8_t val = static_cast<int8_t>(data[pos++]);
            // Validate ternary value
            if (val < -1 || val > 1) val = 0;
            layer_biases_[layer][o] = static_cast<ternary::Trit>(val);
        }
    }
    
    return true;
}

size_t ForwardForwardLearner::parameter_count() const {
    size_t count = 0;
    
    for (size_t layer = 0; layer < config_.num_layers; ++layer) {
        // Weights: output_size * input_size
        if (!layer_weights_[layer].empty()) {
            count += layer_weights_[layer].size() * layer_weights_[layer][0].size();
        }
        // Biases: output_size
        count += layer_biases_[layer].size();
    }
    
    return count;
}

std::unique_ptr<ForwardForwardLearner> create_ff_learner(const FFConfig& config) {
    return std::make_unique<ForwardForwardLearner>(config);
}

} // namespace q_mini_wasm_v2::core::learning