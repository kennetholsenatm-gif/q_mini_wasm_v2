#pragma once

#include <vector>
#include <cstdint>
#include <memory>
#include <chrono>
#include "../stabilizer/tableau.hpp"
#include "../stabilizer/clifford_synthesis.hpp"

namespace q_mini_wasm_v2::core::inference {

/**
 * @brief Enhanced Householder Circuit Synthesizer
 * 
 * Provides O(n^2) circuit synthesis via Householder reflections.
 * Optimized for sub-millisecond inference latency on edge devices.
 */
class HouseholderSynthesizer {
public:
    struct SynthesisConfig {
        size_t max_qutrits;
        double target_precision;
        size_t max_reflection_depth;
        bool enable_parallel_synthesis;
    };

    struct SynthesisResult {
        std::vector<stabilizer::SynthesizedGate> gate_sequence;
        double synthesis_time_ms;
        size_t gate_count;
        double approximation_error;
        bool meets_latency_target;
    };

    struct HouseholderReflection {
        std::vector<int8_t> reflection_vector;
        size_t target_qutrit;
        double phase_angle;
    };

    explicit HouseholderSynthesizer(const SynthesisConfig& config);
    ~HouseholderSynthesizer();

    SynthesisResult synthesize_target_state(
        const std::vector<int8_t>& target_state,
        stabilizer::StabilizerTableau& tableau
    );

    SynthesisResult synthesize_unitary(
        const std::vector<std::vector<int8_t>>& target_matrix,
        stabilizer::StabilizerTableau& tableau
    );

    std::vector<HouseholderReflection> decompose_to_reflections(
        const std::vector<int8_t>& target_state
    );

    std::vector<stabilizer::SynthesizedGate> reflection_to_clifford(
        const HouseholderReflection& reflection
    );

    void apply_synthesis_result(
        stabilizer::StabilizerTableau& tableau,
        const SynthesisResult& result
    );

    double estimate_synthesis_time(size_t num_qutrits) const;
    const SynthesisConfig& config() const { return config_; }

private:
    SynthesisConfig config_;

    HouseholderReflection compute_reflection(
        const std::vector<int8_t>& current,
        const std::vector<int8_t>& target
    );

    std::vector<stabilizer::SynthesizedGate> reflection_to_gates(
        const HouseholderReflection& reflection,
        size_t qutrit_offset
    );

    double compute_reflection_error(
        const std::vector<int8_t>& achieved,
        const std::vector<int8_t>& target
    );
};

std::unique_ptr<HouseholderSynthesizer> create_householder_synthesizer(
    const HouseholderSynthesizer::SynthesisConfig& config
);

} // namespace q_mini_wasm_v2::core::inference