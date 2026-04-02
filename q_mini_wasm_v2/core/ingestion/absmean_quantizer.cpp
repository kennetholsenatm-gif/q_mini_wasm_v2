#include "absmean_quantizer.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace q_mini_wasm_v2::core::ingestion {

// ============================================================================
// AbsmeanQuantizer Implementation
// ============================================================================

AbsmeanQuantizer::AbsmeanQuantizer(double epsilon)
    : epsilon_(epsilon)
{
}

AbsmeanQuantizer::~AbsmeanQuantizer() = default;

std::vector<ternary::Trit> AbsmeanQuantizer::quantize(
    const std::vector<double>& weights,
    size_t rows,
    size_t cols
) const {
    double gamma = compute_scaling_factor(weights);
    
    std::vector<ternary::Trit> result(weights.size());
    
    for (size_t i = 0; i < weights.size(); ++i) {
        double normalized = weights[i] / (gamma + epsilon_);
        result[i] = round_clip(normalized);
    }
    
    return result;
}

std::vector<ternary::Trit> AbsmeanQuantizer::quantize_vector(const std::vector<double>& input) const {
    return quantize(input, 1, input.size());
}

double AbsmeanQuantizer::compute_scaling_factor(const std::vector<double>& weights) const {
    if (weights.empty()) {
        return 1.0;
    }
    
    // γ = (1/nm) Σ|W_ij|
    double sum_abs = 0.0;
    for (double w : weights) {
        sum_abs += std::abs(w);
    }
    
    return sum_abs / weights.size();
}

double AbsmeanQuantizer::compute_sparsity(const std::vector<ternary::Trit>& quantized) const {
    if (quantized.empty()) {
        return 0.0;
    }
    
    size_t zero_count = std::count_if(quantized.begin(), quantized.end(),
        [](ternary::Trit t) { return t == ternary::Trit::ZERO; });
    
    return static_cast<double>(zero_count) / quantized.size();
}

std::vector<double> AbsmeanQuantizer::dequantize(
    const std::vector<ternary::Trit>& quantized,
    double scaling_factor
) const {
    std::vector<double> result(quantized.size());
    
    for (size_t i = 0; i < quantized.size(); ++i) {
        result[i] = static_cast<double>(quantized[i]) * scaling_factor;
    }
    
    return result;
}

ternary::Trit AbsmeanQuantizer::round_clip(double value) {
    if (value > 0.5) {
        return ternary::Trit::POSITIVE;
    } else if (value < -0.5) {
        return ternary::Trit::NEGATIVE;
    }
    return ternary::Trit::ZERO;
}

// ============================================================================
// TernaryLSH Implementation
// ============================================================================

TernaryLSH::TernaryLSH(size_t input_dim, size_t output_dim, double sparsity, unsigned seed)
    : input_dim_(input_dim)
    , output_dim_(output_dim)
    , sparsity_(sparsity)
    , positive_margin_(0.0)
    , negative_margin_(0.0)
{
    initialize_projection_matrix(seed);
    compute_margins();
}

TernaryLSH::~TernaryLSH() = default;

std::vector<ternary::Trit> TernaryLSH::hash(const std::vector<double>& input) const {
    if (input.size() != input_dim_) {
        throw std::invalid_argument("Input dimension mismatch");
    }
    
    // Project onto ternary matrix
    std::vector<double> projections(output_dim_, 0.0);
    
    for (size_t i = 0; i < output_dim_; ++i) {
        for (size_t j = 0; j < input_dim_; ++j) {
            projections[i] += static_cast<double>(projection_matrix_[i][j]) * input[j];
        }
    }
    
    // Apply dual-threshold adaptive margin
    std::vector<ternary::Trit> result(output_dim_);
    
    for (size_t i = 0; i < output_dim_; ++i) {
        if (projections[i] > positive_margin_) {
            result[i] = ternary::Trit::POSITIVE;
        } else if (projections[i] < negative_margin_) {
            result[i] = ternary::Trit::NEGATIVE;
        } else {
            result[i] = ternary::Trit::ZERO;
        }
    }
    
    return result;
}

