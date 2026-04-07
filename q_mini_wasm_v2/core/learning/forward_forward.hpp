#pragma once

#include <vector>
#include <memory>
#include <cstdint>
#include "../ternary/trit.hpp"
#include "../stabilizer/tableau.hpp"

namespace q_mini_wasm_v2::core::learning {

struct FFConfig {
    size_t num_layers = 3;
    size_t neurons_per_layer = 128;
    int learning_rate = 1;  // GF(3) fixed-point Q24.8 representation
    int learning_rate_shift = 10;  // For fixed-point arithmetic
};

struct LayerGoodness {
    uint32_t positive_goodness = 0;
    uint32_t negative_goodness = 0;
    int32_t delta = 0;
};

class ForwardForwardLearner {
public:
    explicit ForwardForwardLearner(const FFConfig& config);
    ~ForwardForwardLearner();

    // Training
    LayerGoodness train_layer(
        size_t layer_idx,
        const std::vector<std::vector<ternary::Trit>>& positive_data,
        const std::vector<std::vector<ternary::Trit>>& negative_data
    );
    
    LayerGoodness train_layer_entangled(
        size_t layer_idx,
        const std::vector<std::vector<ternary::Trit>>& positive_data,
        const std::vector<std::vector<ternary::Trit>>& negative_data
    );

    // Negative sample generation
    std::vector<std::vector<ternary::Trit>> generate_negative_samples(
        const std::vector<std::vector<ternary::Trit>>& positive_data
    );

    // Goodness computation
    uint32_t compute_goodness(const std::vector<ternary::Trit>& activations) const;
    uint32_t compute_entangled_goodness(const stabilizer::StabilizerTableau& tableau) const;

    // Forward pass
    std::vector<ternary::Trit> forward(const std::vector<ternary::Trit>& input);
    std::vector<ternary::Trit> forward_layer(size_t layer_idx, const std::vector<ternary::Trit>& input);
    std::vector<ternary::Trit> entangled_forward(stabilizer::StabilizerTableau& tableau, const std::vector<ternary::Trit>& input);

    // Weight management
    const std::vector<std::vector<ternary::Trit>>& get_weights(size_t layer_idx) const;
    void update_weights_hebbian(size_t layer_idx, const std::vector<ternary::Trit>& activations, int32_t delta);
    void reset_weights();

private:
    FFConfig config_;
    std::vector<std::vector<std::vector<ternary::Trit>>> layer_weights_;
    std::vector<std::vector<ternary::Trit>> layer_biases_;

    void initialize_weights();
    ternary::Trit ternary_activation(int32_t x);
    ternary::Trit compute_weight_update(ternary::Trit pre_synaptic, ternary::Trit post_synaptic);
    ternary::Trit clip_to_ternary(int value);
};

std::unique_ptr<ForwardForwardLearner> create_ff_learner(const FFConfig& config);

} // namespace q_mini_wasm_v2::core::learning
