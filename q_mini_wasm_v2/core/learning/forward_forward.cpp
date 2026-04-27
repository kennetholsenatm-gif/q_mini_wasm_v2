#include "forward_forward.hpp"
#include "entropy_goodness.hpp"
#include <algorithm>
#include <numeric>
#include <cmath>
#include <random>

namespace q_mini_wasm_v2::core::learning {

ForwardForwardLearner::ForwardForwardLearner(const FFConfig& config)
    : config_(config)
    , weights_initialized_(false)
{
    if (!config_.lazy_init) {
        // Pre-allocate layers and initialize weights immediately
        tropical_layers_.resize(config.num_layers);
        initialize_tropical_weights();
        weights_initialized_ = true;
    }
    // If lazy_init: tropical_layers_ stays empty - saves 99% memory for inactive experts
}

void ForwardForwardLearner::ensure_weights_initialized() {
    if (!weights_initialized_ && config_.lazy_init) {
        // Lazy allocate layers on first use
        tropical_layers_.resize(config_.num_layers);
        initialize_tropical_weights();
        weights_initialized_ = true;
    }
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
    
    ensure_weights_initialized();  // Lazy init on first use
    
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
    
    thread_local std::mt19937 rng(std::random_device{}());
    
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
        std::vector<ternary::Trit> permuted = corrupted;  // Copy first, then shuffle
        std::shuffle(permuted.begin(), permuted.end(), rng);  // Proper random shuffle
        
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
    ensure_weights_initialized();  // Lazy init on first use
    
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
    return tropical_forward_layer(layer_idx, input);
}

std::vector<ternary::Trit> ForwardForwardLearner::tropical_forward_layer(
    size_t layer_idx,
    const std::vector<ternary::Trit>& input
) {
    if (layer_idx >= config_.num_layers) {
        throw std::out_of_range("Layer index out of range");
    }
    
    const auto& layer = tropical_layers_[layer_idx];
    size_t output_size = config_.neurons_per_layer;
    std::vector<ternary::Trit> output(output_size, ternary::Trit::ZERO);
    
    // Tropical min-plus: output[target] = min over edges (input[source] + weight)
    // For ternary: we use sum instead of min, but only traverse sparse edges
    for (size_t source = 0; source < input.size() && source < layer.outgoing.size(); ++source) {
        if (input[source] == ternary::Trit::ZERO) continue;  // Skip inactive neurons
        
        const auto& edges = layer.outgoing[source];
        for (const auto& edge : edges) {
            // Ternary multiplication: -1 * -1 = +1, -1 * +1 = -1, etc.
            int product = static_cast<int>(input[source]) * static_cast<int>(edge.weight);
            // Accumulate (tropical "sum" is actually min in pure tropical, but we use addition for FF)
            int current = static_cast<int>(output[edge.target]);
            output[edge.target] = static_cast<ternary::Trit>(std::clamp(current + product, -1, 1));
        }
    }
    
    // Add biases and apply activation
    for (size_t o = 0; o < output_size; ++o) {
        int sum = static_cast<int>(output[o]) + static_cast<int>(layer.biases[o]);
        output[o] = tropical_activation(sum);
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

const std::vector<std::vector<TropicalEdge>>& ForwardForwardLearner::get_tropical_weights(size_t layer_idx) const {
    if (layer_idx >= config_.num_layers) {
        throw std::out_of_range("Layer index out of range");
    }
    return tropical_layers_[layer_idx].outgoing;
}

void ForwardForwardLearner::update_weights_hebbian(
    size_t layer_idx,
    const std::vector<ternary::Trit>& activations,
    int32_t delta
) {
    if (layer_idx >= config_.num_layers) {
        throw std::out_of_range("Layer index out of range");
    }
    
    auto& layer = tropical_layers_[layer_idx];
    
    // Hebbian learning on sparse tropical edges
    int32_t update_magnitude = delta >> config_.learning_rate_shift;
    if (update_magnitude == 0) return;
    
    int32_t direction = (update_magnitude > 0) ? 1 : -1;
    
    // Update only existing sparse edges
    for (size_t source = 0; source < layer.outgoing.size() && source < activations.size(); ++source) {
        if (activations[source] == ternary::Trit::ZERO) continue;
        
        int32_t pre = static_cast<int32_t>(activations[source]);
        
        for (auto& edge : layer.outgoing[source]) {
            int32_t current_w = static_cast<int32_t>(edge.weight);
            int32_t update = direction * pre;
            int32_t new_w = current_w + update;
            edge.weight = clip_to_ternary(new_w);
        }
    }
}

void ForwardForwardLearner::reset_weights() {
    initialize_tropical_weights();
}
// Internal Helpers
// ============================================================================

void ForwardForwardLearner::initialize_tropical_weights() {
    thread_local std::mt19937 rng(std::random_device{}());
    // Integer-based sparsity check: 0-9999, compare against sparsity_bps (500 = 5%)
    std::uniform_int_distribution<uint32_t> sparsity_dist(0, 9999);
    std::discrete_distribution<int> weight_dist({40, 20, 40}); // -1: 40%, 0: 20%, +1: 40%
    std::discrete_distribution<int> bias_dist({40, 20, 40});
    
    for (size_t layer = 0; layer < config_.num_layers; ++layer) {
        size_t neurons = config_.neurons_per_layer;
        
        // Initialize sparse tropical adjacency structure
        tropical_layers_[layer].outgoing.resize(neurons);
        tropical_layers_[layer].biases.resize(neurons);
        
        // Create sparse connections: only sparsity % of possible edges
        for (size_t source = 0; source < neurons; ++source) {
            for (size_t target = 0; target < neurons; ++target) {
                // Integer comparison: if random(0-9999) < sparsity_bps, create edge
                if (sparsity_dist(rng) < config_.sparsity_bps) {
                    // Only add edge if below sparsity threshold
                    ternary::Trit weight = static_cast<ternary::Trit>(weight_dist(rng));
                    if (weight != ternary::Trit::ZERO) {
                        add_tropical_edge(layer, static_cast<uint32_t>(source), 
                                         static_cast<uint32_t>(target), weight);
                    }
                }
            }
        }
        
        // Initialize biases
        for (auto& bias : tropical_layers_[layer].biases) {
            bias = static_cast<ternary::Trit>(bias_dist(rng));
        }
    }
}

void ForwardForwardLearner::add_tropical_edge(size_t layer, uint32_t source, uint32_t target, ternary::Trit weight) {
    if (layer >= tropical_layers_.size()) return;
    if (source >= tropical_layers_[layer].outgoing.size()) return;
    
    TropicalEdge edge{target, weight};
    tropical_layers_[layer].outgoing[source].push_back(edge);
}

ternary::Trit ForwardForwardLearner::tropical_activation(int32_t x) {
    // Tropical threshold activation - sharper than ternary
    static thread_local std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> noise(-1, 1);
    
    // Tropical geometry: min-plus algebra threshold
    int32_t threshold = 1;  // Tropical minimal threshold
    x += noise(rng);
    
    // Tropical activation: clip to {-1, 0, +1} with sharp boundaries
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
// Serialization
// ============================================================================

std::vector<uint8_t> ForwardForwardLearner::serialize_weights() const {
    std::vector<uint8_t> data;
    
    // Header: num_layers (1 byte), sparsity (1 byte as percent)
    data.push_back(static_cast<uint8_t>(config_.num_layers));
    data.push_back(static_cast<uint8_t>(config_.sparsity_bps / 100));
    
    // For each layer: [num_edges][edges...][biases...]
    for (size_t layer = 0; layer < config_.num_layers; ++layer) {
        const auto& layer_data = tropical_layers_[layer];
        
        // Count total edges
        uint32_t num_edges = 0;
        for (const auto& edges : layer_data.outgoing) {
            num_edges += static_cast<uint32_t>(edges.size());
        }
        
        // Store number of edges (4 bytes)
        data.push_back(static_cast<uint8_t>(num_edges & 0xFF));
        data.push_back(static_cast<uint8_t>((num_edges >> 8) & 0xFF));
        data.push_back(static_cast<uint8_t>((num_edges >> 16) & 0xFF));
        data.push_back(static_cast<uint8_t>((num_edges >> 24) & 0xFF));
        
        // Store edges: [source][target][weight]
        for (uint32_t source = 0; source < layer_data.outgoing.size(); ++source) {
            for (const auto& edge : layer_data.outgoing[source]) {
                data.push_back(static_cast<uint8_t>(source & 0xFF));
                data.push_back(static_cast<uint8_t>((source >> 8) & 0xFF));
                data.push_back(static_cast<uint8_t>(edge.target & 0xFF));
                data.push_back(static_cast<uint8_t>((edge.target >> 8) & 0xFF));
                data.push_back(static_cast<int8_t>(edge.weight));
            }
        }
        
        // Biases: each stored as int8_t {-1, 0, 1}
        for (const auto& b : layer_data.biases) {
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
    
    // Read sparsity (skip, we use config)
    if (pos >= data.size()) return false;
    pos++;
    
    if (num_layers > config_.num_layers) {
        return false; // Can't load more layers than configured
    }
    
    // Clear and resize tropical structures
    tropical_layers_.resize(config_.num_layers);
    for (auto& layer : tropical_layers_) {
        layer.outgoing.clear();
        layer.biases.clear();
    }
    
    for (size_t layer_idx = 0; layer_idx < num_layers; ++layer_idx) {
        auto& layer = tropical_layers_[layer_idx];
        layer.outgoing.resize(config_.neurons_per_layer);
        layer.biases.resize(config_.neurons_per_layer);
        
        // Read number of edges
        if (pos + 4 > data.size()) return false;
        uint32_t num_edges = data[pos] | (data[pos + 1] << 8) | 
                            (data[pos + 2] << 16) | (data[pos + 3] << 24);
        pos += 4;
        
        // Read edges
        for (uint32_t i = 0; i < num_edges; ++i) {
            if (pos + 5 > data.size()) return false;
            uint16_t source = data[pos] | (data[pos + 1] << 8);
            uint16_t target = data[pos + 2] | (data[pos + 3] << 8);
            int8_t weight = static_cast<int8_t>(data[pos + 4]);
            pos += 5;
            
            if (weight < -1 || weight > 1) weight = 0;
            if (source < config_.neurons_per_layer && target < config_.neurons_per_layer) {
                add_tropical_edge(layer_idx, source, target, static_cast<ternary::Trit>(weight));
            }
        }
        
        // Read biases
        if (pos + config_.neurons_per_layer > data.size()) return false;
        for (size_t o = 0; o < config_.neurons_per_layer; ++o) {
            int8_t val = static_cast<int8_t>(data[pos++]);
            if (val < -1 || val > 1) val = 0;
            layer.biases[o] = static_cast<ternary::Trit>(val);
        }
    }
    
    return true;
}

size_t ForwardForwardLearner::parameter_count() const {
    size_t count = 0;
    
    for (size_t layer = 0; layer < config_.num_layers; ++layer) {
        // Sparse edges: count actual edges (not all possible)
        for (const auto& edges : tropical_layers_[layer].outgoing) {
            count += edges.size();
        }
        // Biases: one per neuron
        count += tropical_layers_[layer].biases.size();
    }
    
    return count;
}

std::unique_ptr<ForwardForwardLearner> create_ff_learner(const FFConfig& config) {
    return std::make_unique<ForwardForwardLearner>(config);
}

} // namespace q_mini_wasm_v2::core::learning