#pragma once

#include <vector>
#include <cstdint>
#include <memory>
#include <array>
#include "../ternary/trit.hpp"
#include "../stabilizer/tableau.hpp"

namespace q_mini_wasm_v2::core::qgnn {

/**
 * @brief Quantum Betti Number extractor using stabilizer tableau
 * 
 * Computes Betti numbers β₀ (connected components), β₁ (cycles), β₂ (voids)
 * via GF(3) rank calculation on the stabilizer tableau.
 * 
 * Energy budget: <0.5 pJ/op via L1 cache LUTs
 * Complexity: O(n²) via Gottesman-Knill theorem
 * No floating-point operations - pure GF(3) discrete math
 */
class BettiExtractor {
public:
    /**
     * @brief Simplicial complex representation for TDA
     * 
     * Maps classical simplicial complex to quantum stabilizer code:
     * - Edges → Qutrits (TritPack5 for memory efficiency)
     * - Vertices → X-stabilizers
     * - Faces → Z-stabilizers
     */
    struct SimplicialComplex {
        std::vector<ternary::TritPack5> edges;      // Edge list as packed trits
        std::vector<uint32_t> vertices;              // Vertex indices
        std::vector<std::array<uint32_t, 3>> faces;  // Triangular faces (optional)
        
        size_t num_edges() const { return edges.size(); }
        size_t num_vertices() const { return vertices.size(); }
        size_t num_faces() const { return faces.size(); }
    };
    
    /**
     * @brief Betti numbers representing topological features
     * 
     * β₀: Number of connected components
     * β₁: Number of 1-cycles/holes (cyclomatic number)
     * β₂: Number of 2-voids (for 3D complexes)
     */
    struct BettiNumbers {
        uint32_t beta_0;  // Connected components
        uint32_t beta_1;  // 1-cycles/holes
        uint32_t beta_2;  // 2-voids (if 3D)
        ternary::EnergyTrit energy_cost;  // Measured in ternary energy units
        
        uint32_t euler_characteristic() const {
            return beta_0 - beta_1 + beta_2;
        }
    };
    
    /**
     * @brief Construct BettiExtractor for complexes up to max_qutrits edges
     * @param max_qutrits Maximum number of edges (qutrits) to support
     */
    explicit BettiExtractor(size_t max_qutrits);
    
    /**
     * @brief Destructor
     */
    ~BettiExtractor();
    
    /**
     * @brief Map simplicial complex to stabilizer tableau
     * 
     * Converts the simplicial complex into a stabilizer tableau by:
     * - Creating X-stabilizers for each vertex (edge boundaries)
     * - Creating Z-stabilizers for each face (edge coboundaries)
     * 
     * @param complex Simplicial complex to load
     */
    void load_complex(const SimplicialComplex& complex);
    
    /**
     * @brief Compute Betti numbers via tableau rank
     * 
     * Uses GF(3) rank calculation to determine Betti numbers:
     * - β₀ = dim(ker ∂₀) - dim(im ∂₁)  [connected components]
     * - β₁ = dim(ker ∂₁) - dim(im ∂₂)  [1-cycles]
     * - β₂ = dim(ker ∂₂)               [2-voids]
     * 
     * @return BettiNumbers structure with computed values
     */
    BettiNumbers compute_betti() const;
    
    /**
     * @brief Energy-aware computation (aborts if budget exceeded)
     * 
     * @param max_energy Maximum energy budget for computation
     * @return BettiNumbers or throws if budget exceeded
     */
    BettiNumbers compute_betti_with_budget(ternary::EnergyTrit max_energy) const;
    
    /**
     * @brief Get current stabilizer tableau (for debugging)
     * @return Const reference to internal tableau
     */
    const stabilizer::StabilizerTableau& tableau() const { return *tableau_; }
    
    /**
     * @brief Compute rank of current tableau over GF(3)
     * 
     * Uses Gaussian elimination over GF(3) to compute matrix rank.
     * This is the core operation for Betti number extraction.
     * 
     * @return Rank of the 2n×2n tableau matrix over GF(3)
     */
    uint32_t calculate_gf3_rank() const;
    
private:
    std::unique_ptr<stabilizer::StabilizerTableau> tableau_;
    mutable std::array<uint8_t, 9> gf3_mult_lut_;  // L1 cache LUT for GF(3) mult
    mutable std::array<uint8_t, 9> gf3_add_lut_;   // L1 cache LUT for GF(3) add
    size_t max_qutrits_;
    
    void init_luts();  // Initialize compile-time LUTs
    
    // LUT-based GF(3) operations for <0.5 pJ/op
    inline uint8_t gf3_mult(uint8_t a, uint8_t b) const {
        return gf3_mult_lut_[a * 3 + b];  // Single L1 cache lookup
    }
    
    inline uint8_t gf3_add(uint8_t a, uint8_t b) const {
        return gf3_add_lut_[a * 3 + b];   // Single L1 cache lookup
    }
    
    // Helper to create vertex stabilizers from edges
    void create_vertex_stabilizers(const SimplicialComplex& complex);
    
    // Helper to create face stabilizers from triangles
    void create_face_stabilizers(const SimplicialComplex& complex);
};

} // namespace q_mini_wasm_v2::core::qgnn
