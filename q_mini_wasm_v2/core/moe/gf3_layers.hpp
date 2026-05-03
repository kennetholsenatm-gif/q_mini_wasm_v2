#pragma once

#include "expert_network.hpp"
#include <vector>
#include <memory>
#include <cstddef>
#include <cstdint>
#include <span>
#include <iosfwd>
#include <atomic>

namespace q_mini_wasm_v2::core::moe {

/**
 * MoE GF(3) expert FF is a single max-plus (tropical) stack: activations and weights live in GF(3) as trits
 * {-1,0,+1}; the linear map is max-plus (tropical addition = max, tropical multiplication = integer add on lifts).
 * TritPack5 is only a dense polynomial byte encoding of those trits for SYCL/host buffers—it does not change the
 * semiring. Multi-slot Forward–Forward batches the same max-plus forwards and batched Hebbian updates across
 * parallel slots (B experts × layers); it is the same algebra, wider scheduling.
 */

/** Cumulative GF(3) Hebbian weight-cell update steps (process lifetime). */
uint64_t gf3_hebbian_weight_cell_updates_total() noexcept;

/**
 * True in `USE_SYCL=1` training builds: GF3 forward / Hebbian must use the GPU (see @ref throw_gpu_required).
 * Non-SYCL translation units: false (GF3 entry points throw if invoked without SYCL).
 */
bool gpu_mandatory_for_ff_math() noexcept;

/** Host MiB budget for SYCL multi-slot FF: telemetry plus @ref gf3_ff_multislot_host_budget_slots_cap when chunk=0. */
void gf3_ff_set_batched_weight_host_budget_mib(size_t mib) noexcept;

/** Effective host budget in MiB after the last @ref gf3_ff_set_batched_weight_host_budget_mib (0 when SYCL disabled). */
size_t gf3_ff_batched_weight_host_budget_effective_mib() noexcept;

/** Max layers to process per SYCL multi-slot FF batch (0 = unlimited - process all active layers). 
 *  Reduces per-kernel memory pressure while keeping high slot parallelism. */
void gf3_ff_set_layers_per_batch(uint32_t layers) noexcept;

/** Effective layers per batch after the last @ref gf3_ff_set_layers_per_batch (0 when SYCL disabled). */
uint32_t gf3_ff_layers_per_batch_effective() noexcept;

class GF3MultiLayerExpert;

/**
 * Conservative max MoE slot count B for one @c TryTrainForwardForwardMultiSlot chunk so modeled peak
 * host Pack5 buffers stay within @p budget_mib. Returns @c std::numeric_limits<size_t>::max() when SYCL is
 * off, @p expert0 is null, or @p budget_mib is 0 (caller: do not clamp on budget). Otherwise returns >= 2.
 */
size_t gf3_ff_multislot_host_budget_slots_cap(const GF3MultiLayerExpert* expert0, size_t budget_mib) noexcept;

/**
 * @brief Max-plus linear layer over GF(3) trits
 *
 * Values are GF(3) trits; the layer map is tropical (max-plus): output[j] = max_i(input[i] + weight[i][j]) (+ bias),
 * with outputs clamped back to {-1,0,+1}. `USE_SYCL` builds run this on the GPU only; Pack5 buffers carry trits, not floats.
 */
class GF3LinearLayer {
public:
    /**
     * @brief Layer configuration
     */
    struct LayerConfig {
        size_t input_dim;
        size_t output_dim;
        ternary::Trit use_bias = ternary::Trit::POSITIVE;
    };

    explicit GF3LinearLayer(const LayerConfig& config);
    ~GF3LinearLayer() = default;

    // ========================================================================
    // Core Operations
    // ========================================================================
    
    /**
     * @brief Forward pass: tropical (max-plus) linear map over GF(3) trits.
     */
    std::vector<ternary::Trit> Forward(const std::vector<ternary::Trit>& input);

    // ========================================================================
    // Weight Management
    // ========================================================================
    
    /**
     * @brief Initialize weights with deterministic ternary values
     */
    void InitializeWeights(int seed = 42);
    /** Lazy init helper: initialize once when first touched. */
    void EnsureInitialized(int seed = 42);
    
