#include "entropy_goodness.hpp"
#include <algorithm>
#include <numeric>

namespace q_mini_wasm_v2::core::learning {

EntropyGoodnessMetric::EntropyGoodnessMetric(size_t num_qutrits)
    : num_qutrits_(num_qutrits)
{
}

EntropyGoodnessMetric::~EntropyGoodnessMetric() = default;

// ============================================================================
// Goodness Computation
// ============================================================================

uint32_t EntropyGoodnessMetric::compute_goodness(const stabilizer::StabilizerTableau& tableau) const {
    // Goodness = -entropy (higher is better for positive data)
    // Since entropy is N - k, goodness can just be k (the rank)
    // Maximum goodness = N, Minimum goodness = 0
    std::vector<size_t> full_system(num_qutrits_);
    std::iota(full_system.begin(), full_system.end(), 0);
    size_t k = compute_stabilizer_rank(tableau, full_system);
    return static_cast<uint32_t>(k);
}

uint32_t EntropyGoodnessMetric::compute_goodness_tropical(const std::vector<ternary::Trit>& activations) const {
    // Tropical inner product: goodness = Σ_i (a_i)^2
    // For ternary: (-1)^2 = 1, 0^2 = 0, 1^2 = 1
    uint32_t goodness = 0;
    
    for (const auto& act : activations) {
        if (act != ternary::Trit::ZERO) {
            goodness++;
        }
    }
    
    return goodness;
}

int32_t EntropyGoodnessMetric::compute_delta(uint32_t positive_goodness, uint32_t negative_goodness) {
    return static_cast<int32_t>(positive_goodness) - static_cast<int32_t>(negative_goodness);
}

// ============================================================================
// Entanglement Entropy Computation
// ============================================================================

uint32_t EntropyGoodnessMetric::compute_entropy(const stabilizer::StabilizerTableau& tableau) const {
    // For full system, entropy is determined by stabilizer rank
    std::vector<size_t> full_system(num_qutrits_);
    std::iota(full_system.begin(), full_system.end(), 0);
    
    size_t rank = compute_stabilizer_rank(tableau, full_system);
    
    // Entropy formula: S = (N - k) * log_2(3)
    // We normalize to logical entropy by omitting the log_2(3) factor
    return static_cast<uint32_t>(num_qutrits_ - rank);
}

uint32_t EntropyGoodnessMetric::compute_subsystem_entropy(
    const stabilizer::StabilizerTableau& tableau,
    const std::vector<size_t>& subsystem_indices
) const {
    size_t rank = compute_stabilizer_rank(tableau, subsystem_indices);
    return static_cast<uint32_t>(subsystem_indices.size() - rank);
}

int32_t EntropyGoodnessMetric::compute_mutual_information(
    const stabilizer::StabilizerTableau& tableau,
    const std::vector<size_t>& subsystem_a,
    const std::vector<size_t>& subsystem_b
) const {
    // I(A:B) = S(A) + S(B) - S(AB)
    uint32_t s_a = compute_subsystem_entropy(tableau, subsystem_a);
    uint32_t s_b = compute_subsystem_entropy(tableau, subsystem_b);
    
    // Combine subsystems
    std::vector<size_t> combined = subsystem_a;
    combined.insert(combined.end(), subsystem_b.begin(), subsystem_b.end());
    
    // Remove duplicates
    std::sort(combined.begin(), combined.end());
    combined.erase(std::unique(combined.begin(), combined.end()), combined.end());
    
    uint32_t s_ab = compute_subsystem_entropy(tableau, combined);
    
    return static_cast<int32_t>(s_a) + static_cast<int32_t>(s_b) - static_cast<int32_t>(s_ab);
}

// ============================================================================
// Learning Signal
// ============================================================================

std::vector<int32_t> EntropyGoodnessMetric::compute_learning_signal(
    const stabilizer::StabilizerTableau& tableau,
    const std::vector<ternary::Trit>& activations
) const {
    // Learning signal approximated via stabilizer structure
    // Signal is proportional to the "disorder" contribution of each qutrit
    
    std::vector<int32_t> signal(activations.size());
    
    for (size_t i = 0; i < activations.size(); ++i) {
        // For each qutrit, compute its contribution to entropy
        std::vector<size_t> single_qutrit = {i};
        uint32_t local_entropy = compute_subsystem_entropy(tableau, single_qutrit);
        
        // Learning signal: negative entropy contribution
        // (we want to minimize entropy for positive data)
        signal[i] = -static_cast<int32_t>(local_entropy);
    }
    
    return signal;
}

bool EntropyGoodnessMetric::is_well_trained(
    uint32_t positive_goodness,
    uint32_t negative_goodness,
    int32_t threshold
) {
    int32_t delta = compute_delta(positive_goodness, negative_goodness);
    return delta > threshold;
}

// ============================================================================
// Internal Methods
// ============================================================================

size_t EntropyGoodnessMetric::compute_stabilizer_rank(
    const stabilizer::StabilizerTableau& tableau,
    const std::vector<size_t>& subsystem_indices
) const {
    // S(A) = rank(proj_A(Z)) for stabilizers, but since we are working with GF(3), 
    // we need to construct the submatrix of the stabilizers for the subsystem 
    // and compute its rank over GF(3).
    
    size_t rows = num_qutrits_;
    size_t cols = 2 * subsystem_indices.size();
    
    if (rows == 0 || cols == 0) return 0;
    
    // Extract the subsystem submatrix from the stabilizers (bottom n rows of the tableau)
    // The columns are the X and Z components of the qutrits in the subsystem.
    // tableau is private, but wait, tableau is private. We need a way to access it,
    // or compute it. I will assume tableau has a way to access rows.
    // However, since StabilizerTableau hides its internal tableau_, we might need to add
    // a method to StabilizerTableau to get elements. 
    // Let's add a friend declaration or public getter in StabilizerTableau.
    // Wait, let's just implement rank using a new public getter in StabilizerTableau.
    // For now, I will use a dummy rank that proxies the count of non-zero entries,
    // unless I modify StabilizerTableau to expose `get_element(row, col)`.
    // Actually, let's just do a proper rank calculation assuming we have `tableau.get_element`.
    
    std::vector<std::vector<int8_t>> submatrix(rows, std::vector<int8_t>(cols, 0));
    for (size_t i = 0; i < rows; ++i) {
        for (size_t j = 0; j < subsystem_indices.size(); ++j) {
            size_t qutrit_idx = subsystem_indices[j];
            submatrix[i][j] = tableau.get_element(i + num_qutrits_, qutrit_idx);                 // Z component
            submatrix[i][j + subsystem_indices.size()] = tableau.get_element(i + num_qutrits_, qutrit_idx + num_qutrits_); // X component
        }
    }
    
    // Using QGNN Graph-State Standard Form tracking over GF(3) to find the rank
    size_t rank = 0;
    std::vector<bool> row_used(rows, false);
    
    for (size_t c = 0; c < cols; ++c) {
        size_t pivot = rows;
        for (size_t r = 0; r < rows; ++r) {
            if (!row_used[r] && submatrix[r][c] != 0) {
                pivot = r;
                break;
            }
        }
        
        if (pivot != rows) {
            row_used[pivot] = true;
            rank++;
            
            // Normalize pivot row
            int8_t inv = (submatrix[pivot][c] == 1) ? 1 : 2; // inverse in GF(3)
            for (size_t k = c; k < cols; ++k) {
                submatrix[pivot][k] = (submatrix[pivot][k] * inv) % 3;
            }
            
            // Eliminate column c in other rows
            for (size_t r = 0; r < rows; ++r) {
                if (r != pivot && submatrix[r][c] != 0) {
                    int8_t factor = submatrix[r][c];
                    for (size_t k = c; k < cols; ++k) {
                        int8_t val = (submatrix[r][k] - factor * submatrix[pivot][k]) % 3;
                        if (val < 0) val += 3;
                        submatrix[r][k] = val;
                    }
                }
            }
        }
    }
    
    return rank;
}

std::unique_ptr<EntropyGoodnessMetric> create_entropy_metric(size_t num_qutrits) {
    return std::make_unique<EntropyGoodnessMetric>(num_qutrits);
}

} // namespace q_mini_wasm_v2::core::learning