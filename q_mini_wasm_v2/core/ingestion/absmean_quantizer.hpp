#pragma once

#include <vector>
#include <cstdint>
#include <memory>
#include <random>
#include "../ternary/trit.hpp"

namespace q_mini_wasm_v2::core::ingestion {

/**
 * @brief Absmean Quantization for continuous-to-ternary conversion
 * 
 * Based on research: "A Unified QMINIWASM Framework: Bridging Qutrit 
 * Stabilizer Formalisms and Extreme-Edge Ternary AI"
 * 
 * The adaptive scaling factor γ is determined by computing the average 
 * absolute magnitude of the weight matrix:
 * γ = (1/nm) Σ|W_ij|
 * 
 * Continuous values are then mapped to {-1, 0, +1} via symmetric RoundClip:
 * W̃_ij = RoundClip(W_ij / γ, -1, +1)
 * 
 * This induces 30-40% organic sparsity (zero weights), enabling free
 * hardware acceleration during forward pass.
 */
class AbsmeanQuantizer {
public:
    /**
     * @brief Construct quantizer with optional margin constant
     * @param epsilon Small constant to prevent zero-division (default 1e-8)
     */
    explicit AbsmeanQuantizer(double epsilon = 1e-8);
    ~AbsmeanQuantizer();

    // ========================================================================
    // Quantization Operations
    // ========================================================================
    
    /**
     * @brief Quantize continuous weights to ternary using Absmean
     * @param weights Continuous weight matrix (flattened)
     * @param rows Number of rows
     * @param cols Number of columns
     * @return Ternary weight matrix
     */
    std::vector<ternary::Trit> quantize(
        const std::vector<double>& weights,
        size_t rows,
        size_t cols
    ) const;
    
    /**
     * @brief Quantize single vector
     * @param input Continuous input vector
     * @return Ternary output vector
     */
    std::vector<ternary::Trit> quantize_vector(const std::vector<double>& input) const;
    
    /**
     * @brief Compute adaptive scaling factor γ
     * @param weights Continuous weight matrix
     * @return Scaling factor
     */
    double compute_scaling_factor(const std::vector<double>& weights) const;
    
    /**
     * @brief Compute sparsity ratio (fraction of zeros)
     * @param quantized Ternary weight matrix
     * @return Sparsity ratio (0.0 to 1.0)
     */
    double compute_sparsity(const std::vector<ternary::Trit>& quantized) const;

    // ========================================================================
    // Inverse Operations (for dequantization)
    // ========================================================================
    
    /**
     * @brief Dequantize ternary weights back to continuous
     * @param quantized Ternary weights
     * @param scaling_factor Scaling factor used during quantization
     * @return Continuous weights
     */
    std::vector<double> dequantize(
        const std::vector<ternary::Trit>& quantized,
        double scaling_factor
    ) const;

private:
    double epsilon_;
    
    /**
     * @brief RoundClip function: clamp value to [-1, +1] and round
     */
    static ternary::Trit round_clip(double value);
};

/**
 * @brief Ternary Locality-Sensitive Hashing (TLSH)
 * 
 * Based on research: "A Unified QMINIWASM Framework"
 * 
 * Compresses high-dimensional dense embeddings into lower-dimensional
 * discrete formats while preserving topological distance metrics.
 * 
 * TLSH projects continuous vectors onto a randomized sparse Rademacher
 * matrix (entries drawn from {-1, 0, +1}), then applies dual-threshold
 * adaptive margin to generate ternary hashes.
 */
class TernaryLSH {
public:
    /**
     * @brief Construct TLSH with specified output dimension
     * @param input_dim Input vector dimension
     * @param output_dim Output hash dimension
     * @param sparsity Fraction of zero entries in projection matrix (default 0.5)
     * @param seed Random seed for reproducibility
     */
    TernaryLSH(size_t input_dim, size_t output_dim, double sparsity = 0.5, unsigned seed = 42);
    ~TernaryLSH();

    // ========================================================================
    // Hashing Operations
    // ========================================================================
    
    /**
     * @brief Hash continuous vector to ternary hash
     * @param input Continuous input vector
     * @return Ternary hash vector
     */
    std::vector<ternary::Trit> hash(const std::vector<double>& input) const;
    
    /**
     * @brief Hash batch of vectors
     * @param inputs Batch of continuous vectors
     * @return Batch of ternary hashes
     */
    std::vector<std::vector<ternary::Trit>> hash_batch(
        const std::vector<std::vector<double>>& inputs
    ) const;
    
    /**
     * @brief Compute Hamming distance between two ternary hashes
     * @param hash1 First hash
     * @param hash2 Second hash
     * @return Normalized Hamming distance (0.0 to 1.0)
     */
    double hamming_distance(
        const std::vector<ternary::Trit>& hash1,
        const std::vector<ternary::Trit>& hash2
    ) const;
    
    /**
     * @brief Compute similarity score (1 - distance)
     */
    double similarity(
        const std::vector<ternary::Trit>& hash1,
        const std::vector<ternary::Trit>& hash2
    ) const;

    // ========================================================================
    // Configuration
    // ========================================================================
    
    size_t input_dim() const { return input_dim_; }
    size_t output_dim() const { return output_dim_; }
    
    /**
     * @brief Get projection matrix (for inspection/debugging)
     */
    const std::vector<std::vector<ternary::Trit>>& get_projection_matrix() const {
        ensure_initialized();
        return projection_matrix_;
    }

private:
    size_t input_dim_;
    size_t output_dim_;
    double sparsity_;
    unsigned int seed_;
    
    // Rademacher projection matrix (ternary entries) - lazy initialized
    mutable std::vector<std::vector<ternary::Trit>> projection_matrix_;
    mutable bool initialized_ = false;
    
    // Adaptive margin thresholds - mutable for lazy init from const methods
    mutable double positive_margin_;
    mutable double negative_margin_;
    
    /**
     * @brief Initialize projection matrix with random ternary entries (lazy)
     */
    void initialize_projection_matrix(unsigned seed) const;
    
    /**
     * @brief Ensure matrix is initialized (thread-safe lazy init)
     */
    void ensure_initialized() const;
    
    /**
     * @brief Compute adaptive margins from projection statistics
     */
    void compute_margins() const;
};

/**
 * @brief Data ingestion pipeline combining Absmean + TLSH
 */
class DataIngestionPipeline {
public:
    /**
     * @brief Construct pipeline
     * @param input_dim Raw input dimension
     * @param hash_dim Hash output dimension
     * @param quantizer_epsilon Epsilon for Absmean quantizer
     */
    DataIngestionPipeline(size_t input_dim, size_t hash_dim, double quantizer_epsilon = 1e-8);
    ~DataIngestionPipeline();

    /**
     * @brief Process continuous input through full pipeline
     * @param input Continuous input vector
     * @return Ternary representation ready for engine processing
     */
    std::vector<ternary::Trit> process(const std::vector<double>& input) const;
    
    /**
     * @brief Process batch of inputs
     */
    std::vector<std::vector<ternary::Trit>> process_batch(
        const std::vector<std::vector<double>>& inputs
    ) const;
    
    /**
     * @brief Get sparsity statistics from last processing
     */
    double last_sparsity() const { return last_sparsity_; }

private:
    std::unique_ptr<AbsmeanQuantizer> quantizer_;
    std::unique_ptr<TernaryLSH> lsh_;
    mutable double last_sparsity_;
};

} // namespace q_mini_wasm_v2::core::ingestion