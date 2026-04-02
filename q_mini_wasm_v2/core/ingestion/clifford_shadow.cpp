#include "clifford_shadow.hpp"
#include <random>
#include <stdexcept>
#include <algorithm>

namespace q_mini_wasm_v2::core::ingestion {

CliffordShadow::CliffordShadow(size_t num_qutrits, size_t cooling_depth, unsigned seed)
    : num_qutrits_(num_qutrits)
    , cooling_depth_(cooling_depth)
    , seed_(seed)
{
}

CliffordShadow::~CliffordShadow() = default;

std::vector<ternary::Trit> CliffordShadow::extract_snapshot(const std::vector<ternary::Trit>& input) const {
    if (input.size() != num_qutrits_) {
        throw std::invalid_argument("Input size must match the configured number of qutrits for Clifford Shadows.");
    }
    
    // Create the stabilizer tableau
    auto tableau = stabilizer::create_tableau(num_qutrits_);
    
    // Encode input into the tableau state
    for (size_t i = 0; i < num_qutrits_; ++i) {
        if (input[i] == ternary::Trit::POSITIVE) {
            tableau->apply_pauli_x(i);
        } else if (input[i] == ternary::Trit::NEGATIVE) {
            tableau->apply_pauli_x(i);
            tableau->apply_pauli_x(i);
        }
    }
    
    // Setup random generator for uniform Clifford operators
    std::mt19937 rng(seed_);
    std::uniform_int_distribution<int> gate_dist(0, 4); // 0=H, 1=S, 2=X, 3=Z, 4=CSUM
    std::uniform_int_distribution<size_t> qutrit_dist(0, num_qutrits_ - 1);
    
    // Apply entanglement cooling (random sequence of Cliffords)
    for (size_t layer = 0; layer < cooling_depth_; ++layer) {
        int gate_type = gate_dist(rng);
        size_t target1 = qutrit_dist(rng);
        
        switch (gate_type) {
            case 0:
                tableau->apply_hadamard(target1);
                break;
            case 1:
                tableau->apply_phase(target1);
                break;
            case 2:
                tableau->apply_pauli_x(target1);
                break;
            case 3:
                tableau->apply_pauli_z(target1);
                break;
            case 4: {
                size_t target2 = qutrit_dist(rng);
                if (target1 != target2) {
                    tableau->apply_csum(target1, target2);
                }
                break;
            }
        }
    }
    
    // Take measurements to form the snapshot
    auto outcomes = tableau->measure_all();
    std::vector<ternary::Trit> snapshot(num_qutrits_);
    for (size_t i = 0; i < num_qutrits_; ++i) {
        int8_t out = outcomes[i];
        if (out == 1) {
            snapshot[i] = ternary::Trit::POSITIVE;
        } else if (out == 2 || out == -1) {
            snapshot[i] = ternary::Trit::NEGATIVE;
        } else {
            snapshot[i] = ternary::Trit::ZERO;
        }
    }
    
    return snapshot;
}

} // namespace q_mini_wasm_v2::core::ingestion
