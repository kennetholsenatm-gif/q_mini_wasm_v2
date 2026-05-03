#include "gf3_layers.hpp"
#include "gf3_ff_size_checks.hpp"
#include "../../sycl/gf3_layers_sycl.hpp"
#include "../ternary/packing.hpp"
#include <atomic>
#include <random>
#include <algorithm>
#include <numeric>
#include <iostream>
#include <sstream>
#include <cstring>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace q_mini_wasm_v2::core::moe {

#if defined(USE_SYCL) && USE_SYCL
namespace {
std::atomic<uint64_t> g_ff_syctl_verbose_i{0};
/** Throttle hot-path SYCL FF stderr (Windows console I/O can stall the training thread). */
inline bool ff_syctl_verbose_detail() {
    const uint64_t i = g_ff_syctl_verbose_i.fetch_add(1, std::memory_order_relaxed);
    return i < 96u || (i % 512u) == 0u;
}
} // namespace
// Host byte budget (MiB via gf3_ff_set_batched_weight_host_budget_mib): telemetry + multi-slot chunk cap
// when training.ff_multi_row_slots_chunk == 0 (see gf3_ff_multislot_host_budget_slots_cap).
static std::atomic<uint64_t> g_gf3_ff_batched_weight_host_budget_bytes{0};
// Layers per batch limit for SYCL multi-slot FF (0 = unlimited).
static std::atomic<uint32_t> g_gf3_ff_layers_per_batch{0};
#endif

void gf3_ff_set_batched_weight_host_budget_mib(size_t mib) noexcept {
#if defined(USE_SYCL) && USE_SYCL
    if (mib == 0u) {
        return;
    }
    g_gf3_ff_batched_weight_host_budget_bytes.store(
        static_cast<uint64_t>(mib) * 1024ull * 1024ull, std::memory_order_relaxed);
#else
    (void)mib;
#endif
}

size_t gf3_ff_batched_weight_host_budget_effective_mib() noexcept {
#if defined(USE_SYCL) && USE_SYCL
    const uint64_t b = g_gf3_ff_batched_weight_host_budget_bytes.load(std::memory_order_relaxed);
    return static_cast<size_t>(b / (1024ull * 1024ull));
#else
    return 0;
#endif
}

void gf3_ff_set_layers_per_batch(uint32_t layers) noexcept {
#if defined(USE_SYCL) && USE_SYCL
    g_gf3_ff_layers_per_batch.store(layers, std::memory_order_relaxed);
#else
    (void)layers;
#endif
}

uint32_t gf3_ff_layers_per_batch_effective() noexcept {
#if defined(USE_SYCL) && USE_SYCL
    return g_gf3_ff_layers_per_batch.load(std::memory_order_relaxed);
#else
    return 0;
#endif
}

size_t gf3_ff_multislot_host_budget_slots_cap(const GF3MultiLayerExpert* expert0, size_t budget_mib) noexcept {
#if !defined(USE_SYCL) || !USE_SYCL
    (void)expert0;
    (void)budget_mib;
    return std::numeric_limits<size_t>::max();
#else
    if (!expert0 || budget_mib == 0u) {
        return std::numeric_limits<size_t>::max();
    }
    GF3MultiLayerExpert* e0 = const_cast<GF3MultiLayerExpert*>(expert0);
    const size_t L = e0->linear_layer_count();
    if (L == 0u) {
        return std::numeric_limits<size_t>::max();
    }
    GF3LinearLayer* l0 = e0->mutable_layer(0);
    if (!l0) {
        return std::numeric_limits<size_t>::max();
    }
    const size_t in_first = l0->GetInputDim();
    const size_t row_pack_bytes = (in_first + 4u) / 5u;
    size_t max_wp_row = 0;
    size_t max_bp_row = 0;
    size_t max_io_dim = 0;
    size_t max_w_cells = 0;
    for (size_t li = 0; li < L; ++li) {
        GF3LinearLayer* lay = e0->mutable_layer(li);
        if (!lay) {
            return std::numeric_limits<size_t>::max();
        }
        const size_t in_d = lay->GetInputDim();
        const size_t out_d = lay->GetOutputDim();
        max_io_dim = std::max(max_io_dim, std::max(in_d, out_d));
        size_t w_cells = 0;
        if (gf3_ff_mat_cells_overflow_size(in_d, out_d, &w_cells)) {
            return 2;
        }
        max_w_cells = std::max(max_w_cells, w_cells);
        const size_t need_wp = (w_cells + 4u) / 5u;
        max_wp_row = std::max(max_wp_row, need_wp);
        if (lay->layer_use_bias()) {
            const size_t need_bp = (out_d + 4u) / 5u;
            max_bp_row = std::max(max_bp_row, need_bp);
        }
    }
    const size_t staging_row = std::max(row_pack_bytes, std::max(max_wp_row, max_bp_row));

    auto pack_trits = [](size_t B, size_t trits) -> uint64_t {
        if (B == 0 || trits == 0) {
            return 0;
        }
        size_t nt = 0;
        if (gf3_ff_mul_overflow_size(B, trits, &nt)) {
            return std::numeric_limits<uint64_t>::max();
        }
        return static_cast<uint64_t>((nt + 4u) / 5u);
    };

    auto mul_sat = [](uint64_t a, uint64_t b) -> uint64_t {
        if (a == 0ull || b == 0ull) {
            return 0;
        }
        if (a > std::numeric_limits<uint64_t>::max() / b) {
            return std::numeric_limits<uint64_t>::max();
        }
        return a * b;
    };

    // Upper bound bytes(B) for host staging in TryTrainForwardForwardMultiSlot.
    // `staging_row` is already max(input pack5 row, single-layer weight pack5 row, bias row), so
    // `row_st = B * staging_row` scales with the same B×weight-matrix order as stacked `batch_w_packed`
    // (`pack_trits(B, max_w_cells)`). Counting row_st **plus** a large multiple of pack_w double-counts the
    // dominant weight staging and collapses B to O(~100-200) even for 32 GiB budgets. Keep a small pack_w
    // slack for encode/decode/SYCL buffer lifetime beside row_staging.
    const auto bytes_for_B = [&](size_t B) -> uint64_t {
        if (B < 2u) {
            return 0;
        }
        const uint64_t row_st = static_cast<uint64_t>(B) * static_cast<uint64_t>(staging_row);
        const uint64_t pack_io = pack_trits(B, max_io_dim);
        const uint64_t pack_w = pack_trits(B, max_w_cells);
        if (row_st == std::numeric_limits<uint64_t>::max() || pack_io == std::numeric_limits<uint64_t>::max() ||
            pack_w == std::numeric_limits<uint64_t>::max()) {
            return std::numeric_limits<uint64_t>::max();
        }
        constexpr uint64_t kActPackedSlack = 12ull; // pos/neg/recompute + resize slack vs pack_io
        constexpr uint64_t kWeightPackedSlackExtra = 2ull; // batch_w buffer + decode margin (not 5×; see row_st)
        const uint64_t act_churn = mul_sat(pack_io, kActPackedSlack);
        const uint64_t w_extra = mul_sat(pack_w, kWeightPackedSlackExtra);
        const uint64_t fixed = 512ull * 1024ull; // misc std::vector overhead / alignment
        uint64_t sum = row_st + act_churn;
        if (sum < row_st) {
            return std::numeric_limits<uint64_t>::max();
        }
        sum += w_extra;
        if (sum < w_extra) {
            return std::numeric_limits<uint64_t>::max();
        }
        const uint64_t before_fixed = sum;
        sum += fixed;
        if (sum < before_fixed) {
            return std::numeric_limits<uint64_t>::max();
        }
        return sum;
    };

    const uint64_t budget_bytes = static_cast<uint64_t>(budget_mib) * 1024ull * 1024ull;
    // Slightly more of the declared MiB budget goes to B (fewer tiny SYCL chunks). Still leaves headroom vs vectors.
    const uint64_t budget_use = budget_bytes * 93ull / 100ull;
    if (budget_use < 65536ull) {
        return 2;
    }
    if (bytes_for_B(2) > budget_use) {
        return 2;
    }
    size_t lo = 2;
    size_t hi = 2;
    // Exponential hi search until bytes_for_B exceeds budget (no fixed slot cap — programmers set MiB / TOML).
    for (;;) {
        if (hi > std::numeric_limits<size_t>::max() / 2u) {
            break;
        }
        const size_t next = hi * 2u;
        if (next <= hi) {
            break;
        }
        if (bytes_for_B(next) > budget_use) {
            break;
        }
        hi = next;
    }
    while (lo + 1 < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        if (bytes_for_B(mid) <= budget_use) {
            lo = mid;
        } else {
            hi = mid - 1;
        }
    }
    return std::max(size_t{2}, lo);
#endif
}
std::atomic<uint64_t> g_gf3_hebbian_weight_cell_updates{0};

