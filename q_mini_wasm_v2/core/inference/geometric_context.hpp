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
        int32_t manifold_tolerance_fixed;  // Fixed-point: 1000 = 1.0 (was double)
        int8_t enable_manifold_normalization;  // 0/1 instead of bool
    };

    struct Multivector {
        std::vector<int32_t> scalar_fixed;  // Fixed-point components (was double)
        std::vector<int32_t> vector_fixed;
        std::vector<int32_t> bivector_fixed;
        int32_t norm_fixed;  // Fixed-point: 1000 = 1.0 (was double)
    };

    struct GeometricState {
        std::vector<Multivector> sequence;
        size_t current_length;
        int32_t total_geometric_norm_fixed;  // Fixed-point (was double)
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
    
    /**
     * @brief Compute context similarity in fixed-point
     * @return Similarity score in fixed-point (scale 1000)
     */
    int32_t compute_context_similarity_fixed(const GeometricState& state, size_t i, size_t j);
    
    /**
     * @brief Extract context vector as fixed-point values
     * @return Fixed-point vector components (scale 1000)
     */
    std::vector<int32_t> extract_context_vector_fixed(const GeometricState& state, size_t position);

    std::vector<Multivector> tokens_to_multivectors(
        const std::vector<std::vector<ternary::Trit>>& tokens
    );

    const ContextConfig& config() const { return config_; }

private:
    ContextConfig config_;

    Multivector create_multivector(const std::vector<ternary::Trit>& trits);
    void normalize_multivector(Multivector& mv);
    
    /**
     * @brief Compute norm in fixed-point
     * @return Norm in fixed-point (scale 1000)
     */
    int32_t compute_norm_fixed(const Multivector& mv);
};

std::unique_ptr<GeometricContextWindow> create_geometric_context(
    const GeometricContextWindow::ContextConfig& config
);

} // namespace q_mini_wasm_v2::core::inference