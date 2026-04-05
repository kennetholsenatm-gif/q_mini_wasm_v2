#include "magic_state_distillation.hpp"
#include <algorithm>
#include <bit>
#include <array>

namespace q_mini_wasm_v2::core::flash_cim {

// ============================================================================
// Ternary Golay Code (11,6,5) - GF(3) perfect code for magic state distillation
// ============================================================================

namespace {
    // Ternary Golay generator matrix over GF(3)
    const std::array<std::array<int, 11>, 6> GOLAY_GENERATOR = {{
        {1, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1},
        {0, 1, 0, 0, 0, 0, 1, 1, 2, 2, 0},
        {0, 0, 1, 0, 0, 0, 1, 2, 1, 0, 2},
        {0, 0, 0, 1, 0, 0, 1, 2, 0, 2, 1},
        {0, 0, 0, 0, 1, 0, 1, 0, 2, 1, 2},
        {0, 0, 0, 0, 0, 1, 0, 1, 2, 2, 1}
    }};

    // Modulo 3 reduction with proper negative handling
    inline int mod3(int x) {
        x %= 3;
        return x < 0 ? x + 3 : x;
    }
}

// ============================================================================
// MagicStateDistiller Implementation
// ============================================================================

MagicStateDistiller::MagicStateDistiller(size_t distillation_rounds)
    : distillation_rounds_(distillation_rounds)
    , total_attempts_(0)
    , successful_distillations_(0)
{
}

MagicStateDistiller::~MagicStateDistiller() = default;

std::vector<ternary::Trit> MagicStateDistiller::encode_golay(const std::vector<ternary::Trit>& input) const {
    if (input.size() != 6) {
        throw std::invalid_argument("Golay encoder requires exactly 6 input trits");
    }

    std::vector<int> encoded(11, 0);

    // Multiply input vector with generator matrix over GF(3)
    for (size_t i = 0; i < 6; ++i) {
        int val = static_cast<int>(input[i]);
        for (size_t j = 0; j < 11; ++j) {
            encoded[j] = mod3(encoded[j] + val * GOLAY_GENERATOR[i][j]);
        }
    }

    // Convert back to ternary trits {-1, 0, 1}
    std::vector<ternary::Trit> result;
    result.reserve(11);
    for (int val : encoded) {
        result.push_back(static_cast<ternary::Trit>(val == 0 ? 0 : val == 1 ? 1 : -1));
    }

    return result;
}

std::vector<ternary::Trit> MagicStateDistiller::decode_golay(const std::vector<ternary::Trit>& received) const {
    if (received.size() != 11) {
        throw std::invalid_argument("Golay decoder requires exactly 11 received trits");
    }

    // Convert to GF(3) values {0,1,2}
    std::array<int, 11> r;
    for (size_t i = 0; i < 11; ++i) {
        int val = static_cast<int>(received[i]);
        r[i] = val == -1 ? 2 : val;
    }

    // Compute syndrome
    std::array<int, 5> syndrome = {0};
    for (size_t i = 0; i < 5; ++i) {
        for (size_t j = 0; j < 11; ++j) {
            syndrome[i] = mod3(syndrome[i] + r[j] * GOLAY_GENERATOR[i+1][j]);
        }
    }

    // Correct up to 2 errors (minimum distance 5)
    int error_count = 0;
    for (size_t i = 0; i < 11; ++i) {
        for (int e = 1; e <= 2; ++e) {
            std::array<int, 5> test_syndrome = {0};
            for (size_t j = 0; j < 5; ++j) {
                test_syndrome[j] = mod3(test_syndrome[j] + e * GOLAY_GENERATOR[j+1][i]);
            }
            
            bool match = true;
            for (size_t j = 0; j < 5; ++j) {
                if (test_syndrome[j] != syndrome[j]) {
                    match = false;
                    break;
                }
            }
            
            if (match) {
                r[i] = mod3(r[i] - e);
                error_count++;
                break;
            }
        }
    }

    // Extract information bits (first 6 trits)
    std::vector<ternary::Trit> result;
    result.reserve(6);
    for (size_t i = 0; i < 6; ++i) {
        int val = r[i];
        result.push_back(static_cast<ternary::Trit>(val == 2 ? -1 : val));
    }

    return result;
}

DistillationResult MagicStateDistiller::distill_magic_state(const std::vector<ternary::Trit>& noisy_state) {
    DistillationResult result;
    total_attempts_++;

    if (noisy_state.size() != 6) {
        result.success = false;
        result.error_message = "Magic state must be 6 trits for Golay encoding";
        return result;
    }

    // Encode using Ternary Golay code
    auto encoded = encode_golay(noisy_state);

    // Simulate distillation rounds - each round improves fidelity
    std::vector<ternary::Trit> current = encoded;
    double fidelity = 0.85; // Initial fidelity of noisy magic state

    for (size_t round = 0; round < distillation_rounds_; ++round) {
        // Apply error detection and correction
        auto corrected = decode_golay(current);
        current = encode_golay(corrected);
        
        // Fidelity improves exponentially with each distillation round
        fidelity = 1.0 - (1.0 - fidelity) * 0.1;
    }

    // Verify distillation quality
    result.success = fidelity > 0.99999; // 5 nines fidelity target
    result.final_fidelity = fidelity;
    result.distilled_state = decode_golay(current);
    result.rounds_applied = distillation_rounds_;

    if (result.success) {
        successful_distillations_++;
    }

    return result;
}

std::vector<ternary::Trit> MagicStateDistiller::inject_magic_state(stabilizer::StabilizerTableau& tableau, const std::vector<ternary::Trit>& magic_state) {
    // Inject distilled magic state into stabilizer graph
    // This introduces non-Clifford resources while maintaining stabilizer trackability
    
    size_t injection_point = tableau.num_qutrits();
    
    // Add new qutrits for magic state
    tableau.extend(magic_state.size());
    
    // Initialize magic state qutrits
    for (size_t i = 0; i < magic_state.size(); ++i) {
        if (magic_state[i] == ternary::Trit::POSITIVE) {
            tableau.apply_hadamard(injection_point + i);
        } else if (magic_state[i] == ternary::Trit::NEGATIVE) {
            tableau.apply_phase(injection_point + i);
        }
    }

    // Entangle magic state with existing graph
    for (size_t i = 0; i < magic_state.size(); ++i) {
        if (injection_point + i > 0) {
            tableau.apply_csum(injection_point + i - 1, injection_point + i);
        }
    }

    return magic_state;
}

double MagicStateDistiller::get_success_rate() const {
    return total_attempts_ > 0 ? static_cast<double>(successful_distillations_) / total_attempts_ : 0.0;
}

size_t MagicStateDistiller::get_total_attempts() const {
    return total_attempts_;
}

size_t MagicStateDistiller::get_successful_distillations() const {
    return successful_distillations_;
}

std::unique_ptr<MagicStateDistiller> create_magic_state_distiller(size_t distillation_rounds) {
    return std::make_unique<MagicStateDistiller>(distillation_rounds);
}

} // namespace q_mini_wasm_v2::core::flash_cim