bool gpu_mandatory_for_ff_math() noexcept {
#if defined(USE_SYCL) && USE_SYCL
    return true;
#else
    return false;
#endif
}

[[noreturn]] void throw_gpu_required(const char* op, size_t input_dim, size_t output_dim) {
    std::ostringstream oss;
    oss << "[GF3LinearLayer] SYCL required for GF3 FF (" << op << ", input_dim=" << input_dim
        << ", output_dim=" << output_dim << ").";
    throw std::runtime_error(oss.str());
}

#if defined(USE_SYCL) && USE_SYCL
bool gf3_ff_resize_row_staging(std::vector<uint8_t>& buf, size_t rows, size_t bytes_per_row, const char* ctx) {
    size_t total = 0;
    if (gf3_ff_mul_overflow_size(rows, bytes_per_row, &total)) {
        std::cerr << "[GF3MultiLayerExpert] SYCL FF: row staging size overflow ctx=" << ctx << " rows=" << rows
                  << " per_row=" << bytes_per_row << std::endl;
        return false;
    }
    buf.resize(total);
    return true;
}
#endif

uint64_t gf3_hebbian_weight_cell_updates_total() noexcept {
    return g_gf3_hebbian_weight_cell_updates.load(std::memory_order_relaxed);
}

// ============================================================================
// GF3LinearLayer Implementation
// ============================================================================

GF3LinearLayer::GF3LinearLayer(const LayerConfig& config)
    : config_(config)
{
    // Initialize weight matrix [input_dim][output_dim]
    weights_.resize(config_.input_dim);
    for (auto& row : weights_) {
        row.resize(config_.output_dim, ternary::Trit::ZERO);
    }
    
    // Initialize bias
    if (config_.use_bias == ternary::Trit::POSITIVE) {
        bias_.resize(config_.output_dim, ternary::Trit::ZERO);
    }
    weights_pack5_cache_.assign((config_.input_dim * config_.output_dim + 4u) / 5u, static_cast<uint8_t>(0));
    sycl_cache_valid_.store(false, std::memory_order_relaxed);
    bias_cache_valid_.store(false, std::memory_order_relaxed);
}

void GF3LinearLayer::invalidate_sycl_caches() noexcept {
    sycl_cache_valid_.store(false, std::memory_order_relaxed);
    bias_cache_valid_.store(false, std::memory_order_relaxed);
}

void GF3LinearLayer::rebuild_weight_pack5_cache() const {
    const size_t cells = config_.input_dim * config_.output_dim;
    std::vector<int8_t> flat(cells);
    for (size_t i = 0; i < config_.input_dim; ++i) {
        for (size_t j = 0; j < config_.output_dim; ++j) {
            flat[i * config_.output_dim + j] = static_cast<int8_t>(weights_[i][j]);
        }
    }
    q::ternary::pack_batch_t5(flat, weights_pack5_cache_);
    sycl_cache_valid_.store(true, std::memory_order_relaxed);
}

void GF3LinearLayer::sync_weights_to_host_if_needed() {
    if (!weights_host_dirty_.load(std::memory_order_relaxed)) {
        return;
    }
    const size_t cells = config_.input_dim * config_.output_dim;
    if (weights_pack5_cache_.size() < (cells + 4u) / 5u) {
        return;
    }
    std::vector<int8_t> flat;
    q::ternary::unpack_batch_t5(weights_pack5_cache_, flat, static_cast<uint32_t>(cells));
    if (flat.size() < cells) {
        return;
    }
    for (size_t i = 0; i < config_.input_dim; ++i) {
        for (size_t j = 0; j < config_.output_dim; ++j) {
            weights_[i][j] = static_cast<ternary::Trit>(flat[i * config_.output_dim + j]);
        }
    }
    weights_host_dirty_.store(false, std::memory_order_relaxed);
}

void GF3LinearLayer::sync_weights_to_host_if_needed() const {
    const_cast<GF3LinearLayer*>(this)->sync_weights_to_host_if_needed();
}

void GF3LinearLayer::rebuild_bias_pack5_cache() const {
    if (config_.use_bias != ternary::Trit::POSITIVE) {
        bias_pack5_cache_.clear();
        bias_cache_valid_.store(true, std::memory_order_relaxed);
        return;
    }
    std::vector<int8_t> bf(config_.output_dim, 0);
    for (size_t j = 0; j < config_.output_dim && j < bias_.size(); ++j) {
        bf[j] = static_cast<int8_t>(bias_[j]);
    }
    q::ternary::pack_batch_t5(bf, bias_pack5_cache_);
    bias_cache_valid_.store(true, std::memory_order_relaxed);
}

std::vector<ternary::Trit> GF3LinearLayer::Forward(
    const std::vector<ternary::Trit>& input
) {
#if defined(USE_SYCL) && USE_SYCL
    if (q_mini_wasm_v2::sycl_kernels::gf3_sycl_forward_enabled_for_shape(config_.input_dim, config_.output_dim)) {
        std::vector<int8_t> in_flat(config_.input_dim, 0);
        const size_t in_lim = std::min(input.size(), config_.input_dim);
        for (size_t i = 0; i < in_lim; ++i) {
            in_flat[i] = static_cast<int8_t>(input[i]);
        }
        if (!sycl_cache_valid_.load(std::memory_order_relaxed)) {
            rebuild_weight_pack5_cache();
        }
        const bool use_bias = config_.use_bias == ternary::Trit::POSITIVE;
        if (use_bias && !bias_cache_valid_.load(std::memory_order_relaxed)) {
            rebuild_bias_pack5_cache();
        }
        std::vector<uint8_t> in_p;
        q::ternary::pack_batch_t5(in_flat, in_p);
        std::vector<uint8_t> out_p;
        if (q_mini_wasm_v2::sycl_kernels::gf3_tropical_linear_forward_sycl(
                in_p,
                weights_pack5_cache_,
                bias_pack5_cache_,
                use_bias,
                config_.input_dim,
                config_.output_dim,
                out_p)) {
            std::vector<int8_t> out_flat;
            q::ternary::unpack_batch_t5(out_p, out_flat, static_cast<uint32_t>(config_.output_dim));
            if (out_flat.size() >= config_.output_dim) {
                std::vector<ternary::Trit> output(config_.output_dim);
                for (size_t j = 0; j < config_.output_dim; ++j) {
                    output[j] = static_cast<ternary::Trit>(out_flat[j]);
                }
                return output;
            }
        }
    }
    throw_gpu_required("Forward", config_.input_dim, config_.output_dim);
#else
    (void)input;
    throw std::runtime_error(
        "[GF3LinearLayer] Forward requires USE_SYCL (GPU); CPU fallback removed.");
#endif
}

void GF3LinearLayer::InitializeWeights(int seed) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> dist(-1, 1);
    
    for (size_t i = 0; i < config_.input_dim; ++i) {
        for (size_t j = 0; j < config_.output_dim; ++j) {
            weights_[i][j] = static_cast<ternary::Trit>(dist(rng));
        }
    }
    
    if (config_.use_bias == ternary::Trit::POSITIVE) {
        for (size_t j = 0; j < config_.output_dim; ++j) {
            bias_[j] = static_cast<ternary::Trit>(dist(rng));
        }
    }
    invalidate_sycl_caches();
    weights_host_dirty_.store(false, std::memory_order_relaxed);
    layer_initialized_.store(true, std::memory_order_relaxed);
}

void GF3LinearLayer::EnsureInitialized(int seed) {
    bool expected = false;
    if (layer_initialized_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        InitializeWeights(seed);
    }
}

void GF3LinearLayer::SetWeight(size_t in_idx, size_t out_idx, ternary::Trit value) {
    if (in_idx < config_.input_dim && out_idx < config_.output_dim) {
        sync_weights_to_host_if_needed();
        weights_[in_idx][out_idx] = value;
        sycl_cache_valid_.store(false, std::memory_order_relaxed);
        weights_host_dirty_.store(false, std::memory_order_relaxed);
    }
}

ternary::Trit GF3LinearLayer::GetWeight(size_t in_idx, size_t out_idx) const {
    sync_weights_to_host_if_needed();
    if (in_idx < config_.input_dim && out_idx < config_.output_dim) {
        return weights_[in_idx][out_idx];
    }
    return ternary::Trit::ZERO;
}

