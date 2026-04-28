#include "gf3_layers.hpp"
#include "../../sycl/gf3_layers_sycl.hpp"
#include <atomic>
#include <random>
#include <algorithm>
#include <numeric>
#include <iostream>
#include <cstring>
#include <cstdint>

namespace q_mini_wasm_v2::core::moe {

namespace {
std::atomic<uint64_t> g_gf3_hebbian_weight_cell_updates{0};
}

uint64_t gf3_hebbian_weight_cell_updates_total() noexcept {
    return g_gf3_hebbian_weight_cell_updates.load(std::memory_order_relaxed);
}

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
    if (q_mini_wasm_v2::sycl_kernels::gf3_sycl_forward_enabled_for_shape(config_.input_dim, config_.output_dim)) {
        std::vector<int8_t> in_b(config_.input_dim, 0);
        const size_t in_lim = std::min(input.size(), config_.input_dim);
        for (size_t i = 0; i < in_lim; ++i) {
            in_b[i] = static_cast<int8_t>(input[i]);
        }
        std::vector<int8_t> w_row(config_.input_dim * config_.output_dim);
        for (size_t i = 0; i < config_.input_dim; ++i) {
            for (size_t j = 0; j < config_.output_dim; ++j) {
                w_row[i * config_.output_dim + j] = static_cast<int8_t>(weights_[i][j]);
            }
        }
        const bool use_bias = config_.use_bias == ternary::Trit::POSITIVE;
        std::vector<int8_t> bias_b;
        if (use_bias) {
            bias_b.resize(config_.output_dim, 0);
            for (size_t j = 0; j < config_.output_dim && j < bias_.size(); ++j) {
                bias_b[j] = static_cast<int8_t>(bias_[j]);
            }
        }
        std::vector<int8_t> out_b;
        if (q_mini_wasm_v2::sycl_kernels::gf3_tropical_linear_forward_sycl(
                in_b,
                w_row,
                bias_b,
                use_bias,
                config_.input_dim,
                config_.output_dim,
                out_b)
            && out_b.size() == config_.output_dim) {
            std::vector<ternary::Trit> output(config_.output_dim);
            for (size_t j = 0; j < config_.output_dim; ++j) {
                output[j] = static_cast<ternary::Trit>(out_b[j]);
            }
            return output;
        }
    }