std::vector<std::vector<ternary::Trit>> TernaryLSH::hash_batch(
    const std::vector<std::vector<double>>& inputs
) const {
    std::vector<std::vector<ternary::Trit>> results;
    results.reserve(inputs.size());
    
    for (const auto& input : inputs) {
        results.push_back(hash(input));
    }
    
    return results;
}

double TernaryLSH::hamming_distance(
    const std::vector<ternary::Trit>& hash1,
    const std::vector<ternary::Trit>& hash2
) const {
    if (hash1.size() != hash2.size()) {
        throw std::invalid_argument("Hash sizes must match");
    }
    
    size_t distance = 0;
    for (size_t i = 0; i < hash1.size(); ++i) {
        if (hash1[i] != hash2[i]) {
            ++distance;
        }
    }
    
    return static_cast<double>(distance) / hash1.size();
}

double TernaryLSH::similarity(
    const std::vector<ternary::Trit>& hash1,
    const std::vector<ternary::Trit>& hash2
) const {
    return 1.0 - hamming_distance(hash1, hash2);
}

void TernaryLSH::initialize_projection_matrix(unsigned seed) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    
    projection_matrix_.resize(output_dim_, std::vector<ternary::Trit>(input_dim_));
    
    for (size_t i = 0; i < output_dim_; ++i) {
        for (size_t j = 0; j < input_dim_; ++j) {
            double r = dist(rng);
            
            if (r < sparsity_) {
                // Zero entry
                projection_matrix_[i][j] = ternary::Trit::ZERO;
            } else if (r < sparsity_ + (1.0 - sparsity_) / 2.0) {
                // Positive entry
                projection_matrix_[i][j] = ternary::Trit::POSITIVE;
            } else {
                // Negative entry
                projection_matrix_[i][j] = ternary::Trit::NEGATIVE;
            }
        }
    }
}

void TernaryLSH::compute_margins() {
    // Compute adaptive margins from projection matrix statistics
    // Margins set to 1 standard deviation of expected projection values
    
    double expected_mean = 0.0;
    double expected_var = 0.0;
    
    // For Rademacher distribution with sparsity:
    // E[X] = 0
    // Var[X] = (1 - sparsity) * 1^2 = 1 - sparsity
    
    expected_var = 1.0 - sparsity_;
    double expected_std = std::sqrt(expected_var);
    
    positive_margin_ = expected_std;
    negative_margin_ = -expected_std;
}

// ============================================================================
// DataIngestionPipeline Implementation
// ============================================================================

DataIngestionPipeline::DataIngestionPipeline(size_t input_dim, size_t hash_dim, double quantizer_epsilon)
    : last_sparsity_(0.0)
{
    quantizer_ = std::make_unique<AbsmeanQuantizer>(quantizer_epsilon);
    lsh_ = std::make_unique<TernaryLSH>(input_dim, hash_dim, 0.5, 42);
}

DataIngestionPipeline::~DataIngestionPipeline() = default;

std::vector<ternary::Trit> DataIngestionPipeline::process(const std::vector<double>& input) const {
    // Step 1: Apply TLSH dimensionality reduction
    auto hashed = lsh_->hash(input);
    
    // Step 2: Quantize (already ternary from TLSH, but apply absmean for consistency)
    auto quantized = quantizer_->quantize_vector(input);
    
    // Update sparsity statistics
    last_sparsity_ = quantizer_->compute_sparsity(quantized);
    
    // Return hashed representation (compressed dimension)
    return hashed;
}

std::vector<std::vector<ternary::Trit>> DataIngestionPipeline::process_batch(
    const std::vector<std::vector<double>>& inputs
) const {
    std::vector<std::vector<ternary::Trit>> results;
    results.reserve(inputs.size());
    
    double total_sparsity = 0.0;
    
    for (const auto& input : inputs) {
        results.push_back(process(input));
        total_sparsity += last_sparsity_;
    }
    
    last_sparsity_ = total_sparsity / inputs.size();
    
    return results;
}

} // namespace q_mini_wasm_v2::core::ingestion