void GF3LinearLayer::SetBias(const std::vector<ternary::Trit>& bias) {
    if (config_.use_bias == ternary::Trit::POSITIVE && bias.size() == config_.output_dim) {
        bias_ = bias;
        bias_cache_valid_.store(false, std::memory_order_relaxed);
    }
}

std::vector<ternary::Trit> GF3LinearLayer::GetWeights() const {
    sync_weights_to_host_if_needed();
    std::vector<ternary::Trit> flat;
    flat.reserve(config_.input_dim * config_.output_dim);
    
    for (const auto& row : weights_) {
        flat.insert(flat.end(), row.begin(), row.end());
    }
    
    return flat;
}

std::vector<ternary::Trit> GF3LinearLayer::GetBias() const {
    return bias_;
}

uint32_t GF3LinearLayer::ComputeGoodness(
    const std::vector<ternary::Trit>& activations
) const {
    uint32_t goodness = 0;
    
    // Goodness = sum of |activation| (count non-zero for ternary)
    for (auto val : activations) {
        int8_t v = static_cast<int8_t>(val);
        goodness += (v != 0) ? 1 : 0;
    }
    
    return goodness;
}

void GF3LinearLayer::UpdateWeightsHebbian(
    const std::vector<ternary::Trit>& input,
    int32_t goodness_delta,
    int8_t learning_rate
) {
    // Hebbian update in GF(3)
    // Δw = learning_rate × goodness_delta × input × output (ternary)
    
    if (goodness_delta == 0) return;

#if defined(USE_SYCL) && USE_SYCL
    if (q_mini_wasm_v2::sycl_kernels::gf3_sycl_forward_enabled_for_shape(config_.input_dim, config_.output_dim)) {
        std::vector<int8_t> in_flat(config_.input_dim, 0);
        const size_t in_lim = std::min(input.size(), config_.input_dim);
        for (size_t i = 0; i < in_lim; ++i) {
            in_flat[i] = static_cast<int8_t>(input[i]);
        }
        std::vector<uint8_t> in_p;
        q::ternary::pack_batch_t5(in_flat, in_p);
        if (!sycl_cache_valid_.load(std::memory_order_relaxed)) {
            rebuild_weight_pack5_cache();
        }
        if (q_mini_wasm_v2::sycl_kernels::gf3_hebbian_update_sycl(
                in_p, goodness_delta, learning_rate, config_.input_dim, config_.output_dim, weights_pack5_cache_)) {
            const int8_t update_sign = (goodness_delta > 0) ? learning_rate : -learning_rate;
            uint64_t nonzero_inputs = 0;
            for (size_t i = 0; i < in_lim; ++i) {
                const int8_t delta = GF3Multiply(update_sign, static_cast<int8_t>(input[i]));
                if (delta != 0) {
                    ++nonzero_inputs;
                }
            }
            const uint64_t touched = nonzero_inputs * static_cast<uint64_t>(config_.output_dim);
            if (touched > 0) {
                g_gf3_hebbian_weight_cell_updates.fetch_add(touched, std::memory_order_relaxed);
            }
            sycl_cache_valid_.store(true, std::memory_order_relaxed);
            weights_host_dirty_.store(true, std::memory_order_relaxed);
            return;
        }
    }
    throw_gpu_required("UpdateWeightsHebbian", config_.input_dim, config_.output_dim);
#else
    (void)input;
    (void)goodness_delta;
    (void)learning_rate;
    throw std::runtime_error(
        "[GF3LinearLayer] UpdateWeightsHebbian requires USE_SYCL (GPU); CPU fallback removed.");
#endif
}

namespace {
void write_raw(std::ostream& os, const void* p, std::streamsize n) {
    os.write(static_cast<const char*>(p), n);
}

void read_raw(std::istream& is, void* p, std::streamsize n) {
    is.read(static_cast<char*>(p), n);
}

bool trit_ok(int8_t v) {
    return v == -1 || v == 0 || v == 1;
}
} // namespace

void GF3LinearLayer::SerializeWeights(std::ostream& os) const {
    sync_weights_to_host_if_needed();
    const uint64_t in_d = static_cast<uint64_t>(config_.input_dim);
    const uint64_t out_d = static_cast<uint64_t>(config_.output_dim);
    const int8_t use_bias = static_cast<int8_t>(config_.use_bias);
    /** Checkpoint tag: POSITIVE marks max-plus layer blob (on-disk layout field). */
    const int8_t layer_kind_tropical = static_cast<int8_t>(ternary::Trit::POSITIVE);
    write_raw(os, &in_d, sizeof(in_d));
    write_raw(os, &out_d, sizeof(out_d));
    write_raw(os, &use_bias, sizeof(use_bias));
    write_raw(os, &layer_kind_tropical, sizeof(layer_kind_tropical));
    for (size_t i = 0; i < config_.input_dim; ++i) {
        for (size_t j = 0; j < config_.output_dim; ++j) {
            int8_t w = static_cast<int8_t>(weights_[i][j]);
            write_raw(os, &w, sizeof(w));
        }
    }
    if (config_.use_bias == ternary::Trit::POSITIVE) {
        for (size_t j = 0; j < bias_.size() && j < config_.output_dim; ++j) {
            int8_t b = static_cast<int8_t>(bias_[j]);
            write_raw(os, &b, sizeof(b));
        }
    }
}

bool GF3LinearLayer::DeserializeWeights(std::istream& is) {
    uint64_t in_d = 0;
    uint64_t out_d = 0;
    int8_t use_bias = 0;
    int8_t layer_kind = 0;
    read_raw(is, &in_d, sizeof(in_d));
    read_raw(is, &out_d, sizeof(out_d));
    read_raw(is, &use_bias, sizeof(use_bias));
    read_raw(is, &layer_kind, sizeof(layer_kind));
    if (!is || in_d != config_.input_dim || out_d != config_.output_dim) {
        return false;
    }
    if (static_cast<ternary::Trit>(use_bias) != config_.use_bias) {
        return false;
    }
    if (static_cast<ternary::Trit>(layer_kind) != ternary::Trit::POSITIVE) {
        return false;
    }
    for (size_t i = 0; i < config_.input_dim; ++i) {
        for (size_t j = 0; j < config_.output_dim; ++j) {
            int8_t w = 0;
            read_raw(is, &w, sizeof(w));
            if (!is || !trit_ok(w)) {
                return false;
            }
            weights_[i][j] = static_cast<ternary::Trit>(w);
        }
    }
    if (config_.use_bias == ternary::Trit::POSITIVE) {
        for (size_t j = 0; j < config_.output_dim; ++j) {
            int8_t b = 0;
            read_raw(is, &b, sizeof(b));
            if (!is || !trit_ok(b)) {
                return false;
            }
            if (j < bias_.size()) {
                bias_[j] = static_cast<ternary::Trit>(b);
            }
        }
    }
    invalidate_sycl_caches();
    weights_host_dirty_.store(false, std::memory_order_relaxed);
    layer_initialized_.store(true, std::memory_order_relaxed);
    return static_cast<bool>(is);
}

uint32_t GF3LinearLayer::GetSparsity() const {
    sync_weights_to_host_if_needed();
    size_t zeros = 0;
    size_t total = config_.input_dim * config_.output_dim;
    
    for (const auto& row : weights_) {
        for (auto val : row) {
            if (val == ternary::Trit::ZERO) zeros++;
        }
    }
    
    // Q24.8 fixed point representation: (zeros << 8) / total
    return static_cast<uint32_t>((zeros << 8) / total);
}

std::vector<uint8_t>& GF3LinearLayer::weights_pack5_buffer_ref() {
    if (!sycl_cache_valid_.load(std::memory_order_relaxed)) {
        rebuild_weight_pack5_cache();
    }
    return weights_pack5_cache_;
}

const std::vector<uint8_t>& GF3LinearLayer::bias_pack5_ref() const {
    if (config_.use_bias != ternary::Trit::POSITIVE) {
        static const std::vector<uint8_t> k_empty;
        return k_empty;
    }
    rebuild_bias_pack5_cache();
    return bias_pack5_cache_;
}

bool GF3LinearLayer::layer_use_bias() const noexcept {
    return config_.use_bias == ternary::Trit::POSITIVE;
}

void GF3LinearLayer::notify_weights_pack5_device_updated() {
    sycl_cache_valid_.store(true, std::memory_order_relaxed);
    weights_host_dirty_.store(true, std::memory_order_relaxed);
    layer_initialized_.store(true, std::memory_order_relaxed);
}

// ============================================================================
// GF3MultiLayerExpert Implementation
// ============================================================================

