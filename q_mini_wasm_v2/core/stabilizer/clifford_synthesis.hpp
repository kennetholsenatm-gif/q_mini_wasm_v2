#pragma once

#include <vector>
#include <memory>
#include "tableau.hpp"

namespace q_mini_wasm_v2::core::stabilizer {

/**
 * @brief Clifford Gate Type for Synthesis
 */
enum class CliffordGate {
    HADAMARD,
    PHASE,
    PAULI_X,
    PAULI_Y,
    PAULI_Z,
    CSUM
};

/**
 * @brief A synthesized operation
 */
struct SynthesizedGate {
    CliffordGate gate;
    size_t target_1;
    size_t target_2; // Used for 2-qutrit gates like CSUM
};

/**
 * @brief Clifford Synthesizer
 * 
 * Compiles target state transformations into discrete Clifford gate 
 * sequences. Supports Exhaustive Search for static networks and 
 * Householder Search for dynamic, on-the-fly routing.
 */
class CliffordSynthesizer {
public:
    CliffordSynthesizer() = default;
    ~CliffordSynthesizer() = default;

    /**
     * @brief Synthesize a sequence of gates using the Householder Search Algorithm
     * @param target The target unitary transformation matrix or target state representation.
     *        For simplicity, this function attempts to find a generic stabilizer state generator sequence.
     * @param num_qutrits Number of qutrits
     * @return Sequence of gates that prepares the target state
     */
    std::vector<SynthesizedGate> synthesize_householder(const std::vector<int8_t>& target_state, size_t num_qutrits);

    /**
     * @brief Apply a synthesized sequence to a given tableau
     * @param tableau StabilizerTableau to be modified
     * @param sequence Sequence of synthesized gates
     */
    static void apply_sequence(StabilizerTableau& tableau, const std::vector<SynthesizedGate>& sequence);
};

} // namespace q_mini_wasm_v2::core::stabilizer