    std::vector<ternary::Trit> output(config_.output_dim, ternary::Trit::NEGATIVE);
    const int64_t out_d = static_cast<int64_t>(config_.output_dim);
#if defined(_OPENMP)
#pragma omp parallel for schedule(static) if(out_d > 64)
#endif
    for (int64_t jj = 0; jj < out_d; ++jj) {
        const size_t j = static_cast<size_t>(jj);
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
    if (q_mini_wasm_v2::sycl_kernels::gf3_sycl_forward_enabled_for_shape(config_.input_dim, config_.output_dim)) {
        std::vector<int8_t> in_b(config_.input_dim, 0);
        const size_t in_lim = std::min(input.size(), config_.input_dim);
        for (size_t i = 0; i < in_lim; ++i) {
            in_b[i] = static_cast<int8_t>(input[i]);
        }
        std::vector<int8_t> w_row(config_.input_dim * config_.output_dim);
        for (size_t i = 0; i < config_.input_dim; ++i) {
            for (size_t j = 0; j < config_.output_dim; ++j) {
                w_row[i * config_.output_dim + j] = static_cast<int8_t>(weights_[i][j]);
            }
        }
        const bool use_bias = config_.use_bias == ternary::Trit::POSITIVE;
        std::vector<int8_t> bias_b;
        if (use_bias) {
            bias_b.resize(config_.output_dim, 0);
            for (size_t j = 0; j < config_.output_dim && j < bias_.size(); ++j) {
                bias_b[j] = static_cast<int8_t>(bias_[j]);
            }
        }
        std::vector<int8_t> out_b;
        if (q_mini_wasm_v2::sycl_kernels::gf3_standard_linear_forward_sycl(
                in_b,
                w_row,
                bias_b,
                use_bias,
                config_.input_dim,
                config_.output_dim,
                out_b)
            && out_b.size() == config_.output_dim) {
            std::vector<ternary::Trit> output(config_.output_dim);
            for (size_t j = 0; j < config_.output_dim; ++j) {
                output[j] = static_cast<ternary::Trit>(out_b[j]);
            }
            return output;
        }
    }

    std::vector<int32_t> pre_output(config_.output_dim, 0);
    const int64_t out_d = static_cast<int64_t>(config_.output_dim);
    
    // Compute weighted sum
#if defined(_OPENMP)
#pragma omp parallel for schedule(static) if(out_d > 64)
#endif
    for (int64_t jj = 0; jj < out_d; ++jj) {
        const size_t j = static_cast<size_t>(jj);
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

    if (q_mini_wasm_v2::sycl_kernels::gf3_sycl_forward_enabled_for_shape(config_.input_dim, config_.output_dim)) {
        std::vector<int8_t> in_b(config_.input_dim, 0);
        const size_t in_lim = std::min(input.size(), config_.input_dim);
        for (size_t i = 0; i < in_lim; ++i) {
            in_b[i] = static_cast<int8_t>(input[i]);
        }
        std::vector<int8_t> w_row(config_.input_dim * config_.output_dim);
        for (size_t i = 0; i < config_.input_dim; ++i) {
            for (size_t j = 0; j < config_.output_dim; ++j) {
                w_row[i * config_.output_dim + j] = static_cast<int8_t>(weights_[i][j]);
            }
        }
        if (q_mini_wasm_v2::sycl_kernels::gf3_hebbian_update_sycl(
                in_b, goodness_delta, learning_rate, config_.input_dim, config_.output_dim, w_row)) {
            const int8_t update_sign = (goodness_delta > 0) ? learning_rate : -learning_rate;
            for (size_t i = 0; i < config_.input_dim; ++i) {
                for (size_t j = 0; j < config_.output_dim; ++j) {
                    weights_[i][j] = static_cast<ternary::Trit>(w_row[i * config_.output_dim + j]);
                    if (i < in_lim) {
                        const int8_t delta = GF3Multiply(update_sign, static_cast<int8_t>(input[i]));
                        if (delta != 0) {
                            g_gf3_hebbian_weight_cell_updates.fetch_add(1, std::memory_order_relaxed);
                        }
                    }
                }
            }
            return;
        }
    }
    
    int8_t update_sign = (goodness_delta > 0) ? learning_rate : -learning_rate;
    const int64_t out_d = static_cast<int64_t>(config_.output_dim);
#if defined(_OPENMP)
#pragma omp parallel for schedule(static) if(out_d > 48)
#endif
    for (int64_t jj = 0; jj < out_d; ++jj) {
        const size_t j = static_cast<size_t>(jj);
        for (size_t i = 0; i < config_.input_dim; ++i) {
            if (i >= input.size()) break;
            
            // Compute update direction
            int8_t delta = GF3Multiply(
                update_sign,
                static_cast<int8_t>(input[i])
            );
            
            // Apply update (GF(3) wrap); count each cell touched by a non-trivial Hebbian step.
            int8_t new_weight = GF3Add(
                static_cast<int8_t>(weights_[i][j]),
                delta
            );
            
            weights_[i][j] = static_cast<ternary::Trit>(new_weight);
            if (delta != 0) {
                g_gf3_hebbian_weight_cell_updates.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }
}

namespace {
void write_raw(std::ostream& os, const void* p, std::streamsize n) {
    os.write(static_cast<const char*>(p), n);
}

void read_raw(std::istream& is, void* p, std::streamsize n) {
    is.read(static_cast<char*>(p), n);
}

bool trit_ok(int8_t v) {
    return v == -1 || v == 0 || v == 1;
}
} // namespace

void GF3LinearLayer::SerializeWeights(std::ostream& os) const {
    const uint64_t in_d = static_cast<uint64_t>(config_.input_dim);
    const uint64_t out_d = static_cast<uint64_t>(config_.output_dim);
    const int8_t use_bias = static_cast<int8_t>(config_.use_bias);
    const int8_t use_tropical = static_cast<int8_t>(config_.use_tropical);
    write_raw(os, &in_d, sizeof(in_d));
    write_raw(os, &out_d, sizeof(out_d));
    write_raw(os, &use_bias, sizeof(use_bias));
    write_raw(os, &use_tropical, sizeof(use_tropical));
    for (size_t i = 0; i < config_.input_dim; ++i) {
        for (size_t j = 0; j < config_.output_dim; ++j) {
            int8_t w = static_cast<int8_t>(weights_[i][j]);
            write_raw(os, &w, sizeof(w));
        }
    }
    if (config_.use_bias == ternary::Trit::POSITIVE) {
        for (size_t j = 0; j < bias_.size() && j < config_.output_dim; ++j) {
            int8_t b = static_cast<int8_t>(bias_[j]);
            write_raw(os, &b, sizeof(b));
        }
    }
}

bool GF3LinearLayer::DeserializeWeights(std::istream& is) {
    uint64_t in_d = 0;
    uint64_t out_d = 0;
    int8_t use_bias = 0;
    int8_t use_tropical = 0;
    read_raw(is, &in_d, sizeof(in_d));
    read_raw(is, &out_d, sizeof(out_d));
    read_raw(is, &use_bias, sizeof(use_bias));
    read_raw(is, &use_tropical, sizeof(use_tropical));
    if (!is || in_d != config_.input_dim || out_d != config_.output_dim) {
        return false;
    }
    if (static_cast<ternary::Trit>(use_bias) != config_.use_bias ||
        static_cast<ternary::Trit>(use_tropical) != config_.use_tropical) {
        return false;
    }
    for (size_t i = 0; i < config_.input_dim; ++i) {
        for (size_t j = 0; j < config_.output_dim; ++j) {
            int8_t w = 0;
            read_raw(is, &w, sizeof(w));
            if (!is || !trit_ok(w)) {
                return false;
            }
            weights_[i][j] = static_cast<ternary::Trit>(w);
        }
    }
    if (config_.use_bias == ternary::Trit::POSITIVE) {
        for (size_t j = 0; j < config_.output_dim; ++j) {
            int8_t b = 0;
            read_raw(is, &b, sizeof(b));
            if (!is || !trit_ok(b)) {
                return false;
            }
            if (j < bias_.size()) {
                bias_[j] = static_cast<ternary::Trit>(b);
            }
        }
    }
    return static_cast<bool>(is);
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

    record_ff_route_output_goodness(pos_goodness, neg_goodness);

    // Update weights for each layer using Hebbian rule
    if (delta > 0) {
        std::vector<ternary::Trit> current_input = positive;
        
        for (size_t i = 0; i < layers_.size(); ++i) {
            auto& layer = layers_[i];
            
            // Get pre-activation output for this layer
            std::vector<int32_t> pre_act(layer->GetOutputDim());
            const int64_t layer_out = static_cast<int64_t>(layer->GetOutputDim());
#if defined(_OPENMP)
#pragma omp parallel for schedule(static) if(layer_out > 48)
#endif
            for (int64_t jj = 0; jj < layer_out; ++jj) {
                const size_t j = static_cast<size_t>(jj);
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

void GF3MultiLayerExpert::SerializeWeights(std::ostream& os) const {
    const uint32_t magic = 0x45334647u; // 'GF3E' when serialized as uint32 LE
    write_raw(os, &magic, sizeof(magic));
    const uint64_t in_dim = static_cast<uint64_t>(config_.input_dim);
    const uint64_t out_dim = static_cast<uint64_t>(config_.output_dim);
    const uint64_t hid_dim = static_cast<uint64_t>(config_.hidden_dim);
    const uint64_t num_layers = static_cast<uint64_t>(config_.num_layers);
    const int8_t use_act = static_cast<int8_t>(config_.use_activation);
    const int8_t energy = static_cast<int8_t>(config_.energy_budget);
    write_raw(os, &in_dim, sizeof(in_dim));
    write_raw(os, &out_dim, sizeof(out_dim));
    write_raw(os, &hid_dim, sizeof(hid_dim));
    write_raw(os, &num_layers, sizeof(num_layers));
    write_raw(os, &use_act, sizeof(use_act));
    write_raw(os, &energy, sizeof(energy));
    const uint32_t n_lin = static_cast<uint32_t>(layers_.size());
    write_raw(os, &n_lin, sizeof(n_lin));
    for (const auto& layer : layers_) {
        layer->SerializeWeights(os);
    }
}

bool GF3MultiLayerExpert::DeserializeWeights(std::istream& is) {
    uint32_t magic = 0;
    read_raw(is, &magic, sizeof(magic));
    if (!is || magic != 0x45334647u) {
        return false;
    }
    uint64_t in_dim = 0;
    uint64_t out_dim = 0;
    uint64_t hid_dim = 0;
    uint64_t num_layers = 0;
    int8_t use_act = 0;
    int8_t energy = 0;
    read_raw(is, &in_dim, sizeof(in_dim));
    read_raw(is, &out_dim, sizeof(out_dim));
    read_raw(is, &hid_dim, sizeof(hid_dim));
    read_raw(is, &num_layers, sizeof(num_layers));
    read_raw(is, &use_act, sizeof(use_act));
    read_raw(is, &energy, sizeof(energy));
    if (!is || in_dim != config_.input_dim || out_dim != config_.output_dim ||
        hid_dim != config_.hidden_dim || num_layers != config_.num_layers ||
        static_cast<ternary::Trit>(use_act) != config_.use_activation ||
        static_cast<ternary::EnergyTrit>(energy) != config_.energy_budget) {
        return false;
    }
    uint32_t n_lin = 0;
    read_raw(is, &n_lin, sizeof(n_lin));
    if (!is || n_lin != layers_.size()) {
        return false;
    }
    for (auto& layer : layers_) {
        if (!layer->DeserializeWeights(is)) {
            return false;
        }
    }
    initialized_ = ternary::Trit::POSITIVE;
    return static_cast<bool>(is);
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