GF3MultiLayerExpert::GF3MultiLayerExpert(const ExpertConfig& config)
    : ExpertNetwork(config)
{
    AddLayer(config_.hidden_dim);
    for (size_t l = 1; l < config_.num_layers; ++l) {
        AddLayer(config_.hidden_dim);
    }
    AddLayer(config_.output_dim);
}

void GF3MultiLayerExpert::AddLayer(size_t output_dim) {
    size_t input_dim = layers_.empty() ? config_.input_dim : layers_.back()->GetOutputDim();
    
    GF3LinearLayer::LayerConfig layer_config;
    layer_config.input_dim = input_dim;
    layer_config.output_dim = output_dim;
    
    layers_.push_back(std::make_unique<GF3LinearLayer>(layer_config));
}

void GF3MultiLayerExpert::InitializeAllLayers(int seed) {
    int layer_seed = seed;
    for (auto& layer : layers_) {
        layer->InitializeWeights(layer_seed++);
    }
    initialized_ = ternary::Trit::POSITIVE;
}

std::vector<ternary::Trit> GF3MultiLayerExpert::Forward(
    const std::vector<ternary::Trit>& input
) {
    if (initialized_ == ternary::Trit::ZERO) {
        InitializeAllLayers();
    }
    
    std::vector<ternary::Trit> current = input;
    
    for (size_t i = 0; i < layers_.size(); ++i) {
        current = layers_[i]->Forward(current);
        
        // Don't apply activation on final layer if not requested
        if (config_.use_activation == ternary::Trit::ZERO && i == layers_.size() - 1) {
            break;
        }
    }
    
    stats_.forward_calls++;
    return current;
}

int32_t GF3MultiLayerExpert::TrainForwardForward(
    const std::vector<uint8_t>& positive_pack5,
    const std::vector<uint8_t>& negative_pack5
) {
#if defined(USE_SYCL) && USE_SYCL
    std::vector<GF3FfTrainingSlot> one;
    one.reserve(1);
    one.push_back(GF3FfTrainingSlot{this, positive_pack5, negative_pack5});
    if (!TryTrainForwardForwardMultiSlot(one, nullptr, nullptr)) {
        throw std::runtime_error(
            "[GF3MultiLayerExpert] Forward-Forward SYCL path failed (TryTrainForwardForwardMultiSlot); "
            "CPU FF disabled in USE_SYCL builds.");
    }
    const auto g = last_forward_forward_route_goodness();
    return static_cast<int32_t>(g.first) - static_cast<int32_t>(g.second);
#else
    (void)positive_pack5;
    (void)negative_pack5;
    throw std::runtime_error(
        "[GF3MultiLayerExpert] TrainForwardForward requires USE_SYCL (GPU); CPU fallback removed.");
#endif
}

void GF3MultiLayerExpert::SerializeWeights(std::ostream& os) const {
    const uint32_t magic = 0x45334647u; // 'GF3E' when serialized as uint32 LE
    write_raw(os, &magic, sizeof(magic));
    const uint64_t in_dim = static_cast<uint64_t>(config_.input_dim);
    const uint64_t out_dim = static_cast<uint64_t>(config_.output_dim);
    const uint64_t hid_dim = static_cast<uint64_t>(config_.hidden_dim);
    const uint64_t num_layers = static_cast<uint64_t>(config_.num_layers);
    const int8_t use_act = static_cast<int8_t>(config_.use_activation);
    const int8_t energy = static_cast<int8_t>(config_.energy_budget);
    write_raw(os, &in_dim, sizeof(in_dim));
    write_raw(os, &out_dim, sizeof(out_dim));
    write_raw(os, &hid_dim, sizeof(hid_dim));
    write_raw(os, &num_layers, sizeof(num_layers));
    write_raw(os, &use_act, sizeof(use_act));
    write_raw(os, &energy, sizeof(energy));
    const uint32_t n_lin = static_cast<uint32_t>(layers_.size());
    write_raw(os, &n_lin, sizeof(n_lin));
    for (const auto& layer : layers_) {
        layer->SerializeWeights(os);
    }
}

bool GF3MultiLayerExpert::DeserializeWeights(std::istream& is) {
    uint32_t magic = 0;
    read_raw(is, &magic, sizeof(magic));
    if (!is || magic != 0x45334647u) {
        return false;
    }
    uint64_t in_dim = 0;
    uint64_t out_dim = 0;
    uint64_t hid_dim = 0;
    uint64_t num_layers = 0;
    int8_t use_act = 0;
    int8_t energy = 0;
    read_raw(is, &in_dim, sizeof(in_dim));
    read_raw(is, &out_dim, sizeof(out_dim));
    read_raw(is, &hid_dim, sizeof(hid_dim));
    read_raw(is, &num_layers, sizeof(num_layers));
    read_raw(is, &use_act, sizeof(use_act));
    read_raw(is, &energy, sizeof(energy));
    if (!is || in_dim != config_.input_dim || out_dim != config_.output_dim ||
        hid_dim != config_.hidden_dim || num_layers != config_.num_layers ||
        static_cast<ternary::Trit>(use_act) != config_.use_activation ||
        static_cast<ternary::EnergyTrit>(energy) != config_.energy_budget) {
        return false;
    }
    uint32_t n_lin = 0;
    read_raw(is, &n_lin, sizeof(n_lin));
    if (!is || n_lin != layers_.size()) {
        return false;
    }
    for (auto& layer : layers_) {
        if (!layer->DeserializeWeights(is)) {
            return false;
        }
    }
    initialized_ = ternary::Trit::POSITIVE;
    return static_cast<bool>(is);
}

size_t GF3MultiLayerExpert::linear_layer_count() const noexcept {
    return layers_.size();
}

GF3LinearLayer* GF3MultiLayerExpert::mutable_layer(size_t layer_idx) {
    if (layer_idx >= layers_.size()) {
        return nullptr;
    }
    return layers_[layer_idx].get();
}

bool GF3MultiLayerExpert::is_initialized() const noexcept {
    return initialized_ == ternary::Trit::POSITIVE;
}

void GF3MultiLayerExpert::accumulate_batched_ff_stats(int32_t delta) {
    stats_.train_calls++;
    stats_.total_goodness_delta += delta;
    stats_.forward_calls += 2;
}

bool GF3MultiLayerExpert::TryTrainForwardForwardMultiSlot(
    const std::vector<GF3FfTrainingSlot>& slots,
    std::vector<uint32_t>* out_pos_goodness,
    std::vector<uint32_t>* out_neg_goodness) {
    return TryTrainForwardForwardMultiSlot(std::span<const GF3FfTrainingSlot>(slots.data(), slots.size()),
                                           out_pos_goodness, out_neg_goodness);
}

