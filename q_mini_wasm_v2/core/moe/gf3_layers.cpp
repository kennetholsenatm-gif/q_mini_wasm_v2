#include "gf3_layers.hpp"
#include <random>
#include <algorithm>
#include <numeric>

namespace q_mini_wasm_v2::core::moe {

// ============================================================================
// GF3LinearLayer Implementation
// ============================================================================

GF3LinearLayer::GF3LinearLayer(const LayerConfig& config)
    : config_(config)
{
    // Initialize weight matrix [input_dim][output_dim]
    weights_.resize(config_.input_dim);
    for (auto& row : weights_) {
        row.resize(config_.output_dim, ternary::Trit::ZERO);
    }
    
    // Initialize bias
    if (config_.use_bias == ternary::Trit::POSITIVE) {
        bias_.resize(config_.output_dim, ternary::Trit::ZERO);
    }
}

std::vector<ternary::Trit> GF3LinearLayer::Forward(
    const std::vector<ternary::Trit>& input
) {
    if (config_.use_tropical == ternary::Trit::POSITIVE) {
        return TropicalForward(input);
    } else {
        return StandardForward(input);
    }
}

std::vector<ternary::Trit> GF3LinearLayer::TropicalForward(
    const std::vector<ternary::Trit>& input
) {
    std::vector<ternary::Trit> output(config_.output_dim, ternary::Trit::NEGATIVE);
    
    for (size_t j = 0; j < config_.output_dim; ++j) {
        int8_t max_val = -2;  // Below minimum ternary value
        
        for (size_t i = 0; i < config_.input_dim; ++i) {
            if (i >= input.size()) break;
            
            // Tropical: output[j] = max_i(input[i] + weight[i][j])
            int8_t val = TropicalMultiply(
                static_cast<int8_t>(input[i]),
                static_cast<int8_t>(weights_[i][j])
            );
            max_val = TropicalAdd(max_val, val);
        }
        
        // Add bias if enabled
        if (config_.use_bias == ternary::Trit::POSITIVE && j < bias_.size()) {
            max_val = TropicalAdd(max_val, static_cast<int8_t>(bias_[j]));
        }
        
        // Clamp to valid ternary range
        if (max_val > 1) max_val = 1;
        if (max_val < -1) max_val = -1;
        
        output[j] = static_cast<ternary::Trit>(max_val);
    }
    
    return output;
}

std::vector<ternary::Trit> GF3LinearLayer::StandardForward(
    const std::vector<ternary::Trit>& input
) {
    std::vector<int32_t> pre_output(config_.output_dim, 0);
    
    // Compute weighted sum
    for (size_t j = 0; j < config_.output_dim; ++j) {
        int32_t sum = 0;
        
        for (size_t i = 0; i < config_.input_dim; ++i) {
            if (i >= input.size()) break;
            
            // Standard GF(3): sum += input[i] * weight[i][j]
            int8_t product = GF3Multiply(
                static_cast<int8_t>(input[i]),
                static_cast<int8_t>(weights_[i][j])
            );
            sum += product;
        }
        
        // Add bias
        if (config_.use_bias == ternary::Trit::POSITIVE && j < bias_.size()) {
            sum += static_cast<int8_t>(bias_[j]);
        }
        
        pre_output[j] = sum;
    }
    
    // Apply activation (ternarization)
    return GF3MultiLayerExpert::TernaryActivation(pre_output);
}

void GF3LinearLayer::InitializeWeights(int seed) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> dist(-1, 1);
    
    for (size_t i = 0; i < config_.input_dim; ++i) {
        for (size_t j = 0; j < config_.output_dim; ++j) {
            weights_[i][j] = static_cast<ternary::Trit>(dist(rng));
        }
    }
    
    if (config_.use_bias == ternary::Trit::POSITIVE) {
        for (size_t j = 0; j < config_.output_dim; ++j) {
            bias_[j] = static_cast<ternary::Trit>(dist(rng));
        }
    }
}

void GF3LinearLayer::SetWeight(size_t in_idx, size_t out_idx, ternary::Trit value) {
    if (in_idx < config_.input_dim && out_idx < config_.output_dim) {
        weights_[in_idx][out_idx] = value;
    }
}

ternary::Trit GF3LinearLayer::GetWeight(size_t in_idx, size_t out_idx) const {
    if (in_idx < config_.input_dim && out_idx < config_.output_dim) {
        return weights_[in_idx][out_idx];
    }
    return ternary::Trit::ZERO;
}

void GF3LinearLayer::SetBias(const std::vector<ternary::Trit>& bias) {
    if (config_.use_bias == ternary::Trit::POSITIVE && bias.size() == config_.output_dim) {
        bias_ = bias;
    }
}

std::vector<ternary::Trit> GF3LinearLayer::GetWeights() const {
    std::vector<ternary::Trit> flat;
    flat.reserve(config_.input_dim * config_.output_dim);
    
    for (const auto& row : weights_) {
        flat.insert(flat.end(), row.begin(), row.end());
    }
    
    return flat;
}

std::vector<ternary::Trit> GF3LinearLayer::GetBias() const {
    return bias_;
}

uint32_t GF3LinearLayer::ComputeGoodness(
    const std::vector<ternary::Trit>& activations
) const {
    uint32_t goodness = 0;
    
    // Goodness = sum of |activation| (count non-zero for ternary)
    for (auto val : activations) {
        int8_t v = static_cast<int8_t>(val);
        goodness += (v != 0) ? 1 : 0;
    }
    
    return goodness;
}

