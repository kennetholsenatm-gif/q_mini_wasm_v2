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
        int32_t target_precision_fixed;  // Fixed-point: 1000 = 1.0 (was double)
        size_t max_reflection_depth;
        int8_t enable_parallel_synthesis;  // 0/1 instead of bool
    };

    struct SynthesisResult {
        std::vector<stabilizer::SynthesizedGate> gate_sequence;
        int32_t synthesis_time_ms_fixed;  // Fixed-point: 1000 = 1.0 ms (was double)
        size_t gate_count;
        int32_t approximation_error_fixed;  // Fixed-point: 1000 = 1.0 (was double)
        int8_t meets_latency_target;  // 0/1 instead of bool
    };

    struct HouseholderReflection {
        std::vector<int8_t> reflection_vector;
        size_t target_qutrit;
        int32_t phase_angle_fixed;  // Fixed-point: 1000 = 1.0 rad (was double)
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

    /**
     * @brief Estimate synthesis time in fixed-point
     * @param num_qutrits Number of qutrits
     * @return Estimated time in fixed-point (1000 = 1.0 ms)
     */
    int32_t estimate_synthesis_time_fixed(size_t num_qutrits) const;
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

    /**
     * @brief Compute reflection error in fixed-point
     * @return Error metric in fixed-point (scale 1000)
     */
    int32_t compute_reflection_error_fixed(
        const std::vector<int8_t>& achieved,
        const std::vector<int8_t>& target
    );
};

std::unique_ptr<HouseholderSynthesizer> create_householder_synthesizer(
    const HouseholderSynthesizer::SynthesisConfig& config
);

} // namespace q_mini_wasm_v2::core::inference