bool GF3MultiLayerExpert::TryTrainForwardForwardMultiSlot(
    std::span<const GF3FfTrainingSlot> slots,
    std::vector<uint32_t>* out_pos_goodness,
    std::vector<uint32_t>* out_neg_goodness) {
#if !defined(USE_SYCL) || !USE_SYCL
    (void)slots;
    (void)out_pos_goodness;
    (void)out_neg_goodness;
    throw std::runtime_error(
        "[GF3MultiLayerExpert] TryTrainForwardForwardMultiSlot requires USE_SYCL (GPU); no CPU fallback.");
#else
    const size_t B = slots.size();
    // Hot path: unconditional stderr per chunk destroyed throughput (tiny B × many chunks).
    static std::atomic<uint64_t> s_ff_entry_i{0};
    const uint64_t entry_i = s_ff_entry_i.fetch_add(1u, std::memory_order_relaxed);
    if (entry_i < 32u || (entry_i & 0x3FFu) == 0u) {
        std::cerr << "[GF3MultiLayerExpert] SYCL FF ENTRY: B=" << B << " slots=" << slots.data()
                  << " (throttled: first 32 + every 1024th)\n";
    }
    if (B < 1) {
        std::cerr << "[GF3MultiLayerExpert] SYCL FF: no slots (B=0)" << std::endl;
        return false;
    }
    for (size_t i = 0; i < slots.size(); ++i) {
        if (!slots[i].expert) {
            std::cerr << "[GF3MultiLayerExpert] SYCL FF: null expert in slot " << i << std::endl;
            return false;
        }
    }

    GF3MultiLayerExpert* e0 = slots[0].expert;
    const size_t L = e0->linear_layer_count();
    if (L == 0) {
        std::cerr << "[GF3MultiLayerExpert] SYCL FF: no layers (L=0)" << std::endl;
        return false;
    }
    const size_t active_cfg = e0->GetConfig().ff_active_internal_layers;
    const size_t active_layers = (active_cfg == 0) ? L : std::max<size_t>(size_t{1}, std::min(active_cfg, L));
    const size_t window_count = (L >= active_layers) ? (L - active_layers + 1) : 1;
    static std::atomic<uint64_t> g_ff_layer_window_counter{0};
    const size_t window_start = static_cast<size_t>(g_ff_layer_window_counter.fetch_add(1, std::memory_order_relaxed) %
                                                    static_cast<uint64_t>(std::max<size_t>(size_t{1}, window_count)));

    for (size_t b = 1; b < B; ++b) {
        if (slots[b].expert->linear_layer_count() != L) {
            std::cerr << "[GF3MultiLayerExpert] SYCL FF: layer count mismatch slot=" << b 
                      << " has=" << slots[b].expert->linear_layer_count() << " expected=" << L << std::endl;
            return false;
        }
    }

    for (size_t li = 0; li < L; ++li) {
        GF3LinearLayer* l0 = e0->mutable_layer(li);
        if (!l0) {
            std::cerr << "[GF3MultiLayerExpert] SYCL FF: null layer at li=" << li << std::endl;
            return false;
        }
        const size_t in0 = l0->GetInputDim();
        const size_t out0 = l0->GetOutputDim();
        const bool bias0 = l0->layer_use_bias();
        for (size_t b = 1; b < B; ++b) {
            GF3LinearLayer* lb = slots[b].expert->mutable_layer(li);
            if (!lb) {
                std::cerr << "[GF3MultiLayerExpert] SYCL FF: null layer at slot=" << b << " li=" << li << std::endl;
                return false;
            }
            if (lb->GetInputDim() != in0 || lb->GetOutputDim() != out0 || lb->layer_use_bias() != bias0) {
                std::cerr << "[GF3MultiLayerExpert] SYCL FF: layer mismatch at slot=" << b << " li=" << li 
                          << " in=" << lb->GetInputDim() << "/" << in0
                          << " out=" << lb->GetOutputDim() << "/" << out0
                          << " bias=" << lb->layer_use_bias() << "/" << bias0 << std::endl;
                return false;
            }
        }
    }

    const size_t in_first = e0->mutable_layer(0)->GetInputDim();
    const size_t row_pack_bytes = (in_first + 4u) / 5u;
    size_t initial_pack_staging = 0;
    if (gf3_ff_mul_overflow_size(B, row_pack_bytes, &initial_pack_staging)) {
        std::cerr << "[GF3MultiLayerExpert] SYCL FF: initial row pack staging overflow B=" << B
                  << " row_pack_bytes=" << row_pack_bytes << std::endl;
        return false;
    }
    std::vector<uint8_t> row_staging(initial_pack_staging);
    for (size_t b = 0; b < B; ++b) {
        std::vector<uint8_t> rp = slots[b].positive_pack5_view();
        std::vector<uint8_t> rn = slots[b].negative_pack5_view();
        if (rp.size() < row_pack_bytes) {
            rp.resize(row_pack_bytes, static_cast<uint8_t>(0));
        } else if (rp.size() > row_pack_bytes) {
            rp.resize(row_pack_bytes);
        }
        if (rn.size() < row_pack_bytes) {
            rn.resize(row_pack_bytes, static_cast<uint8_t>(0));
        } else if (rn.size() > row_pack_bytes) {
            rn.resize(row_pack_bytes);
        }
        std::memcpy(row_staging.data() + b * row_pack_bytes, rp.data(), row_pack_bytes);
    }
    std::vector<uint8_t> cur_pos_packed;
    q::ternary::pack5_encode_global_batch_from_row_major_row_buffers(
        row_staging.data(), B, row_pack_bytes, in_first, cur_pos_packed);
    for (size_t b = 0; b < B; ++b) {
        std::vector<uint8_t> rn = slots[b].negative_pack5_view();
        if (rn.size() < row_pack_bytes) {
            rn.resize(row_pack_bytes, static_cast<uint8_t>(0));
        } else if (rn.size() > row_pack_bytes) {
            rn.resize(row_pack_bytes);
        }
        std::memcpy(row_staging.data() + b * row_pack_bytes, rn.data(), row_pack_bytes);
    }
    std::vector<uint8_t> cur_neg_packed;
    q::ternary::pack5_encode_global_batch_from_row_major_row_buffers(
        row_staging.data(), B, row_pack_bytes, in_first, cur_neg_packed);

    const std::vector<uint8_t> initial_pos_packed = cur_pos_packed;
    std::vector<uint8_t> batch_w_packed;
    std::vector<uint8_t> batch_bias_packed;
    std::vector<uint8_t> out_pos_packed;
    std::vector<uint8_t> out_neg_packed;

    // If we train a window that starts past layer 0, first compute inputs at window_start.
    if (window_start > 0) {
        for (size_t p = 0; p < window_start; ++p) {
            GF3LinearLayer* p_layer = e0->mutable_layer(p);
            if (!p_layer) {
                std::cerr << "[GF3MultiLayerExpert] SYCL FF: null layer e0 pre-window p=" << p << std::endl;
                return false;
            }
            const size_t pin_d = p_layer->GetInputDim();
            const size_t pout_d = p_layer->GetOutputDim();
            const bool p_use_bias = p_layer->layer_use_bias();
            size_t p_w_cells = 0;
            if (gf3_ff_mat_cells_overflow_size(pin_d, pout_d, &p_w_cells)) {
                std::cerr << "[GF3MultiLayerExpert] SYCL FF: pre-window weight cells overflow pin_d=" << pin_d
                          << " pout_d=" << pout_d << " p=" << p << std::endl;
                return false;
            }
            const size_t need_wp = (p_w_cells + 4u) / 5u;
            if (!gf3_ff_resize_row_staging(row_staging, B, need_wp, "pre_window_weights")) {
                return false;
            }
            for (size_t b = 0; b < B; ++b) {
                GF3LinearLayer* lay = slots[b].expert->mutable_layer(p);
                if (!lay) {
                    std::cerr << "[GF3MultiLayerExpert] SYCL FF: null layer slot pre-window b=" << b << " p=" << p
                              << std::endl;
                    return false;
                }
                const int init_seed = 42 + static_cast<int>(p) +
                                      static_cast<int>(reinterpret_cast<std::uintptr_t>(lay) & 0x7fff);
                lay->EnsureInitialized(init_seed);
                const std::vector<uint8_t>& wp_src = lay->weights_pack5_buffer_ref();
                std::vector<uint8_t> wp_copy(wp_src.begin(), wp_src.end());
                if (wp_copy.size() < need_wp) {
                    wp_copy.resize(need_wp, static_cast<uint8_t>(0));
                }
                std::memcpy(row_staging.data() + b * need_wp, wp_copy.data(), need_wp);
            }
            q::ternary::pack5_encode_global_batch_from_row_major_row_buffers(
                row_staging.data(), B, need_wp, p_w_cells, batch_w_packed);
            if (p_use_bias) {
                const size_t need_bp = (pout_d + 4u) / 5u;
                if (!gf3_ff_resize_row_staging(row_staging, B, need_bp, "pre_window_bias")) {
                    return false;
                }
                for (size_t b = 0; b < B; ++b) {
                    GF3LinearLayer* lay = slots[b].expert->mutable_layer(p);
                    if (!lay) {
                        std::cerr << "[GF3MultiLayerExpert] SYCL FF: null layer bias pre-window b=" << b << " p=" << p
                                  << std::endl;
                        return false;
                    }
                    std::vector<uint8_t> bp(lay->bias_pack5_ref().begin(), lay->bias_pack5_ref().end());
                    if (bp.size() < need_bp) {
                        bp.resize(need_bp, static_cast<uint8_t>(0));
                    }
                    std::memcpy(row_staging.data() + b * need_bp, bp.data(), need_bp);
                }
                q::ternary::pack5_encode_global_batch_from_row_major_row_buffers(
                    row_staging.data(), B, need_bp, pout_d, batch_bias_packed);
            } else {
                batch_bias_packed.clear();
            }
            std::vector<uint8_t> p_out_pos, p_out_neg;
            const bool ok_pn = q_mini_wasm_v2::sycl_kernels::gf3_linear_forward_batched_pos_neg_fused_pack5_io_sycl(
                cur_pos_packed, cur_neg_packed, batch_w_packed, batch_bias_packed, p_use_bias, B, pin_d, pout_d,
                p_out_pos, p_out_neg);
            if (!ok_pn) {
                std::cerr << "[GF3MultiLayerExpert] SYCL FF: pre-window forward failed p=" << p << " B=" << B << std::endl;
                return false;
            }
            cur_pos_packed.swap(p_out_pos);
            cur_neg_packed.swap(p_out_neg);
        }
    }

    // Layer batching: process active_layers in chunks to reduce per-kernel memory pressure
    // while keeping high slot parallelism (B slots). This allows deeper networks (100+ layers)
    // without GPU memory deadlock.
    const uint32_t layers_per_batch_cfg = gf3_ff_layers_per_batch_effective();
    const size_t layers_per_batch = (layers_per_batch_cfg > 0) ? static_cast<size_t>(layers_per_batch_cfg) : active_layers;
    const size_t num_layer_batches = (active_layers + layers_per_batch - 1) / layers_per_batch;
    
    if (ff_syctl_verbose_detail()) {
        std::cerr << "[GF3MultiLayerExpert] SYCL FF ENTER: B=" << B << " active_layers=" << active_layers
                  << " layers_per_batch_cfg=" << layers_per_batch_cfg << " num_batches=" << num_layer_batches
                  << " (throttled detail)\n";
    }

    if (layers_per_batch_cfg > 0 && num_layer_batches > 1 && ff_syctl_verbose_detail()) {
        std::cerr << "[GF3MultiLayerExpert] SYCL FF: layer batching active - processing " << active_layers
                  << " layers in " << num_layer_batches << " batches of " << layers_per_batch << " (slots=" << B << ")\n";
    }

    for (size_t layer_batch = 0; layer_batch < num_layer_batches; ++layer_batch) {
        const size_t layer_batch_start = layer_batch * layers_per_batch;
        const size_t layer_batch_end = std::min(layer_batch_start + layers_per_batch, active_layers);

        if (ff_syctl_verbose_detail()) {
            std::cerr << "[GF3MultiLayerExpert] SYCL FF: layer_batch=" << layer_batch << "/" << num_layer_batches
                      << " layers [" << layer_batch_start << "-" << layer_batch_end << ")\n";
        }

        for (size_t w = layer_batch_start; w < layer_batch_end; ++w) {
            const size_t li = window_start + w;
            if (ff_syctl_verbose_detail()) {
                std::cerr << "[GF3MultiLayerExpert] SYCL FF: processing layer w=" << w << " li=" << li << std::endl;
            }
            GF3LinearLayer* ref_layer = e0->mutable_layer(li);
            if (!ref_layer) {
                std::cerr << "[GF3MultiLayerExpert] SYCL FF: null ref_layer forward li=" << li << std::endl;
                return false;
            }
            const size_t in_d = ref_layer->GetInputDim();
            const size_t out_d = ref_layer->GetOutputDim();
            const bool use_bias = ref_layer->layer_use_bias();
            if (ff_syctl_verbose_detail()) {
                std::cerr << "[GF3MultiLayerExpert] SYCL FF: layer li=" << li << " in=" << in_d << " out=" << out_d
                          << " bias=" << use_bias << std::endl;
            }

            size_t w_cells = 0;
            if (gf3_ff_mat_cells_overflow_size(in_d, out_d, &w_cells)) {
                std::cerr << "[GF3MultiLayerExpert] SYCL FF: weight cells overflow in_d=" << in_d << " out_d=" << out_d
                          << " li=" << li << std::endl;
                return false;
            }
            size_t n_w_trits = 0;
            if (gf3_ff_mul_overflow_size(B, w_cells, &n_w_trits)) {
                std::cerr << "[GF3MultiLayerExpert] SYCL FF: n_w_trits overflow B=" << B << " w_cells=" << w_cells
                          << " li=" << li << std::endl;
                return false;
            }
            const size_t need_wp = (w_cells + 4u) / 5u;
            if (!gf3_ff_resize_row_staging(row_staging, B, need_wp, "forward_weights")) {
                return false;
            }
            for (size_t b = 0; b < B; ++b) {
                GF3LinearLayer* lay = slots[b].expert->mutable_layer(li);
                if (!lay) {
                    std::cerr << "[GF3MultiLayerExpert] SYCL FF: null layer slot forward b=" << b << " li=" << li
                              << std::endl;
                    return false;
                }
                const int init_seed = 42 + static_cast<int>(li) +
                                      static_cast<int>(reinterpret_cast<std::uintptr_t>(lay) & 0x7fff);
                lay->EnsureInitialized(init_seed);
                const std::vector<uint8_t>& wp_src = lay->weights_pack5_buffer_ref();
                std::vector<uint8_t> wp_copy(wp_src.begin(), wp_src.end());
                if (wp_copy.size() < need_wp) {
                    wp_copy.resize(need_wp, static_cast<uint8_t>(0));
                }
                std::memcpy(row_staging.data() + b * need_wp, wp_copy.data(), need_wp);
            }
            q::ternary::pack5_encode_global_batch_from_row_major_row_buffers(
                row_staging.data(), B, need_wp, w_cells, batch_w_packed);

            if (use_bias) {
                const size_t need_bp = (out_d + 4u) / 5u;
                if (!gf3_ff_resize_row_staging(row_staging, B, need_bp, "forward_bias")) {
                    return false;
                }
                for (size_t b = 0; b < B; ++b) {
                    GF3LinearLayer* lay = slots[b].expert->mutable_layer(li);
                    if (!lay) {
                        std::cerr << "[GF3MultiLayerExpert] SYCL FF: null layer bias forward b=" << b << " li=" << li
                                  << std::endl;
                        return false;
                    }
                    std::vector<uint8_t> bp(lay->bias_pack5_ref().begin(), lay->bias_pack5_ref().end());
                    if (bp.size() < need_bp) {
                        bp.resize(need_bp, static_cast<uint8_t>(0));
                    }
                    std::memcpy(row_staging.data() + b * need_bp, bp.data(), need_bp);
                }
                q::ternary::pack5_encode_global_batch_from_row_major_row_buffers(
                    row_staging.data(), B, need_bp, out_d, batch_bias_packed);
            } else {
                batch_bias_packed.clear();
            }

            const size_t need_in_b = (B * in_d + 4u) / 5u;
            const size_t need_w_b = (n_w_trits + 4u) / 5u;
            if (cur_pos_packed.size() < need_in_b) {
                cur_pos_packed.resize(need_in_b, static_cast<uint8_t>(0));
            }
            if (cur_neg_packed.size() < need_in_b) {
                cur_neg_packed.resize(need_in_b, static_cast<uint8_t>(0));
            }
            if (batch_w_packed.size() < need_w_b) {
                std::cerr << "[GF3MultiLayerExpert] SYCL FF: stacked weight pack5 undersized li=" << li
                          << " B=" << B << " have_w_bytes=" << batch_w_packed.size() << " need=" << need_w_b << std::endl;
                return false;
            }
            if (use_bias) {
                const size_t need_bias_b = (B * out_d + 4u) / 5u;
                if (batch_bias_packed.size() < need_bias_b) {
                    std::cerr << "[GF3MultiLayerExpert] SYCL FF: batched bias pack5 undersized li=" << li
                              << " have=" << batch_bias_packed.size() << " need=" << need_bias_b << std::endl;
                    return false;
                }
            }
            if (!q_mini_wasm_v2::sycl_kernels::gf3_sycl_forward_enabled_for_shape(in_d, out_d)) {
                std::cerr << "[GF3MultiLayerExpert] SYCL FF: invalid layer dimensions (in_d=" << in_d << " out_d=" << out_d
                          << " li=" << li << ")" << std::endl;
                return false;
            }

            const bool ok_pn = q_mini_wasm_v2::sycl_kernels::gf3_linear_forward_batched_pos_neg_fused_pack5_io_sycl(
                cur_pos_packed, cur_neg_packed, batch_w_packed, batch_bias_packed, use_bias, B, in_d, out_d,
                out_pos_packed, out_neg_packed);
            if (ff_syctl_verbose_detail()) {
                std::cerr << "[GF3MultiLayerExpert] SYCL FF: fused pos+neg forward li=" << li << " B=" << B
                          << " ok=" << (ok_pn ? 1 : 0) << std::endl;
            }
            if (!ok_pn) {
                std::cerr << "[GF3MultiLayerExpert] SYCL FF: fused batched forward failed li=" << li << " B=" << B
                          << " in_bytes=" << cur_pos_packed.size() << "/"
                          << need_in_b << " w_bytes=" << batch_w_packed.size() << "/" << need_w_b << std::endl;
                return false;
            }

            cur_pos_packed.swap(out_pos_packed);
            cur_neg_packed.swap(out_neg_packed);
        }
    
        // Sync queue between layer batches to free GPU memory before processing next batch
        // This is the key to supporting deep networks (100+ layers) without memory deadlock
        if (layers_per_batch_cfg > 0 && layer_batch + 1 < num_layer_batches) {
            try {
                sycl::queue& q = q_mini_wasm_v2::sycl_kernels::gf3_layers_queue_for_thread();
                q.wait_and_throw();
            } catch (const sycl::exception& e) {
                std::cerr << "[GF3MultiLayerExpert] SYCL FF: layer batch sync failed: " << e.what() << std::endl;
                return false;
            }
        }
    }

    GF3LinearLayer* last_forward_layer = e0->mutable_layer(L - 1);
    if (!last_forward_layer) {
        std::cerr << "[GF3MultiLayerExpert] SYCL FF: null layer L-1=" << (L - 1) << std::endl;
        return false;
    }
    const size_t out_last = last_forward_layer->GetOutputDim();
    // Goodness scan indexes trits linearly as row-major B × out_last; packed bytes must cover tri indices
    // [0, B*out_last). Without this guard, read_trit_t5_at can read past cur_*_packed (undefined → AV).
    const size_t goodness_trits = B * out_last;
    const size_t need_goodness_pack_bytes = (goodness_trits + 4u) / 5u;
    // SYCL forwards occasionally return slightly short Pack5 buffers; pad with 0 (implicit ZERO trits) instead of
    // reading past the end — matches unpack_batch_t5 padding semantics and prevents AV on goodness scan.
    if (cur_pos_packed.size() < need_goodness_pack_bytes) {
        static std::atomic<int> g_ff_goodness_pad_warns_pos{0};
        if (g_ff_goodness_pad_warns_pos.fetch_add(1) < 6) {
            std::cerr << "[GF3MultiLayerExpert] SYCL FF: padding pos activations for goodness scan "
                      << cur_pos_packed.size() << " -> " << need_goodness_pack_bytes << " bytes (B=" << B
                      << " out_last=" << out_last << ")\n";
        }
        cur_pos_packed.resize(need_goodness_pack_bytes, static_cast<uint8_t>(0));
    }
    if (cur_neg_packed.size() < need_goodness_pack_bytes) {
        static std::atomic<int> g_ff_goodness_pad_warns_neg{0};
        if (g_ff_goodness_pad_warns_neg.fetch_add(1) < 6) {
            std::cerr << "[GF3MultiLayerExpert] SYCL FF: padding neg activations for goodness scan "
                      << cur_neg_packed.size() << " -> " << need_goodness_pack_bytes << " bytes (B=" << B
                      << " out_last=" << out_last << ")\n";
        }
        cur_neg_packed.resize(need_goodness_pack_bytes, static_cast<uint8_t>(0));
    }
    std::vector<uint32_t> pg_vec(B);
    std::vector<uint32_t> ng_vec(B);
    std::vector<int32_t> delta(B);
    for (size_t b = 0; b < B; ++b) {
        uint32_t pg = 0;
        uint32_t ng = 0;
        for (size_t j = 0; j < out_last; ++j) {
            const size_t tri_p = b * out_last + j;
            const int8_t tp = q::ternary::read_trit_t5_at(cur_pos_packed.data(), tri_p);
            if (tp != 0) {
                ++pg;
            }
            const int8_t tn = q::ternary::read_trit_t5_at(cur_neg_packed.data(), tri_p);
            if (tn != 0) {
                ++ng;
            }
        }
        pg_vec[b] = pg;
        ng_vec[b] = ng;
        delta[b] = static_cast<int32_t>(pg) - static_cast<int32_t>(ng);
    }

    std::vector<uint8_t> recompute_cur_packed;
    std::vector<uint8_t> recompute_out_packed;
    std::cerr << "[GF3MultiLayerExpert] SYCL FF: starting Hebbian updates for " << active_layers << " layers" << std::endl;
    for (size_t wi = active_layers; wi-- > 0;) {
        const size_t li = window_start + wi;
        std::cerr << "[GF3MultiLayerExpert] SYCL FF: Hebbian layer li=" << li << " (wi=" << wi << ")" << std::endl;
        GF3LinearLayer* ref_layer = e0->mutable_layer(li);
        if (!ref_layer) {
            std::cerr << "[GF3MultiLayerExpert] SYCL FF: null ref_layer Hebbian li=" << li << std::endl;
            return false;
        }
        const size_t in_d = ref_layer->GetInputDim();
        const size_t out_d = ref_layer->GetOutputDim();
        std::vector<uint8_t> layer_in_packed;
        if (li == 0) {
            layer_in_packed = initial_pos_packed;
            std::cerr << "[GF3MultiLayerExpert] SYCL FF: Hebbian li=0 using initial pack" << std::endl;
        } else {
            recompute_cur_packed = initial_pos_packed;
            std::cerr << "[GF3MultiLayerExpert] SYCL FF: Hebbian recomputing " << li << " layers..." << std::endl;
            for (size_t p = 0; p < li; ++p) {
                GF3LinearLayer* p_layer = e0->mutable_layer(p);
                if (!p_layer) {
                    std::cerr << "[GF3MultiLayerExpert] SYCL FF: null p_layer Hebbian recompute p=" << p << std::endl;
                    return false;
                }
                const size_t pin_d = p_layer->GetInputDim();
                const size_t pout_d = p_layer->GetOutputDim();
                const bool p_use_bias = p_layer->layer_use_bias();
                size_t p_w_cells = 0;
                if (gf3_ff_mat_cells_overflow_size(pin_d, pout_d, &p_w_cells)) {
                    std::cerr << "[GF3MultiLayerExpert] SYCL FF: Hebbian recompute weight cells overflow pin_d="
                              << pin_d << " pout_d=" << pout_d << " p=" << p << std::endl;
                    return false;
                }
                const size_t need_wp = (p_w_cells + 4u) / 5u;
                if (!gf3_ff_resize_row_staging(row_staging, B, need_wp, "hebbian_recompute_w")) {
                    return false;
                }
                for (size_t b = 0; b < B; ++b) {
                    GF3LinearLayer* lay = slots[b].expert->mutable_layer(p);
                    if (!lay) {
                        std::cerr << "[GF3MultiLayerExpert] SYCL FF: null lay Hebbian recompute b=" << b << " p=" << p
                                  << std::endl;
                        return false;
                    }
                    const int init_seed = 42 + static_cast<int>(p) +
                                          static_cast<int>(reinterpret_cast<std::uintptr_t>(lay) & 0x7fff);
                    lay->EnsureInitialized(init_seed);
                    const std::vector<uint8_t>& wp_src = lay->weights_pack5_buffer_ref();
                    std::vector<uint8_t> wp_copy(wp_src.begin(), wp_src.end());
                    if (wp_copy.size() < need_wp) {
                        wp_copy.resize(need_wp, static_cast<uint8_t>(0));
                    }
                    std::memcpy(row_staging.data() + b * need_wp, wp_copy.data(), need_wp);
                }
                q::ternary::pack5_encode_global_batch_from_row_major_row_buffers(
                    row_staging.data(), B, need_wp, p_w_cells, batch_w_packed);
                if (p_use_bias) {
                    const size_t need_bp = (pout_d + 4u) / 5u;
                    if (!gf3_ff_resize_row_staging(row_staging, B, need_bp, "hebbian_recompute_bias")) {
                        return false;
                    }
                    for (size_t b = 0; b < B; ++b) {
                        GF3LinearLayer* lay = slots[b].expert->mutable_layer(p);
                        if (!lay) {
                            std::cerr << "[GF3MultiLayerExpert] SYCL FF: null lay bias Hebbian recompute b=" << b
                                      << " p=" << p << std::endl;
                            return false;
                        }
                        std::vector<uint8_t> bp(lay->bias_pack5_ref().begin(), lay->bias_pack5_ref().end());
                        if (bp.size() < need_bp) {
                            bp.resize(need_bp, static_cast<uint8_t>(0));
                        }
                        std::memcpy(row_staging.data() + b * need_bp, bp.data(), need_bp);
                    }
                    q::ternary::pack5_encode_global_batch_from_row_major_row_buffers(
                        row_staging.data(), B, need_bp, pout_d, batch_bias_packed);
                } else {
                    batch_bias_packed.clear();
                }
                const size_t need_pin_b = (B * pin_d + 4u) / 5u;
                if (recompute_cur_packed.size() < need_pin_b) {
                    recompute_cur_packed.resize(need_pin_b, static_cast<uint8_t>(0));
                }
                std::cerr << "[GF3MultiLayerExpert] SYCL FF: Hebbian kernel p=" << p << " B=" << B 
                          << " in=" << pin_d << " out=" << pout_d << std::endl;
                const bool ok_recompute =
                    q_mini_wasm_v2::sycl_kernels::gf3_tropical_linear_forward_batched_pack5_io_sycl(
                        recompute_cur_packed, batch_w_packed, batch_bias_packed, p_use_bias, B, pin_d, pout_d,
                        recompute_out_packed, false);
                std::cerr << "[GF3MultiLayerExpert] SYCL FF: Hebbian kernel p=" << p << " returned ok=" << ok_recompute << std::endl;
                if (!ok_recompute) {
                    std::cerr << "[GF3MultiLayerExpert] SYCL FF: recompute forward failed p=" << p << " B=" << B
                              << " in_d=" << pin_d << " out_d=" << pout_d << std::endl;
                    return false;
                }
                recompute_cur_packed.swap(recompute_out_packed);
            }
            layer_in_packed = recompute_cur_packed;
        }
        const size_t need_in_h = (B * in_d + 4u) / 5u;
        if (layer_in_packed.size() < need_in_h) {
            layer_in_packed.resize(need_in_h, static_cast<uint8_t>(0));
        }

        size_t w_cells_h = 0;
        if (gf3_ff_mat_cells_overflow_size(in_d, out_d, &w_cells_h)) {
            std::cerr << "[GF3MultiLayerExpert] SYCL FF (Hebbian): weight cells overflow in_d=" << in_d
                      << " out_d=" << out_d << " li=" << li << std::endl;
            return false;
        }
        size_t n_w_trits = 0;
        if (gf3_ff_mul_overflow_size(B, w_cells_h, &n_w_trits)) {
            std::cerr << "[GF3MultiLayerExpert] SYCL FF (Hebbian): n_w_trits overflow B=" << B << " w_cells=" << w_cells_h
                      << " li=" << li << std::endl;
            return false;
        }
        const size_t need_wp = (w_cells_h + 4u) / 5u;
        if (!gf3_ff_resize_row_staging(row_staging, B, need_wp, "hebbian_gather_w")) {
            return false;
        }
        for (size_t b = 0; b < B; ++b) {
            GF3LinearLayer* lay = slots[b].expert->mutable_layer(li);
            if (!lay) {
                std::cerr << "[GF3MultiLayerExpert] SYCL FF: null lay Hebbian gather b=" << b << " li=" << li
                          << std::endl;
                return false;
            }
            const int init_seed = 42 + static_cast<int>(li) +
                                  static_cast<int>(reinterpret_cast<std::uintptr_t>(lay) & 0x7fff);
            lay->EnsureInitialized(init_seed);
            const std::vector<uint8_t>& wp_src = lay->weights_pack5_buffer_ref();
            std::vector<uint8_t> wp_copy(wp_src.begin(), wp_src.end());
            if (wp_copy.size() < need_wp) {
                wp_copy.resize(need_wp, static_cast<uint8_t>(0));
            }
            std::memcpy(row_staging.data() + b * need_wp, wp_copy.data(), need_wp);
        }
        q::ternary::pack5_encode_global_batch_from_row_major_row_buffers(
            row_staging.data(), B, need_wp, w_cells_h, batch_w_packed);

        const size_t need_w_h = (n_w_trits + 4u) / 5u;
        if (batch_w_packed.size() < need_w_h) {
            std::cerr << "[GF3MultiLayerExpert] SYCL FF (Hebbian): stacked weight pack5 undersized li=" << li
                      << " have=" << batch_w_packed.size() << " need=" << need_w_h << std::endl;
            return false;
        }

        std::cerr << "[GF3MultiLayerExpert] SYCL FF: Hebbian UPDATE kernel li=" << li << " B=" << B 
                  << " in=" << in_d << " out=" << out_d << std::endl;
        if (!q_mini_wasm_v2::sycl_kernels::gf3_hebbian_update_batched_pack5_io_sycl(
                layer_in_packed, delta, static_cast<int8_t>(1), B, in_d, out_d, batch_w_packed, true)) {
            std::cerr << "[GF3MultiLayerExpert] SYCL FF: batched Hebbian failed li=" << li << " B=" << B
                      << " in_d=" << in_d << " out_d=" << out_d << " layer_in_bytes=" << layer_in_packed.size() << "/"
                      << need_in_h << " w_bytes=" << batch_w_packed.size() << "/" << need_w_h << std::endl;
            return false;
        }
        std::cerr << "[GF3MultiLayerExpert] SYCL FF: Hebbian UPDATE done li=" << li << std::endl;

        if (!gf3_ff_resize_row_staging(row_staging, B, need_wp, "hebbian_scatter_decode")) {
            return false;
        }
        q::ternary::pack5_decode_global_batch_to_row_major_row_buffers(
            batch_w_packed.data(), batch_w_packed.size(), B, need_wp, w_cells_h, row_staging.data());
        for (size_t b = 0; b < B; ++b) {
            GF3LinearLayer* lay = slots[b].expert->mutable_layer(li);
            if (!lay) {
                std::cerr << "[GF3MultiLayerExpert] SYCL FF: null lay Hebbian scatter b=" << b << " li=" << li
                          << std::endl;
                return false;
            }
            std::vector<uint8_t>& dst = lay->weights_pack5_buffer_ref();
            dst.resize(need_wp);
            std::memcpy(dst.data(), row_staging.data() + b * need_wp, need_wp);
            lay->notify_weights_pack5_device_updated();
        }
    }

    {
        uint64_t hebb_cells = 0;
        for (size_t w = 0; w < active_layers; ++w) {
            const size_t li = window_start + w;
            GF3LinearLayer* ref_layer = e0->mutable_layer(li);
            if (!ref_layer) {
                std::cerr << "[GF3MultiLayerExpert] SYCL FF: null ref_layer stats li=" << li << std::endl;
                return false;
            }
            const size_t in_d = ref_layer->GetInputDim();
            const size_t out_d = ref_layer->GetOutputDim();
            for (size_t b = 0; b < B; ++b) {
                if (delta[b] != 0) {
                    hebb_cells += static_cast<uint64_t>(in_d) * static_cast<uint64_t>(out_d);
                }
            }
        }
        if (hebb_cells > 0) {
            g_gf3_hebbian_weight_cell_updates.fetch_add(hebb_cells, std::memory_order_relaxed);
        }
    }
    std::cerr << "[GF3MultiLayerExpert] SYCL FF: Hebbian phase COMPLETE for " << active_layers << " layers" << std::endl;

    if (out_pos_goodness) {
        *out_pos_goodness = pg_vec;
    }
    if (out_neg_goodness) {
        *out_neg_goodness = ng_vec;
    }

    for (size_t b = 0; b < B; ++b) {
        slots[b].expert->record_ff_route_output_goodness(pg_vec[b], ng_vec[b]);
        slots[b].expert->accumulate_batched_ff_stats(delta[b]);
    }

    std::cerr << "[GF3MultiLayerExpert] SYCL FF EXIT: B=" << B << " layers=" << active_layers << " DONE" << std::endl;
    return true;
#endif
}

