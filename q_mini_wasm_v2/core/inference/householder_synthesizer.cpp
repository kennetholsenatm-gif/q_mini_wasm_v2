#include "householder_synthesizer.hpp"
#include <algorithm>
#include <cmath>
#include <chrono>

namespace q_mini_wasm_v2::core::inference {

HouseholderSynthesizer::HouseholderSynthesizer(const SynthesisConfig& config)
    : config_(config) {}

HouseholderSynthesizer::~HouseholderSynthesizer() = default;

HouseholderSynthesizer::HouseholderReflection HouseholderSynthesizer::compute_reflection(
    const std::vector<int8_t>& current,
    const std::vector<int8_t>& target
) {
    HouseholderReflection reflection;
    reflection.target_qutrit = 0;
    reflection.phase_angle_fixed = 0;  // Fixed-point: 0 radians
    
    size_t dim = std::min(current.size(), target.size());
    reflection.reflection_vector.resize(dim);
    
    for (size_t i = 0; i < dim; ++i) {
        reflection.reflection_vector[i] = (target[i] - current[i] + 3) % 3;
    }
    
    return reflection;
}

std::vector<stabilizer::SynthesizedGate> HouseholderSynthesizer::reflection_to_gates(
    const HouseholderReflection& reflection,
    size_t qutrit_offset
) {
    std::vector<stabilizer::SynthesizedGate> gates;
    
    for (size_t i = 0; i < reflection.reflection_vector.size(); ++i) {
        int8_t val = reflection.reflection_vector[i];
        
        if (val == 1) {
            gates.push_back({stabilizer::CliffordGate::PAULI_X, qutrit_offset + i, 0});
        } else if (val == 2) {
            gates.push_back({stabilizer::CliffordGate::PAULI_X, qutrit_offset + i, 0});
            gates.push_back({stabilizer::CliffordGate::PAULI_X, qutrit_offset + i, 0});
        }
    }
    
    return gates;
}

double HouseholderSynthesizer::compute_reflection_error(
    const std::vector<int8_t>& achieved,
    const std::vector<int8_t>& target
) {
    double error = 0.0;
    size_t dim = std::min(achieved.size(), target.size());
    
    for (size_t i = 0; i < dim; ++i) {
        int diff = (achieved[i] - target[i] + 3) % 3;
        error += diff * diff;
    }
    
    return std::sqrt(error / dim);
}

HouseholderSynthesizer::SynthesisResult HouseholderSynthesizer::synthesize_target_state(
    const std::vector<int8_t>& target_state,
    stabilizer::StabilizerTableau& tableau
) {
    auto start_time = std::chrono::high_resolution_clock::now();
    
    SynthesisResult result;
    result.approximation_error_fixed = 1000;  // Fixed-point: 1.0 = 1000
    result.meets_latency_target = 1;  // true = 1
    
    std::vector<int8_t> current_state(config_.max_qutrits, 0);
    
    for (size_t depth = 0; depth < config_.max_reflection_depth; ++depth) {
        auto reflection = compute_reflection(current_state, target_state);
        auto gates = reflection_to_gates(reflection, 0);
        
        for (const auto& gate : gates) {
            result.gate_sequence.push_back(gate);
        }
        
        for (size_t i = 0; i < std::min(current_state.size(), reflection.reflection_vector.size()); ++i) {
            current_state[i] = (current_state[i] + reflection.reflection_vector[i]) % 3;
        }
        
        result.approximation_error_fixed = static_cast<int32_t>(
            compute_reflection_error(current_state, target_state) * 1000);
        
        // Fixed-point comparison: 1000 = 1.0
        if (result.approximation_error_fixed <= config_.target_precision_fixed) {
            break;
        }
    }
    
    for (size_t i = 0; i + 1 < config_.max_qutrits; i += 2) {
        result.gate_sequence.push_back({stabilizer::CliffordGate::CSUM, i, i + 1});
    }
    
    result.gate_count = result.gate_sequence.size();
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
    result.synthesis_time_ms_fixed = static_cast<int32_t>(duration.count());
    
    result.meets_latency_target = (result.synthesis_time_ms_fixed < 1000) ? 1 : 0;
    
    return result;
}

HouseholderSynthesizer::SynthesisResult HouseholderSynthesizer::synthesize_unitary(
    const std::vector<std::vector<int8_t>>& target_matrix,
    stabilizer::StabilizerTableau& tableau
) {
    SynthesisResult result;
    
    if (target_matrix.empty()) {
        result.gate_count = 0;
        result.synthesis_time_ms_fixed = 0;
        result.approximation_error_fixed = 0;
        result.meets_latency_target = 1;
        return result;
    }
    
    std::vector<int8_t> diagonal;
    for (size_t i = 0; i < target_matrix.size() && i < target_matrix[i].size(); ++i) {
        diagonal.push_back(target_matrix[i][i]);
    }
    
    return synthesize_target_state(diagonal, tableau);
}

std::vector<HouseholderSynthesizer::HouseholderReflection> HouseholderSynthesizer::decompose_to_reflections(
    const std::vector<int8_t>& target_state
) {
    std::vector<HouseholderReflection> reflections;
    
    std::vector<int8_t> current_state(target_state.size(), 0);
    
    while (true) {
        auto reflection = compute_reflection(current_state, target_state);
        
        int8_t all_zero = 1;
        for (auto val : reflection.reflection_vector) {
            if (val != 0) {
                all_zero = 0;
                break;
            }
        }
        
        if (all_zero) {
            break;
        }
        
        reflections.push_back(reflection);
        
        for (size_t i = 0; i < std::min(current_state.size(), reflection.reflection_vector.size()); ++i) {
            current_state[i] = (current_state[i] + reflection.reflection_vector[i]) % 3;
        }
        
        if (reflections.size() >= config_.max_reflection_depth) {
            break;
        }
    }
    
    return reflections;
}

std::vector<stabilizer::SynthesizedGate> HouseholderSynthesizer::reflection_to_clifford(
    const HouseholderReflection& reflection
) {
    return reflection_to_gates(reflection, 0);
}

void HouseholderSynthesizer::apply_synthesis_result(
    stabilizer::StabilizerTableau& tableau,
    const SynthesisResult& result
) {
    stabilizer::CliffordSynthesizer::apply_sequence(tableau, result.gate_sequence);
}

int32_t HouseholderSynthesizer::estimate_synthesis_time_fixed(size_t num_qutrits) const {
    return static_cast<int32_t>(num_qutrits * num_qutrits);
}

std::unique_ptr<HouseholderSynthesizer> create_householder_synthesizer(
    const HouseholderSynthesizer::SynthesisConfig& config
) {
    return std::make_unique<HouseholderSynthesizer>(config);
}

} // namespace q_mini_wasm_v2::core::inference