    /**
     * @brief Set weight at specific position
     */
    void SetWeight(size_t in_idx, size_t out_idx, ternary::Trit value);
    
    /**
     * @brief Get weight at specific position
     */
    ternary::Trit GetWeight(size_t in_idx, size_t out_idx) const;
    
    /**
     * @brief Set bias vector
     */
    void SetBias(const std::vector<ternary::Trit>& bias);
    
    /**
     * @brief Get all weights (flattened)
     */
    std::vector<ternary::Trit> GetWeights() const;
    
    /**
     * @brief Get all biases
     */
    std::vector<ternary::Trit> GetBias() const;

    // ========================================================================
    // Training (Forward-Forward)
    // ========================================================================
    
    /**
     * @brief Compute layer goodness for Forward-Forward
     * 
     * Goodness = sum of squared activations
     * For ternary: count of non-zero activations
     */
    uint32_t ComputeGoodness(const std::vector<ternary::Trit>& activations) const;
    
    /**
     * @brief Hebbian weight nudges (Forward–Forward) in the GF(3) weight ring
     *
     * Same trit alphabet as the max-plus forward; updates apply GF(3) add/mul to weights (the layer map itself stays max-plus).
     *
     * @param input Input activations
     * @param goodness_delta Delta from positive/negative samples
     * @param learning_rate Learning rate (ternary: typically 1 or -1)
     */
    void UpdateWeightsHebbian(
        const std::vector<ternary::Trit>& input,
        int32_t goodness_delta,
        int8_t learning_rate = 1
    );

    // ========================================================================
    // Information
    // ========================================================================
    
    size_t GetInputDim() const { return config_.input_dim; }
    size_t GetOutputDim() const { return config_.output_dim; }
    size_t GetParameterCount() const { 
        return config_.input_dim * config_.output_dim + 
               (config_.use_bias == ternary::Trit::POSITIVE ? config_.output_dim : 0); 
    }

    /**
     * @brief Get sparsity (fraction of zero weights)
     * @return Q24.8 fixed point representation
     */
    uint32_t GetSparsity() const;

    /** Binary weight blob for QMINI_V3 checkpoints (little-endian layout). */
    void SerializeWeights(std::ostream& os) const;
    bool DeserializeWeights(std::istream& is);

    /** TritPack5-packed weight cache (ceil(input_dim*output_dim/5) bytes). */
    std::vector<uint8_t>& weights_pack5_buffer_ref();
    const std::vector<uint8_t>& bias_pack5_ref() const;
    bool layer_use_bias() const noexcept;
    /** Call after batched SYCL Hebbian wrote into @ref weights_pack5_buffer_ref. */
    void notify_weights_pack5_device_updated();

private:
    LayerConfig config_;
    
    // Weights: [input_dim][output_dim]
    std::vector<std::vector<ternary::Trit>> weights_;
    
    // Bias: [output_dim]
    std::vector<ternary::Trit> bias_;

    // TritPack5 caches for SYCL (same polynomial layout as q::ternary::pack_batch_t5).
    mutable std::vector<uint8_t> weights_pack5_cache_;
    mutable std::vector<uint8_t> bias_pack5_cache_;
    mutable std::atomic<bool> sycl_cache_valid_{false};
    mutable std::atomic<bool> bias_cache_valid_{false};
    mutable std::atomic<bool> weights_host_dirty_{false};
    std::atomic<bool> layer_initialized_{false};

    void invalidate_sycl_caches() noexcept;
    void rebuild_weight_pack5_cache() const;
    void rebuild_bias_pack5_cache() const;
    void sync_weights_to_host_if_needed();
    void sync_weights_to_host_if_needed() const;
    
    /** GF(3) add — used for Hebbian weight updates (weight ring), not for max-plus forward. */
    static int8_t GF3Add(int8_t a, int8_t b) {
        int8_t sum = a + b;
        if (sum > 1) return -1;  // Wrap: 2 -> -1
        if (sum < -1) return 1;  // Wrap: -2 -> 1
        return sum;
    }
    
