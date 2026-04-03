#pragma once

#include <vector>
#include <cstdint>
#include <memory>
#include "../ternary/trit.hpp"

namespace q_mini_wasm_v2::core::stabilizer {

/**
 * @brief Qutrit Stabilizer Tableau over GF(3)
 * 
 * Based on research: "Enhancing the QMINIWASM Framework: Integrating Qutrit 
 * Clifford Entanglement for Extreme-Edge AI"
 * 
 * The tableau tracks n qutrits using O(n²) storage complexity via the 
 * Gottesman-Knill theorem, enabling efficient classical simulation of 
 * Clifford circuits without exponential state-vector overhead.
 * 
 * Storage: 2n × 2n matrix over GF(3) + 2n phase vector
 */
class StabilizerTableau {
public:
    /**
     * @brief Construct tableau for n qutrits
     * @param num_qutrits Number of qutrits to track
     */
    explicit StabilizerTableau(size_t num_qutrits);
    
    /**
     * @brief Copy constructor
     */
    StabilizerTableau(const StabilizerTableau& other);
    
    /**
     * @brief Move constructor
     */
    StabilizerTableau(StabilizerTableau&& other) noexcept;
    
    /**
     * @brief Assignment operator
     */
    StabilizerTableau& operator=(const StabilizerTableau& other);
    
    /**
     * @brief Destructor
     */
    ~StabilizerTableau();

    // ========================================================================
    // Qutrit Clifford Gate Operations
    // ========================================================================
    
    /**
     * @brief Apply Hadamard gate to qutrit j
     * Creates discrete superposition for uniform routing dispersion
     * 
     * Update rules over GF(3):
     * - Swap X and Z blocks for column j
     * - Negate Z block for column j
     * - Update phase vector
     */
    void apply_hadamard(size_t j);
    
    /**
     * @brief Apply Phase gate (S) to qutrit j
     * Injects state-dependent phase shifts for routing entanglement
     * 
     * Update rules:
     * - Z[j] = (Z[j] + X[j]) mod 3
     * - Phase update with quadratic correction
     */
    void apply_phase(size_t j);
    
    /**
     * @brief Apply Controlled-SUM gate from control to target
     * Primary engine for MoE routing entanglement
     * 
     * Update rules:
     * - Z[target] = (Z[target] + Z[control]) mod 3
     * - X[control] = (X[control] - X[target]) mod 3
     */
    void apply_csum(size_t control, size_t target);
    
    /**
     * @brief Apply Controlled-Z gate between control and target
     * Phase-based entangling gate for alternative circuit decompositions
     * 
     * CZ = (I⊗H) × CSUM × (I⊗H) up to global phase
     * 
     * Update rules over GF(3):
     * - X[control] = (X[control] + X[target]) mod 3
     * - X[target] = (X[target] + X[control_orig]) mod 3  
     * - Phase correction for quadratic terms
     */
    void apply_cz(size_t control, size_t target);
    
    /**
     * @brief Apply Pauli X gate (shift) to qutrit j
     */
    void apply_pauli_x(size_t j);
    
    /**
     * @brief Apply Pauli Y gate to qutrit j
     */
    void apply_pauli_y(size_t j);
    
    /**
     * @brief Apply Pauli Z gate (clock) to qutrit j
     */
    void apply_pauli_z(size_t j);

    // ========================================================================
    // Measurement Operations
    // ========================================================================
    
    /**
     * @brief Measure qutrit j in computational basis
     * @return Measurement outcome (0, 1, or 2 in GF(3))
     */
    int8_t measure(size_t j);
    
    /**
     * @brief Measure all qutrits and return outcomes
     * @return Vector of measurement outcomes
     */
    std::vector<int8_t> measure_all();
    
    /**
     * @brief Compute stabilizer state overlap for routing decisions
     * Used in Forward-Forward learning goodness metric
     */
    double compute_overlap() const;

    // ========================================================================
    // State Management
    // ========================================================================
    
    /**
     * @brief Reset all qutrits to |0⟩ state
     */
    void reset();
    
    /**
     * @brief Get number of qutrits
     */
    size_t num_qutrits() const noexcept { return n_; }
    
    /**
     * @brief Check if tableau represents valid stabilizer state
     */
    bool is_valid() const;
    
    /**
     * @brief Access an element from the tableau matrix (for Entropy computations)
     */
    int8_t get_element(size_t row, size_t col) const {
        return tableau_[row][col];
    }

private:
    size_t n_;  // Number of qutrits
    
    // Tableau matrix: 2n × 2n over GF(3)
    // Top n rows: destabilizers
    // Bottom n rows: stabilizers
    std::vector<std::vector<int8_t>> tableau_;
    
    // Phase vector: 2n entries over GF(3)
    std::vector<int8_t> phase_;
    
    // ========================================================================
    // Internal Operations
    // ========================================================================
    
    /**
     * @brief Row operation: row_dest = (row_dest + row_src) mod 3
     */
    void row_add(size_t dest, size_t src);
    
    /**
     * @brief Update phase when rows are combined
     */
    void update_phase_on_row_add(size_t dest, size_t src);
    
    /**
     * @brief Compute symplectic inner product of two rows
     */
    int8_t symplectic_inner_product(size_t row1, size_t row2) const;
};

/**
 * @brief Factory function for creating initialized tableau
 */
std::unique_ptr<StabilizerTableau> create_tableau(size_t num_qutrits);

} // namespace q_mini_wasm_v2::core::stabilizer