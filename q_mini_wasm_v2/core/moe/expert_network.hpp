#pragma once

#include "../ternary/trit.hpp"
#include <utility>
#include <vector>
#include <string>
#include <map>
#include <functional>
#include <memory>
#include <cstddef>
#include <cstdint>

namespace q_mini_wasm_v2::core::moe {

/**
 * @brief Abstract expert network (max-plus layers over GF(3) trits)
 *
 * Concrete experts implement max-plus (tropical) linear forwards and Forward–Forward training; FF uses TritPack5
 * buffers for the same GF(3) alphabet—encoding only, not a different semiring.
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
        size_t ff_active_internal_layers = 0; // 0 = use all layers each FF step
        ternary::Trit use_activation = ternary::Trit::POSITIVE;     // Apply ternary activation
        ternary::EnergyTrit energy_budget = ternary::EnergyTrit::MEDIUM;
    };

    explicit ExpertNetwork(const ExpertConfig& config) : config_(config) {}
    virtual ~ExpertNetwork() = default;

    // ========================================================================
    // Core Operations (must be implemented by derived classes)
    // ========================================================================
    
    /**
     * @brief Forward pass (max-plus linear map over GF(3) trits; see concrete expert)
     * @param input Input trits
     */
    virtual std::vector<ternary::Trit> Forward(
        const std::vector<ternary::Trit>& input
    ) = 0;
    
    /**
     * @brief Training step using Forward-Forward algorithm (TritPack5 wire only).
     *
     * Each buffer is @c q::ternary::pack_batch_t5 over @c config_.input_dim balanced trits
     * (length ceil(input_dim/5) bytes). No dense @c ternary::Trit vectors on this path.
     */
    virtual int32_t TrainForwardForward(
        const std::vector<uint8_t>& positive_pack5,
        const std::vector<uint8_t>& negative_pack5) = 0;

    // ========================================================================
    // Common Utilities (provided by base class)
    // ========================================================================
    
    /**
     * @brief Goodness on an arbitrary ternary activation vector.
     *
     * For MoE metrics, prefer @ref last_forward_forward_route_goodness() after
     * @ref TrainForwardForward — that reflects the expert's **final-layer output**
     * used inside the training step. This helper counts non-zero trits in the
     * vector passed in (for ternary {-1,0,+1}, equivalent to sum of |trit|).
     */
    virtual uint32_t ComputeGoodness(
        const std::vector<ternary::Trit>& activations
    ) const;

    /** Goodness values from the most recent @ref TrainForwardForward (final-layer outputs). */
    std::pair<uint32_t, uint32_t> last_forward_forward_route_goodness() const noexcept {
        return {last_ff_pos_out_good_, last_ff_neg_out_good_};
    }
    
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
    void record_ff_route_output_goodness(uint32_t pos_goodness, uint32_t neg_goodness) noexcept {
        last_ff_pos_out_good_ = pos_goodness;
        last_ff_neg_out_good_ = neg_goodness;
    }

    ExpertConfig config_;
    ExpertStats stats_;

    /** Last FF step: goodness on positive/negative **network outputs** (not raw inputs). */
    uint32_t last_ff_pos_out_good_ = 0;
    uint32_t last_ff_neg_out_good_ = 0;

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
