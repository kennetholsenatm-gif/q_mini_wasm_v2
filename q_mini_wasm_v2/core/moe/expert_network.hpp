#pragma once

#include "../ternary/trit.hpp"
#include <vector>
#include <cstddef>
#include <cstdint>

namespace q_mini_wasm_v2::core::moe {

/**
 * @brief Abstract base class for expert neural networks
 * 
 * All expert implementations must inherit from this and implement
 * GF(3) ternary operations for forward pass and Forward-Forward training.
 */
class ExpertNetwork {
public:
    /**
     * @brief Expert configuration
     */
    struct ExpertConfig {
        size_t input_dim = 64;          // Input feature dimension
        size_t output_dim = 64;         // Output feature dimension
        size_t hidden_dim = 128;        // Hidden layer dimension
        size_t num_layers = 2;          // Number of layers
        ternary::Trit use_activation = ternary::Trit::POSITIVE;     // Apply ternary activation
        ternary::EnergyTrit energy_budget = ternary::EnergyTrit::MEDIUM;
    };

    explicit ExpertNetwork(const ExpertConfig& config) : config_(config) {}
    virtual ~ExpertNetwork() = default;

    // ========================================================================
    // Core Operations (must be implemented by derived classes)
    // ========================================================================
    
    /**
     * @brief Forward pass using GF(3) ternary arithmetic
     * 
     * @param input Input features as ternary values
     * @return Output features as ternary values
     */
    virtual std::vector<ternary::Trit> Forward(
        const std::vector<ternary::Trit>& input
    ) = 0;
    
    /**
     * @brief Training step using Forward-Forward algorithm
     * 
     * @param positive Positive sample (real data)
     * @param negative Negative sample (corrupted data)
     * @return Goodness delta (positive - negative)
     */
    virtual int32_t TrainForwardForward(
        const std::vector<ternary::Trit>& positive,
        const std::vector<ternary::Trit>& negative
    ) = 0;

    // ========================================================================
    // Common Utilities (provided by base class)
    // ========================================================================
    
    /**
     * @brief Compute tropical goodness metric
     * 
     * Goodness = sum of squared activations (sum of abs values for ternary)
     */
    virtual uint32_t ComputeGoodness(
        const std::vector<ternary::Trit>& activations
    ) const;
    
    /**
     * @brief Generate negative sample by corrupting input
     */
    virtual std::vector<ternary::Trit> GenerateNegativeSample(
        const std::vector<ternary::Trit>& positive
    ) const;
    
    /**
     * @brief Get expert configuration
     */
    const ExpertConfig& GetConfig() const { return config_; }
    
    /**
     * @brief Get expert statistics
     */
    struct ExpertStats {
        uint64_t forward_calls = 0;
        uint64_t train_calls = 0;
        int32_t total_goodness_delta = 0;
        ternary::EnergyTrit total_energy_consumed = ternary::EnergyTrit::LOW;
    };
    
    ExpertStats GetStats() const { return stats_; }
    void ResetStats() { stats_ = ExpertStats{}; }

protected:
    ExpertConfig config_;
    ExpertStats stats_;
    
    // Helper: GF(3) modulo arithmetic
    static int8_t GF3Add(int8_t a, int8_t b) {
        int8_t sum = a + b;
        if (sum > 1) return -1;
        if (sum < -1) return 1;
        return sum;
    }
    
    static int8_t GF3Multiply(int8_t a, int8_t b) {
        // Ternary multiplication: {-1, 0, 1} × {-1, 0, 1}
        if (a == 0 || b == 0) return 0;
        if (a == b) return 1;
        return -1;
    }
};

/**
 * @brief Factory function type for creating experts
 */
using ExpertFactory = std::function<std::unique_ptr<ExpertNetwork>(const ExpertNetwork::ExpertConfig&)>;

/**
 * @brief Registry for expert types
 */
class ExpertRegistry {
public:
    static ExpertRegistry& Instance();
    
    void Register(const std::string& name, ExpertFactory factory);
    std::unique_ptr<ExpertNetwork> Create(const std::string& name, const ExpertNetwork::ExpertConfig& config);
    std::vector<std::string> GetRegisteredTypes() const;

private:
    ExpertRegistry() = default;
    std::map<std::string, ExpertFactory> factories_;
};

} // namespace q_mini_wasm_v2::core::moe