    /** GF(3) mul — Hebbian deltas on weights (SYCL path uses same ring on device). */
    static int8_t GF3Multiply(int8_t a, int8_t b) {
        if (a == 0 || b == 0) return 0;
        if (a == b) return 1;
        return -1;
    }
};

class GF3MultiLayerExpert;

/** One multi-slot FF entry: one expert + pos/neg Pack5 rows (same max-plus stack, batched with other slots). */
struct GF3FfTrainingSlot {
    GF3MultiLayerExpert* expert = nullptr;
    /** TritPack5 for first-layer input trits (length ceil(expert_input_dim/5)). */
    std::vector<uint8_t> positive_pack5;
    std::vector<uint8_t> negative_pack5;
    /** Optional non-owning row-pack references to avoid deep copies in multi-route batching. */
    const std::vector<uint8_t>* positive_pack5_ref = nullptr;
    const std::vector<uint8_t>* negative_pack5_ref = nullptr;

    const std::vector<uint8_t>& positive_pack5_view() const noexcept {
        return positive_pack5_ref ? *positive_pack5_ref : positive_pack5;
    }
    const std::vector<uint8_t>& negative_pack5_view() const noexcept {
        return negative_pack5_ref ? *negative_pack5_ref : negative_pack5;
    }
};

/**
 * @brief Multi-layer max-plus expert (GF(3) trits, TritPack5 on FF wire)
 */
class GF3MultiLayerExpert : public ExpertNetwork {
public:
    explicit GF3MultiLayerExpert(const ExpertConfig& config);
    ~GF3MultiLayerExpert() override = default;

    // ExpertNetwork interface implementation
    std::vector<ternary::Trit> Forward(
        const std::vector<ternary::Trit>& input
    ) override;
    
    int32_t TrainForwardForward(
        const std::vector<uint8_t>& positive_pack5,
        const std::vector<uint8_t>& negative_pack5) override;

    // Layer management
    void AddLayer(size_t output_dim);
    void InitializeAllLayers(int seed = 42);
    
    /**
     * @brief Apply ternary activation (identity with threshold)
     * 
     * Values > 0.5 -> 1
     * Values < -0.5 -> -1
     * Otherwise -> 0
     */
    static std::vector<ternary::Trit> TernaryActivation(
        const std::vector<int32_t>& pre_activations
    );

    void SerializeWeights(std::ostream& os) const;
    bool DeserializeWeights(std::istream& is);

    /** Number of linear layers in this expert (composition depth). */
    size_t linear_layer_count() const noexcept;

    /** Non-owning pointer for batched GPU FF (bounds-checked). */
    GF3LinearLayer* mutable_layer(size_t layer_idx);

    bool is_initialized() const noexcept;

    /**
     * Forward–Forward for one or more slots; SYCL batched kernels (`USE_SYCL` only). Returns false only on recoverable
     * SYCL submission/shape failure (caller may retry); never a CPU math path.
     */
    static bool TryTrainForwardForwardMultiSlot(
        const std::vector<GF3FfTrainingSlot>& slots,
        std::vector<uint32_t>* out_pos_goodness = nullptr,
        std::vector<uint32_t>* out_neg_goodness = nullptr);
    static bool TryTrainForwardForwardMultiSlot(
        std::span<const GF3FfTrainingSlot> slots,
        std::vector<uint32_t>* out_pos_goodness = nullptr,
        std::vector<uint32_t>* out_neg_goodness = nullptr);

    /**
     * Batched FF for experts with identical topology. SYCL only when `USE_SYCL`; returns false if the batched kernel
     * could not run (caller may use per-expert `TrainForwardForward` — still GPU SYCL per expert, not CPU).
     */
    static bool TryTrainForwardForwardBatched(
        const std::vector<GF3MultiLayerExpert*>& experts,
        const std::vector<uint8_t>& positive_pack5,
        const std::vector<uint8_t>& negative_pack5);

    /** Match per-expert stats updates from a single batched FF step. */
    void accumulate_batched_ff_stats(int32_t delta);

private:
    std::vector<std::unique_ptr<GF3LinearLayer>> layers_;
    ternary::Trit initialized_ = ternary::Trit::ZERO;
};

} // namespace q_mini_wasm_v2::core::moe