void GF3LinearLayer::UpdateWeightsHebbian(
    const std::vector<ternary::Trit>& input,
    int32_t goodness_delta,
    int8_t learning_rate
) {
    // Hebbian update in GF(3)
    // Δw = learning_rate × goodness_delta × input × output (ternary)
    
    if (goodness_delta == 0) return;
    
    int8_t update_sign = (goodness_delta > 0) ? learning_rate : -learning_rate;
    
    for (size_t j = 0; j < config_.output_dim; ++j) {
        for (size_t i = 0; i < config_.input_dim; ++i) {
            if (i >= input.size()) break;
            
            // Compute update direction
            int8_t delta = GF3Multiply(
                update_sign,
                static_cast<int8_t>(input[i])
            );
            
            // Apply update
            int8_t new_weight = GF3Add(
                static_cast<int8_t>(weights_[i][j]),
                delta
            );
            
            weights_[i][j] = static_cast<ternary::Trit>(new_weight);
        }
    }
}

uint32_t GF3LinearLayer::GetSparsity() const {
    size_t zeros = 0;
    size_t total = config_.input_dim * config_.output_dim;
    
    for (const auto& row : weights_) {
        for (auto val : row) {
            if (val == ternary::Trit::ZERO) zeros++;
        }
    }
    
    // Q24.8 fixed point representation: (zeros << 8) / total
    return static_cast<uint32_t>((zeros << 8) / total);
}

// ============================================================================
// GF3MultiLayerExpert Implementation
// ============================================================================

GF3MultiLayerExpert::GF3MultiLayerExpert(const ExpertConfig& config)
    : ExpertNetwork(config)
{
    // First layer: input_dim -> hidden_dim
    AddLayer(config_.hidden_dim);
    
    // Middle layers: hidden_dim -> hidden_dim
    for (size_t l = 1; l < config_.num_layers; ++l) {
        AddLayer(config_.hidden_dim);
    }
    
    // Final layer: hidden_dim -> output_dim
    AddLayer(config_.output_dim);
}

void GF3MultiLayerExpert::AddLayer(size_t output_dim, ternary::Trit use_tropical) {
    size_t input_dim = layers_.empty() ? config_.input_dim : layers_.back()->GetOutputDim();
    
    GF3LinearLayer::LayerConfig layer_config;
    layer_config.input_dim = input_dim;
    layer_config.output_dim = output_dim;
    layer_config.use_tropical = use_tropical;
    
    layers_.push_back(std::make_unique<GF3LinearLayer>(layer_config));
}

void GF3MultiLayerExpert::InitializeAllLayers(int seed) {
    int layer_seed = seed;
    for (auto& layer : layers_) {
        layer->InitializeWeights(layer_seed++);
    }
    initialized_ = ternary::Trit::POSITIVE;
}

std::vector<ternary::Trit> GF3MultiLayerExpert::Forward(
    const std::vector<ternary::Trit>& input
) {
    if (initialized_ == ternary::Trit::ZERO) {
        InitializeAllLayers();
    }
    
    std::vector<ternary::Trit> current = input;
    
    for (size_t i = 0; i < layers_.size(); ++i) {
        current = layers_[i]->Forward(current);
        
        // Don't apply activation on final layer if not requested
        if (config_.use_activation == ternary::Trit::ZERO && i == layers_.size() - 1) {
            break;
        }
    }
    
    stats_.forward_calls++;
    return current;
}

int32_t GF3MultiLayerExpert::TrainForwardForward(
    const std::vector<ternary::Trit>& positive,
    const std::vector<ternary::Trit>& negative
) {
    // Forward pass for positive sample
    auto pos_output = Forward(positive);
    uint32_t pos_goodness = ComputeGoodness(pos_output);
    
    // Forward pass for negative sample
    auto neg_output = Forward(negative);
    uint32_t neg_goodness = ComputeGoodness(neg_output);
    
    // Compute delta
    int32_t delta = static_cast<int32_t>(pos_goodness) - static_cast<int32_t>(neg_goodness);
    
    // Update weights for each layer using Hebbian rule
    if (delta > 0) {
        std::vector<ternary::Trit> current_input = positive;
        
        for (size_t i = 0; i < layers_.size(); ++i) {
            auto& layer = layers_[i];
            
            // Get pre-activation output for this layer
            std::vector<int32_t> pre_act(layer->GetOutputDim());
            for (size_t j = 0; j < layer->GetOutputDim(); ++j) {
                int32_t sum = 0;
                for (size_t k = 0; k < layer->GetInputDim(); ++k) {
                    if (k < current_input.size()) {
                        sum += static_cast<int8_t>(current_input[k]) * 
                               static_cast<int8_t>(layer->GetWeight(k, j));
                    }
                }
                pre_act[j] = sum;
            }
            
            // Apply activation
            auto activated = TernaryActivation(pre_act);
            
            // Update weights
            layer->UpdateWeightsHebbian(current_input, delta, 1);
            
            // Propagate to next layer
            current_input = activated;
        }
    }
    
    stats_.train_calls++;
    stats_.total_goodness_delta += delta;
    
    return delta;
}

std::vector<ternary::Trit> GF3MultiLayerExpert::TernaryActivation(
    const std::vector<int32_t>& pre_activations
) {
    std::vector<ternary::Trit> output;
    output.reserve(pre_activations.size());
    
    for (auto val : pre_activations) {
        // Threshold at ±0.5
        if (val > 0) {
            output.push_back(ternary::Trit::POSITIVE);
        } else if (val < 0) {
            output.push_back(ternary::Trit::NEGATIVE);
        } else {
            output.push_back(ternary::Trit::ZERO);
        }
    }
    
    return output;
}

} // namespace q_mini_wasm_v2::core::moe
