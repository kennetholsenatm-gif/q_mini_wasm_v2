#pragma once

#include <vector>
#include <cstdint>
#include <memory>
#include "../ternary/trit.hpp"
#include "../stabilizer/tableau.hpp"

namespace q_mini_wasm_v2::core::inference {

/**
 * @brief Geometric Context Window using Clifford Algebra
 * 
 * Implements O(n) linear complexity context windows via Geometric Product.
 * Replaces quadratic attention with multivector interactions.
 */
class GeometricContextWindow {
public:
    struct ContextConfig {
        size_t max_context_length;
        size_t multivector_dim;
        double manifold_tolerance;
        bool enable_manifold_normalization;
    };

    struct Multivector {
        std::vector<double> scalar;
        std::vector<double> vector;
        std::vector<double> bivector;
        double norm;
    };

    struct GeometricState {
        std::vector<Multivector> sequence;
        size_t current_length;
        double total_geometric_norm;
    };

    explicit GeometricContextWindow(const ContextConfig& config);
    ~GeometricContextWindow();

    GeometricState initialize_state(size_t sequence_length);
    
    Multivector geometric_product(const Multivector& a, const Multivector& b);
    Multivector inner_product(const Multivector& a, const Multivector& b);
    Multivector wedge_product(const Multivector& a, const Multivector& b);

    GeometricState update_context(
        const GeometricState& state,
        const Multivector& new_token
    );

    GeometricState apply_manifold_normalization(const GeometricState& state);
    double compute_context_similarity(const GeometricState& state, size_t i, size_t j);
    std::vector<double> extract_context_vector(const GeometricState& state, size_t position);

    std::vector<Multivector> tokens_to_multivectors(
        const std::vector<std::vector<ternary::Trit>>& tokens
    );

    const ContextConfig& config() const { return config_; }

private:
    ContextConfig config_;

    Multivector create_multivector(const std::vector<ternary::Trit>& trits);
    void normalize_multivector(Multivector& mv);
    double compute_norm(const Multivector& mv);
};

std::unique_ptr<GeometricContextWindow> create_geometric_context(
    const GeometricContextWindow::ContextConfig& config
);

} // namespace q_mini_wasm_v2::core::inference