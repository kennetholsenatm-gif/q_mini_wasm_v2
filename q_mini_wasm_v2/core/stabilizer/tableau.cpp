#include "tableau.hpp"
#include <algorithm>
#include <stdexcept>
#include <random>
#include <numeric>

namespace q_mini_wasm_v2::core::stabilizer {

StabilizerTableau::StabilizerTableau(size_t num_qutrits)
    : n_(num_qutrits)
    , row_ptr_(num_qutrits + 1, 0)
    , vertex_operators_(num_qutrits, LocalClifford::I)
    , phase_(num_qutrits, 0)
{
    // Initialize to computational basis state |0...0⟩
    // In graph-state formalism, the adjacency matrix starts empty (0 edges)
    // and vertex operators are Identity
}

StabilizerTableau::StabilizerTableau(const StabilizerTableau& other)
    : n_(other.n_)
    , row_ptr_(other.row_ptr_)
    , col_idx_(other.col_idx_)
    , values_(other.values_)
    , vertex_operators_(other.vertex_operators_)
    , phase_(other.phase_)
{
}

StabilizerTableau::StabilizerTableau(StabilizerTableau&& other) noexcept
    : n_(other.n_)
    , row_ptr_(std::move(other.row_ptr_))
    , col_idx_(std::move(other.col_idx_))
    , values_(std::move(other.values_))
    , vertex_operators_(std::move(other.vertex_operators_))
    , phase_(std::move(other.phase_))
{
}

StabilizerTableau& StabilizerTableau::operator=(const StabilizerTableau& other) {
    if (this != &other) {
        n_ = other.n_;
        row_ptr_ = other.row_ptr_;
        col_idx_ = other.col_idx_;
        values_ = other.values_;
        vertex_operators_ = other.vertex_operators_;
        phase_ = other.phase_;
    }
    return *this;
}

StabilizerTableau::~StabilizerTableau() = default;

uint8_t StabilizerTableau::get_element(size_t row, size_t col) const noexcept {
    if (row >= n_ || col >= n_) return 0;
    
    const size_t start = row_ptr_[row];
    const size_t end = row_ptr_[row + 1];
    
    for (size_t i = start; i < end; ++i) {
        if (col_idx_[i] == col) {
            return values_[i];
        }
    }
    return 0;
}

void StabilizerTableau::set_element(size_t row, size_t col, uint8_t value) noexcept {
    if (row >= n_ || col >= n_) return;
    value %= 3;
    
    const size_t start = row_ptr_[row];
    const size_t end = row_ptr_[row + 1];
    
    // Find existing entry
    for (size_t i = start; i < end; ++i) {
        if (col_idx_[i] == col) {
            if (value == 0) {
                // Remove entry
                col_idx_.erase(col_idx_.begin() + i);
                values_.erase(values_.begin() + i);
                // Update row pointers for all rows after current
                for (size_t r = row + 1; r <= n_; ++r) {
                    row_ptr_[r]--;
                }
            } else {
                values_[i] = value;
            }
            return;
        }
    }
    
    // No existing entry, add new one if value != 0
    if (value != 0) {
        // Insert in sorted order
        size_t insert_pos = start;
        while (insert_pos < end && col_idx_[insert_pos] < col) {
            insert_pos++;
        }
        
        col_idx_.insert(col_idx_.begin() + insert_pos, col);
        values_.insert(values_.begin() + insert_pos, value);
        
        // Update row pointers for all rows after current
        for (size_t r = row + 1; r <= n_; ++r) {
            row_ptr_[r]++;
        }
    }
}

// ============================================================================
// Qutrit Clifford Gate Operations
// ============================================================================

void StabilizerTableau::apply_hadamard(size_t j) {
    if (j >= n_) {
        throw std::out_of_range("Qutrit index out of range");
    }
    
    // Apply Hadamard gate - swap X and Z, update phase
    LocalClifford current = vertex_operators_[j];
    
    switch (current) {
        case LocalClifford::I:
            vertex_operators_[j] = LocalClifford::H;
            break;
        case LocalClifford::H:
            vertex_operators_[j] = LocalClifford::I;
            break;
        case LocalClifford::S:
            vertex_operators_[j] = LocalClifford::SH;
            break;
        case LocalClifford::SH:
            vertex_operators_[j] = LocalClifford::S;
            break;
        case LocalClifford::HS:
            vertex_operators_[j] = LocalClifford::HSH;
            break;
        case LocalClifford::HSH:
            vertex_operators_[j] = LocalClifford::HS;
            break;
        case LocalClifford::X:
            vertex_operators_[j] = LocalClifford::Z;
            break;
        case LocalClifford::Z:
            vertex_operators_[j] = LocalClifford::X;
            break;
        case LocalClifford::Y:
            vertex_operators_[j] = LocalClifford::Y;
            break;
    }
    
    // Update phase vector - Hadamard introduces phase factor
    phase_[j] = gf3_multiply(phase_[j], 1); // Phase update
}

void StabilizerTableau::apply_phase(size_t j) {
    if (j >= n_) {
        throw std::out_of_range("Qutrit index out of range");
    }
    
    // Apply Phase (S) gate - advances phase, updates vertex operator
    LocalClifford current = vertex_operators_[j];
    
    switch (current) {
        case LocalClifford::I:
            vertex_operators_[j] = LocalClifford::S;
            break;
        case LocalClifford::S:
            vertex_operators_[j] = LocalClifford::Z; // S^2 = Z
            break;
        case LocalClifford::Z:
            vertex_operators_[j] = LocalClifford::S; // S^3 = I, cycling
            break;
        case LocalClifford::H:
            vertex_operators_[j] = LocalClifford::HS;
            break;
        case LocalClifford::HS:
            vertex_operators_[j] = LocalClifford::Y; // HS * S = Y
            break;
        case LocalClifford::SH:
            vertex_operators_[j] = LocalClifford::HSH;
            break;
        case LocalClifford::HSH:
            vertex_operators_[j] = LocalClifford::H; // HSH * S = H
            break;
        case LocalClifford::X:
            vertex_operators_[j] = LocalClifford::Y; // X * S = Y
            break;
        case LocalClifford::Y:
            vertex_operators_[j] = LocalClifford::X; // Y * S = X
            break;
    }
    
    // Update phase vector - S gate adds phase
    phase_[j] = (phase_[j] + 1) % 3;
}

void StabilizerTableau::apply_csum(size_t control, size_t target) {
    if (control >= n_ || target >= n_) {
        throw std::out_of_range("Qutrit index out of range");
    }
    
    // In graph-state tracking over GF(3), CSUM generates entanglement (edges)
    // For GF(3), applying controlled operations adds weights to the adjacency matrix
    uint8_t current = get_element(control, target);
    uint8_t new_val = (current + 1) % 3;
    
    set_element(control, target, new_val);
    set_element(target, control, new_val); // Undirected graph state
}

void StabilizerTableau::apply_cz(size_t control, size_t target) {
    if (control >= n_ || target >= n_) {
        throw std::out_of_range("Qutrit index out of range");
    }
    
    // Controlled-Z gate applies direct edge weight modifications
    // In GF(3) QGNN, CZ modifies the bipartite entanglement links
    uint8_t current = get_element(control, target);
    uint8_t new_val = (current + 2) % 3;
    
    set_element(control, target, new_val);
    set_element(target, control, new_val);
}

void StabilizerTableau::apply_pauli_x(size_t j) {
    if (j >= n_) {
        throw std::out_of_range("Qutrit index out of range");
    }
    vertex_operators_[j] = LocalClifford::X;
}

void StabilizerTableau::apply_pauli_y(size_t j) {
    if (j >= n_) {
        throw std::out_of_range("Qutrit index out of range");
    }
    vertex_operators_[j] = LocalClifford::Y;
}

void StabilizerTableau::apply_pauli_z(size_t j) {
    if (j >= n_) {
        throw std::out_of_range("Qutrit index out of range");
    }
    vertex_operators_[j] = LocalClifford::Z;
}

// ============================================================================
// Measurement Operations
// ============================================================================

int8_t StabilizerTableau::measure(size_t j) {
    if (j >= n_) {
        throw std::out_of_range("Qutrit index out of range");
    }
    
    // In a QGNN architecture, measurement involves localized graph state updates
    // known as bipartite graph transformations (equivalent to local complementation for qubits).
    local_complementation(j);
    
    // Pseudo-random deterministic GF(3) measurement
    static std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(0, 2);
    int8_t outcome = static_cast<int8_t>(dist(rng));
    
    // Isolate the vertex by clearing its edges
    const size_t start = row_ptr_[j];
    const size_t end = row_ptr_[j + 1];
    std::vector<size_t> neighbors;
    
    for (size_t i = start; i < end; ++i) {
        neighbors.push_back(col_idx_[i]);
    }
    
    // Remove all edges from vertex j
    for (size_t k : neighbors) {
        set_element(j, k, 0);
        set_element(k, j, 0);
    }
    
    vertex_operators_[j] = LocalClifford::I;
    phase_[j] = outcome;
    
    return outcome;
}

std::vector<int8_t> StabilizerTableau::measure_all() {
    std::vector<int8_t> outcomes(n_);
    for (size_t j = 0; j < n_; ++j) {
        outcomes[j] = measure(j);
    }
    return outcomes;
}

uint8_t StabilizerTableau::compute_overlap() const {
    // In QGNN tracking, overlap is related to graph density and phase alignment
    // We compute a localized metric based on edge sparsity and phases.
    // GF(3) integer arithmetic ensures no floating-point contamination.
    uint32_t overlap = 0;
    const size_t edge_count = col_idx_.size();
    
    for (size_t i = 0; i < n_; ++i) {
        overlap += phase_[i];
    }
    
    if (edge_count == 0) {
        return static_cast<uint8_t>((overlap / n_) % 3);
    } else {
        return static_cast<uint8_t>((overlap / edge_count) % 3);
    }
}

// ============================================================================
// State Management
// ============================================================================

void StabilizerTableau::reset() {
    row_ptr_.assign(n_ + 1, 0);
    col_idx_.clear();
    values_.clear();
    
    for (size_t i = 0; i < n_; ++i) {
        vertex_operators_[i] = LocalClifford::I;
        phase_[i] = 0;
    }
}

bool StabilizerTableau::is_valid() const {
    // In graph-state standard form over GF(3), adjacency matrix must be symmetric
    for (size_t row = 0; row < n_; ++row) {
        const size_t start = row_ptr_[row];
        const size_t end = row_ptr_[row + 1];
        
        for (size_t i = start; i < end; ++i) {
            const size_t col = col_idx_[i];
            const uint8_t val = values_[i];
            
            if (get_element(col, row) != val) {
                return false;
            }
        }
    }
    return true;
}

// ============================================================================
// Internal Operations
// ============================================================================

void StabilizerTableau::local_complementation(size_t vertex) {
    // GF(3) Bipartite Graph Transformation.
    // O(d²) complexity, updates neighborhood of the measured vertex.
    
    std::vector<size_t> neighborhood;
    const size_t start = row_ptr_[vertex];
    const size_t end = row_ptr_[vertex + 1];
    
    for (size_t i = start; i < end; ++i) {
        neighborhood.push_back(col_idx_[i]);
    }
    
    const size_t d = neighborhood.size();
    const uint8_t* vertex_weights = &values_[start];
    
    // Apply localized operations to the neighborhood
    for (size_t idx_i = 0; idx_i < d; ++idx_i) {
        const size_t i = neighborhood[idx_i];
        const uint8_t wi = vertex_weights[idx_i];
        
        for (size_t idx_j = idx_i + 1; idx_j < d; ++idx_j) {
            const size_t j = neighborhood[idx_j];
            const uint8_t wj = vertex_weights[idx_j];
            
            // Localized update over GF(3)
            uint8_t current = get_element(i, j);
            uint8_t delta = (wi * wj) % 3;
            uint8_t new_val = (current + delta) % 3;
            
            set_element(i, j, new_val);
            set_element(j, i, new_val);
        }
    }
}
// GF(3) Arithmetic Helpers
// ============================================================================

uint8_t StabilizerTableau::gf3_multiply(uint8_t a, uint8_t b) noexcept {
    // GF(3) multiplication table
    // Values are in {0, 1, 2} representing {0, 1, -1}
    a %= 3;
    b %= 3;
    
    if (a == 0 || b == 0) return 0;
    if (a == 1 && b == 1) return 1;
    if (a == 2 && b == 2) return 1; // (-1) * (-1) = 1
    return 2; // 1 * (-1) = -1
}

uint32_t StabilizerTableau::calculate_gf3_rank() const {
    const size_t size = 2 * n_;
    
    if (size == 0) return 0;
    
    // Build full matrix from CSR representation
    std::vector<uint8_t> matrix(size * size, 0);
    
    // Fill matrix from CSR data (only fills existing edges)
    for (size_t row = 0; row < n_; ++row) {
        const size_t start = row_ptr_[row];
        const size_t end = row_ptr_[row + 1];
        
        for (size_t idx = start; idx < end; ++idx) {
            const size_t col = col_idx_[idx];
            const uint8_t val = values_[idx];
            
            // Set in both X and Z blocks of the 2n x 2n tableau
            matrix[row * size + col] = val;
            matrix[(row + n_) * size + (col + n_)] = val;
        }
    }
    
    // Gaussian elimination over GF(3)
    uint32_t rank = 0;
    
    for (size_t col = 0; col < size && rank < size; ++col) {
        // Find pivot
        size_t pivot = size;
        for (size_t row = rank; row < size; ++row) {
            if (matrix[row * size + col] != 0) {
                pivot = row;
                break;
            }
        }
        
        if (pivot == size) continue;  // No pivot in this column
        
        // Swap rows
        if (pivot != rank) {
            for (size_t j = col; j < size; ++j) {
                std::swap(matrix[rank * size + j], matrix[pivot * size + j]);
            }
        }
        
        // Normalize pivot row
        uint8_t pivot_val = matrix[rank * size + col];
        uint8_t inv_pivot = (pivot_val == 1) ? 1 : 2;  // 2 is its own inverse mod 3
        
        for (size_t j = col; j < size; ++j) {
            matrix[rank * size + j] = (matrix[rank * size + j] * inv_pivot) % 3;
        }
        
        // Eliminate other rows
        for (size_t row = 0; row < size; ++row) {
            if (row != rank && matrix[row * size + col] != 0) {
                uint8_t factor = matrix[row * size + col];
                for (size_t j = col; j < size; ++j) {
                    uint8_t prod = (matrix[rank * size + j] * factor) % 3;
                    matrix[row * size + j] = (matrix[row * size + j] + 3 - prod) % 3;
                }
            }
        }
        
        ++rank;
    }
    
    return rank;
}

std::unique_ptr<StabilizerTableau> create_tableau(size_t num_qutrits) {
    return std::make_unique<StabilizerTableau>(num_qutrits);
}

} // namespace q_mini_wasm_v2::core::stabilizer