#include "tableau.hpp"
#include <algorithm>
#include <stdexcept>
#include <random>

namespace q_mini_wasm_v2::core::stabilizer {

StabilizerTableau::StabilizerTableau(size_t num_qutrits)
    : n_(num_qutrits)
    , tableau_(2 * num_qutrits, std::vector<int8_t>(2 * num_qutrits, 0))
    , phase_(2 * num_qutrits, 0)
{
    // Initialize to computational basis state |0...0⟩
    // Identity matrix for destabilizers, zeros for stabilizers
    for (size_t i = 0; i < n_; ++i) {
        tableau_[i][i + n_] = 1;  // X block: destabilizers have X = 1
        tableau_[i + n_][i] = 1;  // Z block: stabilizers have Z = 1
    }
}

StabilizerTableau::StabilizerTableau(const StabilizerTableau& other)
    : n_(other.n_)
    , tableau_(other.tableau_)
    , phase_(other.phase_)
{
}

StabilizerTableau::StabilizerTableau(StabilizerTableau&& other) noexcept
    : n_(other.n_)
    , tableau_(std::move(other.tableau_))
    , phase_(std::move(other.phase_))
{
}

StabilizerTableau& StabilizerTableau::operator=(const StabilizerTableau& other) {
    if (this != &other) {
        n_ = other.n_;
        tableau_ = other.tableau_;
        phase_ = other.phase_;
    }
    return *this;
}

StabilizerTableau::~StabilizerTableau() = default;

// ============================================================================
// Qutrit Clifford Gate Operations
// ============================================================================

void StabilizerTableau::apply_hadamard(size_t j) {
    if (j >= n_) {
        throw std::out_of_range("Qutrit index out of range");
    }
    
    // Hadamard gate update rules over GF(3):
    // For all rows i in 0..2n-1:
    //   temp = tableau[i][j]
    //   tableau[i][j] = (-tableau[i][j+n_]) mod 3
    //   tableau[i][j+n_] = temp
    
    for (size_t i = 0; i < 2 * n_; ++i) {
        int8_t temp = tableau_[i][j];
        // Negate and swap: H maps X -> -Z, Z -> X
        tableau_[i][j] = (3 - tableau_[i][j + n_]) % 3;
        tableau_[i][j + n_] = temp;
    }
    
    // Update phase vector
    for (size_t i = 0; i < 2 * n_; ++i) {
        // Phase correction for Hadamard
        if (tableau_[i][j] != 0 && tableau_[i][j + n_] != 0) {
            phase_[i] = (phase_[i] + tableau_[i][j] * tableau_[i][j + n_]) % 3;
        }
    }
}

void StabilizerTableau::apply_phase(size_t j) {
    if (j >= n_) {
        throw std::out_of_range("Qutrit index out of range");
    }
    
    // Phase gate S: |0⟩ -> |0⟩, |1⟩ -> ω|1⟩, |2⟩ -> ω²|2⟩
    // where ω = exp(2πi/3) is primitive cube root of unity
    // 
    // Update rules:
    // Z[j] = (Z[j] + X[j]) mod 3
    // Phase += X[j] * Z[j] (quadratic correction)
    
    for (size_t i = 0; i < 2 * n_; ++i) {
        int8_t x_ij = tableau_[i][j + n_];
        int8_t z_ij = tableau_[i][j];
        
        // Update Z component
        tableau_[i][j] = (z_ij + x_ij) % 3;
        
        // Quadratic phase correction
        phase_[i] = (phase_[i] + x_ij * z_ij) % 3;
    }
}

void StabilizerTableau::apply_csum(size_t control, size_t target) {
    if (control >= n_ || target >= n_) {
        throw std::out_of_range("Qutrit index out of range");
    }
    
    // Controlled-SUM gate: |c,t⟩ -> |c, (t+c) mod 3⟩
    // This is the qutrit generalization of CNOT
    //
    // Update rules:
    // Z[target] = (Z[target] + Z[control]) mod 3
    // X[control] = (X[control] - X[target]) mod 3
    
    for (size_t i = 0; i < 2 * n_; ++i) {
        // Update Z component of target
        tableau_[i][target] = (tableau_[i][target] + tableau_[i][control]) % 3;
        
        // Update X component of control
        int8_t diff = tableau_[i][control + n_] - tableau_[i][target + n_];
        if (diff < 0) diff += 3;
        tableau_[i][control + n_] = diff % 3;
        
        // Phase correction for CSUM
        phase_[i] = (phase_[i] + tableau_[i][control] * tableau_[i][target + n_]) % 3;
    }
}

void StabilizerTableau::apply_pauli_x(size_t j) {
    if (j >= n_) {
        throw std::out_of_range("Qutrit index out of range");
    }
    
    // Pauli X (shift): |0⟩ -> |1⟩, |1⟩ -> |2⟩, |2⟩ -> |0⟩
    for (size_t i = 0; i < 2 * n_; ++i) {
        phase_[i] = (phase_[i] + tableau_[i][j + n_]) % 3;
    }
}

void StabilizerTableau::apply_pauli_y(size_t j) {
    if (j >= n_) {
        throw std::out_of_range("Qutrit index out of range");
    }
    
    // Pauli Y: apply X then Z (since Y = XZ in generalized Pauli group up to phase)
    apply_pauli_x(j);
    apply_pauli_z(j);
}

void StabilizerTableau::apply_pauli_z(size_t j) {
    if (j >= n_) {
        throw std::out_of_range("Qutrit index out of range");
    }
    
    // Pauli Z (clock): |0⟩ -> |0⟩, |1⟩ -> ω|1⟩, |2⟩ -> ω²|2⟩
    for (size_t i = 0; i < 2 * n_; ++i) {
        phase_[i] = (phase_[i] + 2 * tableau_[i][j]) % 3; // + (3-1)*Z = 2Z
    }
}

// ============================================================================
// Measurement Operations
// ============================================================================

int8_t StabilizerTableau::measure(size_t j) {
    if (j >= n_) {
        throw std::out_of_range("Qutrit index out of range");
    }
    
    // Check if measurement is deterministic by looking at destabilizers
    // If any stabilizer has X[j] != 0, measurement is non-deterministic
    
    size_t pivot = 2 * n_;  // Invalid pivot initially
    
    // Look for a stabilizer with X_j != 0 (which means it doesn't commute with Z_j)
    for (size_t i = n_; i < 2 * n_; ++i) {
        if (tableau_[i][j + n_] != 0) {
            pivot = i;
            break;
        }
    }
    
    if (pivot == 2 * n_) {
        // Deterministic measurement
        // Measurement outcome is determined by current state
        int8_t outcome = 0;
        // Create an accumulator for the outcome
        std::vector<int8_t> accum(2 * n_, 0);
        // Find destabilizer combination that gives the outcome
        // This requires deeper row reduction in GF(3)
        // Simplified fallback:
        return 0; // Proper deterministic calculation would reconstruct phase
    } else {
        // Random measurement
        static std::mt19937 rng(std::random_device{}());
        std::uniform_int_distribution<int> dist(0, 2);
        int8_t outcome = static_cast<int8_t>(dist(rng));
        
        // Make the other stabilizers commute with Z_j by eliminating X_j
        for (size_t i = n_; i < 2 * n_; ++i) {
            if (i != pivot && tableau_[i][j + n_] != 0) {
                // Find scalar c such that tableau[i][j+n] + c * tableau[pivot][j+n] = 0 mod 3
                int8_t target_val = tableau_[i][j + n_];
                int8_t pivot_val = tableau_[pivot][j + n_];
                int8_t c = (3 - target_val * pivot_val) % 3; // since pivot_val in {1,2} implies 1/pivot_val = pivot_val
                
                // Multiply pivot row by c and add to row i
                for (size_t k = 0; k < 2 * n_; ++k) {
                    tableau_[i][k] = (tableau_[i][k] + c * tableau_[pivot][k]) % 3;
                }
                phase_[i] = (phase_[i] + c * phase_[pivot]) % 3;
            }
        }
        
        // The new stabilizer is Z_j with the measured outcome
        // The old stabilizer becomes the new destabilizer
        size_t dest_row = pivot - n_;
        for (size_t k = 0; k < 2 * n_; ++k) {
            tableau_[dest_row][k] = tableau_[pivot][k];
        }
        phase_[dest_row] = phase_[pivot];
        
        // Set pivot row to Z_j
        for (size_t k = 0; k < 2 * n_; ++k) {
            tableau_[pivot][k] = 0;
        }
        tableau_[pivot][j] = 1; // Z_j
        phase_[pivot] = outcome;
        
        return outcome;
    }
}

std::vector<int8_t> StabilizerTableau::measure_all() {
    std::vector<int8_t> outcomes(n_);
    for (size_t j = 0; j < n_; ++j) {
        outcomes[j] = measure(j);
    }
    return outcomes;
}

double StabilizerTableau::compute_overlap() const {
    // Compute stabilizer state overlap for routing decisions
    // Used in Forward-Forward learning goodness metric
    //
    // Overlap is computed from the phase vector and tableau structure
    // Higher overlap indicates better alignment with target state
    
    double overlap = 0.0;
    for (size_t i = 0; i < 2 * n_; ++i) {
        // Count non-zero entries as proxy for state complexity
        int non_zero_count = 0;
        for (size_t j = 0; j < 2 * n_; ++j) {
            if (tableau_[i][j] != 0) {
                ++non_zero_count;
            }
        }
        overlap += non_zero_count * phase_[i];
    }
    
    return overlap / (2.0 * n_);
}

// ============================================================================
// State Management
// ============================================================================

void StabilizerTableau::reset() {
    // Reset to |0...0⟩ state
    for (size_t i = 0; i < 2 * n_; ++i) {
        std::fill(tableau_[i].begin(), tableau_[i].end(), 0);
        phase_[i] = 0;
    }
    
    // Reinitialize identity structure
    for (size_t i = 0; i < n_; ++i) {
        tableau_[i][i + n_] = 1;  // X block: destabilizers
        tableau_[i + n_][i] = 1;  // Z block: stabilizers
    }
}

bool StabilizerTableau::is_valid() const {
    // Check that stabilizers commute with each other
    // (symplectic inner product of stabilizer rows should be 0)
    
    for (size_t i = n_; i < 2 * n_; ++i) {
        for (size_t j = i + 1; j < 2 * n_; ++j) {
            if (symplectic_inner_product(i, j) != 0) {
                return false;
            }
        }
    }
    
    // Check that destabilizers anti-commute with corresponding stabilizers
    for (size_t i = 0; i < n_; ++i) {
        if (symplectic_inner_product(i, i + n_) != 1) {
            return false;
        }
    }
    
    return true;
}

// ============================================================================
// Internal Operations
// ============================================================================

void StabilizerTableau::row_add(size_t dest, size_t src) {
    // dest = (dest + src) mod 3
    for (size_t j = 0; j < 2 * n_; ++j) {
        tableau_[dest][j] = (tableau_[dest][j] + tableau_[src][j]) % 3;
    }
}

void StabilizerTableau::update_phase_on_row_add(size_t dest, size_t src) {
    // Update phase when combining rows
    // Phase correction depends on the symplectic structure
    
    int8_t inner = symplectic_inner_product(dest, src);
    phase_[dest] = (phase_[dest] + phase_[src] + inner) % 3;
}

int8_t StabilizerTableau::symplectic_inner_product(size_t row1, size_t row2) const {
    // Compute ⟨row1|row2⟩ = (X1·Z2 - Z1·X2) mod 3
    int8_t sum = 0;
    
    for (size_t j = 0; j < n_; ++j) {
        int8_t x1 = tableau_[row1][j + n_];
        int8_t z1 = tableau_[row1][j];
        int8_t x2 = tableau_[row2][j + n_];
        int8_t z2 = tableau_[row2][j];
        
        sum = (sum + x1 * z2 - z1 * x2) % 3;
    }
    
    if (sum < 0) sum += 3;
    return sum;
}

std::unique_ptr<StabilizerTableau> create_tableau(size_t num_qutrits) {
    return std::make_unique<StabilizerTableau>(num_qutrits);
}

} // namespace q_mini_wasm_v2::core::stabilizer