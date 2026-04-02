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

double EntropyGoodnessMetric::compute_goodness(const stabilizer::StabilizerTableau& tableau) const {
    // Goodness = -entropy (higher is better for positive data)
    double entropy = compute_entropy(tableau);
    return -entropy;
}

double EntropyGoodnessMetric::compute_goodness_tropical(const std::vector<ternary::Trit>& activations) const {
    // Tropical inner product: goodness = Σ_i (a_i)^2
    // For ternary: (-1)^2 = 1, 0^2 = 0, 1^2 = 1
    double goodness = 0.0;
    
    for (const auto& act : activations) {
        double val = static_cast<double>(act);
        goodness += val * val;
    }
    
    return goodness;
}

double EntropyGoodnessMetric::compute_delta(double positive_goodness, double negative_goodness) {
    return positive_goodness - negative_goodness;
}

// ============================================================================
// Entanglement Entropy Computation
// ============================================================================

double EntropyGoodnessMetric::compute_entropy(const stabilizer::StabilizerTableau& tableau) const {
    // For full system, entropy is determined by stabilizer rank
    std::vector<size_t> full_system(num_qutrits_);
    std::iota(full_system.begin(), full_system.end(), 0);
    
    size_t rank = compute_stabilizer_rank(tableau, full_system);
    
    // Entropy = k * log(d) where d = 3 for qutrits
    // For pure stabilizer states, full system entropy is typically 0
    // We compute the "mixedness" based on stabilizer structure
    return rank * std::log(3.0);
}

double EntropyGoodnessMetric::compute_subsystem_entropy(
    const stabilizer::StabilizerTableau& tableau,
    const std::vector<size_t>& subsystem_indices
) const {
    size_t rank = compute_stabilizer_rank(tableau, subsystem_indices);
    return rank * std::log(3.0);
}

double EntropyGoodnessMetric::compute_mutual_information(
    const stabilizer::StabilizerTableau& tableau,
    const std::vector<size_t>& subsystem_a,
    const std::vector<size_t>& subsystem_b
) const {
    // I(A:B) = S(A) + S(B) - S(AB)
    double s_a = compute_subsystem_entropy(tableau, subsystem_a);
    double s_b = compute_subsystem_entropy(tableau, subsystem_b);
    
    // Combine subsystems
    std::vector<size_t> combined = subsystem_a;
    combined.insert(combined.end(), subsystem_b.begin(), subsystem_b.end());
    
    // Remove duplicates
    std::sort(combined.begin(), combined.end());
    combined.erase(std::unique(combined.begin(), combined.end()), combined.end());
    
    double s_ab = compute_subsystem_entropy(tableau, combined);
    
    return s_a + s_b - s_ab;
}

// ============================================================================
// Learning Signal
// ============================================================================

std::vector<double> EntropyGoodnessMetric::compute_learning_signal(
    const stabilizer::StabilizerTableau& tableau,
    const std::vector<ternary::Trit>& activations
) const {
    // Learning signal approximated via stabilizer structure
    // Signal is proportional to the "disorder" contribution of each qutrit
    
    std::vector<double> signal(activations.size());
    
    for (size_t i = 0; i < activations.size(); ++i) {
        // For each qutrit, compute its contribution to entropy
        std::vector<size_t> single_qutrit = {i};
        double local_entropy = compute_subsystem_entropy(tableau, single_qutrit);
        
        // Learning signal: negative entropy contribution
        // (we want to minimize entropy for positive data)
        signal[i] = -local_entropy;
    }
    
    return signal;
}

bool EntropyGoodnessMetric::is_well_trained(
    double positive_goodness,
    double negative_goodness,
    double threshold
) {
    double delta = compute_delta(positive_goodness, negative_goodness);
    return delta > threshold;
}

// ============================================================================
// Internal Methods
// ============================================================================

size_t EntropyGoodnessMetric::compute_stabilizer_rank(
    const stabilizer::StabilizerTableau& tableau,
    const std::vector<size_t>& subsystem_indices
) const {
    // Simplified implementation: count stabilizers that act non-trivially
    // on the specified subsystem
    
    // For a stabilizer state, the entropy of subsystem A is:
    // S(A) = |A| - rank(G_A)
    // where G_A is the subgroup of stabilizers that act trivially on complement of A
    
    // In practice, we count the number of independent stabilizer generators
    // that have non-trivial support on the subsystem
    
    size_t rank = 0;
    
    // This is a simplified version - full implementation would analyze
    // the stabilizer tableau structure more carefully
    
    // For now, return a proxy based on tableau validity
    if (tableau.is_valid()) {
        rank = subsystem_indices.size();
    }
    
    return rank;
}

std::unique_ptr<EntropyGoodnessMetric> create_entropy_metric(size_t num_qutrits) {
    return std::make_unique<EntropyGoodnessMetric>(num_qutrits);
}

} // namespace q_mini_wasm_v2::core::learning