bool GF3MultiLayerExpert::TryTrainForwardForwardBatched(
    const std::vector<GF3MultiLayerExpert*>& experts,
    const std::vector<uint8_t>& positive_pack5,
    const std::vector<uint8_t>& negative_pack5) {
#if !defined(USE_SYCL) || !USE_SYCL
    (void)experts;
    (void)positive_pack5;
    (void)negative_pack5;
    throw std::runtime_error(
        "[GF3MultiLayerExpert] TryTrainForwardForwardBatched requires USE_SYCL (GPU); no CPU fallback.");
#else
    std::vector<GF3FfTrainingSlot> slots;
    slots.reserve(experts.size());
    for (auto* e : experts) {
        slots.push_back(GF3FfTrainingSlot{e, positive_pack5, negative_pack5});
    }
    return TryTrainForwardForwardMultiSlot(slots, nullptr, nullptr);
#endif
}

std::vector<ternary::Trit> GF3MultiLayerExpert::TernaryActivation(
    const std::vector<int32_t>& pre_activations
) {
    std::vector<ternary::Trit> output;
    output.reserve(pre_activations.size());
    
    for (auto val : pre_activations) {
        // Threshold at ±0.5
        if (val > 0) {
            output.push_back(ternary::Trit::POSITIVE);
        } else if (val < 0) {
            output.push_back(ternary::Trit::NEGATIVE);
        } else {
            output.push_back(ternary::Trit::ZERO);
        }
    }
    
    return output;
}

} // namespace q_mini_wasm_v2::core::moe
