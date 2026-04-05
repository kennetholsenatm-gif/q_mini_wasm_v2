#pragma once

#include <vector>
#include <memory>
#include "../ternary/trit.hpp"
#include "../stabilizer/stabilizer_tableau.hpp"

namespace q_mini_wasm_v2::core::flash_cim {

struct DistillationResult {
    bool success;
    double final_fidelity;
    size_t rounds_applied;
    std::vector<ternary::Trit> distilled_state;
    std::string error_message;
};

class MagicStateDistiller {
public:
    explicit MagicStateDistiller(size_t distillation_rounds = 3);
    ~MagicStateDistiller();

    std::vector<ternary::Trit> encode_golay(const std::vector<ternary::Trit>& input) const;
    std::vector<ternary::Trit> decode_golay(const std::vector<ternary::Trit>& received) const;
    
    DistillationResult distill_magic_state(const std::vector<ternary::Trit>& noisy_state);
    std::vector<ternary::Trit> inject_magic_state(stabilizer::StabilizerTableau& tableau, const std::vector<ternary::Trit>& magic_state);

    double get_success_rate() const;
    size_t get_total_attempts() const;
    size_t get_successful_distillations() const;

private:
    size_t distillation_rounds_;
    size_t total_attempts_;
    size_t successful_distillations_;
};

std::unique_ptr<MagicStateDistiller> create_magic_state_distiller(size_t distillation_rounds = 3);

} // namespace q_mini_wasm_v2::core::flash_cim