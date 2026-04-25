// Full MoE router checkpoint (RUF2) + legacy RTW1 reader — friends of MoERouter.
#include "router.hpp"
#include <cstring>
#include <istream>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

namespace q_mini_wasm_v2::core::moe {

namespace {

constexpr uint32_t kRouterChunkRTW1 =
    static_cast<uint32_t>('R') | (static_cast<uint32_t>('T') << 8) |
    (static_cast<uint32_t>('W') << 16) | (static_cast<uint32_t>('1') << 24);

constexpr uint32_t kRouterFullRUF2 =
    static_cast<uint32_t>('R') | (static_cast<uint32_t>('U') << 8) |
    (static_cast<uint32_t>('F') << 16) | (static_cast<uint32_t>('2') << 24);

constexpr uint32_t kRuf2SchemaVersion = 1;

bool trit_ok(int8_t v) {
    return v == -1 || v == 0 || v == 1;
}

void w_u8(std::ostream& os, uint8_t v) {
    os.write(reinterpret_cast<const char*>(&v), 1);
}

void w_u32(std::ostream& os, uint32_t v) {
    os.write(reinterpret_cast<const char*>(&v), 4);
}

void w_u64(std::ostream& os, uint64_t v) {
    os.write(reinterpret_cast<const char*>(&v), 8);
}

void w_i8(std::ostream& os, int8_t v) {
    os.write(reinterpret_cast<const char*>(&v), 1);
}

void w_i32(std::ostream& os, int32_t v) {
    os.write(reinterpret_cast<const char*>(&v), 4);
}

void w_sz(std::ostream& os, size_t s) {
    w_u64(os, static_cast<uint64_t>(s));
}

bool r_u8(std::istream& is, uint8_t& v) {
    is.read(reinterpret_cast<char*>(&v), 1);
    return static_cast<bool>(is);
}

bool r_u32(std::istream& is, uint32_t& v) {
    is.read(reinterpret_cast<char*>(&v), 4);
    return static_cast<bool>(is);
}

bool r_u64(std::istream& is, uint64_t& v) {
    is.read(reinterpret_cast<char*>(&v), 8);
    return static_cast<bool>(is);
}

bool r_i8(std::istream& is, int8_t& v) {
    is.read(reinterpret_cast<char*>(&v), 1);
    return static_cast<bool>(is);
}

bool r_i32(std::istream& is, int32_t& v) {
    is.read(reinterpret_cast<char*>(&v), 4);
    return static_cast<bool>(is);
}

bool r_sz(std::istream& is, size_t& out) {
    uint64_t v = 0;
    if (!r_u64(is, v)) {
        return false;
    }
    out = static_cast<size_t>(v);
    return true;
}

void w_vec_u32(std::ostream& os, const std::vector<uint32_t>& v) {
    w_sz(os, v.size());
    for (uint32_t x : v) {
        w_u32(os, x);
    }
}

bool r_vec_u32(std::istream& is, std::vector<uint32_t>& v) {
    size_t n = 0;
    if (!r_sz(is, n) || n > 1ull << 25) {
        return false;
    }
    v.resize(n);
    for (size_t i = 0; i < n; ++i) {
        uint32_t x = 0;
        if (!r_u32(is, x)) {
            return false;
        }
        v[i] = x;
    }
    return true;
}

void w_vec_sz(std::ostream& os, const std::vector<size_t>& v) {
    w_sz(os, v.size());
    for (size_t x : v) {
        w_u64(os, static_cast<uint64_t>(x));
    }
}

bool r_vec_sz(std::istream& is, std::vector<size_t>& v, size_t expected) {
    size_t n = 0;
    if (!r_sz(is, n) || n != expected) {
        return false;
    }
    v.resize(n);
    for (size_t i = 0; i < n; ++i) {
        uint64_t x = 0;
        if (!r_u64(is, x)) {
            return false;
        }
        v[i] = static_cast<size_t>(x);
    }
    return true;
}

void w_vec_i8(std::ostream& os, const std::vector<int8_t>& v) {
    w_sz(os, v.size());
    for (int8_t x : v) {
        w_i8(os, x);
    }
}

bool r_vec_i8(std::istream& is, std::vector<int8_t>& v, size_t max_n) {
    size_t n = 0;
    if (!r_sz(is, n) || n > max_n) {
        return false;
    }
    v.resize(n);
    for (size_t i = 0; i < n; ++i) {
        int8_t x = 0;
        if (!r_i8(is, x)) {
            return false;
        }
        v[i] = x;
    }
    return true;
}

void w_mat_i32(std::ostream& os, const std::vector<std::vector<int32_t>>& m) {
    w_sz(os, m.size());
    for (const auto& row : m) {
        w_sz(os, row.size());
        for (int32_t x : row) {
            w_i32(os, x);
        }
    }
}

bool r_mat_i32(std::istream& is, std::vector<std::vector<int32_t>>& m, size_t max_rows, size_t max_cols) {
    size_t rows = 0;
    if (!r_sz(is, rows) || rows > max_rows) {
        return false;
    }
    m.resize(rows);
    for (size_t r = 0; r < rows; ++r) {
        size_t cols = 0;
        if (!r_sz(is, cols) || cols > max_cols) {
            return false;
        }
        m[r].resize(cols);
        for (size_t c = 0; c < cols; ++c) {
            if (!r_i32(is, m[r][c])) {
                return false;
            }
        }
    }
    return true;
}

void w_mat_u32(std::ostream& os, const std::vector<std::vector<uint32_t>>& m) {
    w_sz(os, m.size());
    for (const auto& row : m) {
        w_vec_u32(os, row);
    }
}

bool r_mat_u32(std::istream& is, std::vector<std::vector<uint32_t>>& m, size_t max_rows, size_t max_cols) {
    size_t rows = 0;
    if (!r_sz(is, rows) || rows > max_rows) {
        return false;
    }
    m.resize(rows);
    for (size_t r = 0; r < rows; ++r) {
        if (!r_vec_u32(is, m[r])) {
            return false;
        }
        if (m[r].size() > max_cols) {
            return false;
        }
    }
    return true;
}

void w_mat_trit(std::ostream& os, const std::vector<std::vector<ternary::Trit>>& m) {
    w_sz(os, m.size());
    for (const auto& row : m) {
        w_sz(os, row.size());
        for (ternary::Trit t : row) {
            w_i8(os, static_cast<int8_t>(t));
        }
    }
}

bool r_mat_trit(std::istream& is, std::vector<std::vector<ternary::Trit>>& m, size_t max_rows, size_t max_cols) {
    size_t rows = 0;
    if (!r_sz(is, rows) || rows > max_rows) {
        return false;
    }
    m.resize(rows);
    for (size_t r = 0; r < rows; ++r) {
        size_t cols = 0;
        if (!r_sz(is, cols) || cols > max_cols) {
            return false;
        }
        m[r].resize(cols);
        for (size_t c = 0; c < cols; ++c) {
            int8_t b = 0;
            if (!r_i8(is, b) || !trit_ok(b)) {
                return false;
            }
            m[r][c] = static_cast<ternary::Trit>(b);
        }
    }
    return true;
}

void w_vec_vec_sz(std::ostream& os, const std::vector<std::vector<size_t>>& m) {
    w_sz(os, m.size());
    for (const auto& row : m) {
        w_vec_sz(os, row);
    }
}

bool r_vec_vec_sz(std::istream& is, std::vector<std::vector<size_t>>& m, size_t max_rows, size_t max_inner) {
    size_t rows = 0;
    if (!r_sz(is, rows) || rows > max_rows) {
        return false;
    }
    m.resize(rows);
    for (size_t r = 0; r < rows; ++r) {
        size_t inner = 0;
        if (!r_sz(is, inner) || inner > max_inner) {
            return false;
        }
        m[r].resize(inner);
        for (size_t c = 0; c < inner; ++c) {
            uint64_t x = 0;
            if (!r_u64(is, x)) {
                return false;
            }
            m[r][c] = static_cast<size_t>(x);
        }
    }
    return true;
}

} // namespace

void moe_router_checkpoint_write(MoERouter& r, std::ostream& os) {
    r.ensure_routing_weights_initialized();

    os.write(reinterpret_cast<const char*>(&kRouterFullRUF2), sizeof(kRouterFullRUF2));
    w_u32(os, kRuf2SchemaVersion);

    const uint64_t ne = static_cast<uint64_t>(r.config_.total_experts);
    const uint64_t rq = static_cast<uint64_t>(r.config_.routing_qutrits);
    w_u64(os, ne);
    w_u64(os, rq);

    w_u64(os, static_cast<uint64_t>(r.current_active_experts_));
    w_u32(os, r.last_load_measurement_);
    w_u8(os, r.routing_weights_initialized_ ? uint8_t{1} : uint8_t{0});
    w_u8(os, r.entanglement_initialized_ ? uint8_t{1} : uint8_t{0});
    w_u8(os, r.advanced_selection_initialized_ ? uint8_t{1} : uint8_t{0});
    w_u32(os, r.ternary_seed_);
    w_u32(os, r.selection_episode_);
    w_i8(os, static_cast<int8_t>(r.tropical_initialized_));
    w_i8(os, static_cast<int8_t>(r.energy_per_expert_));
    w_i8(os, static_cast<int8_t>(r.load_balancing_accuracy_));
    w_u32(os, r.scaling_decision_count_);
    w_u32(os, r.last_scaling_time_);
    w_u32(os, r.thermal_current_);
    w_u32(os, r.priority_preemptions_);
    w_u32(os, r.load_balancing_episodes_);

    w_u32(os, r.entangled_config_.entanglement_strength);
    w_u32(os, r.entangled_config_.coherence_threshold);
    w_u32(os, r.entangled_config_.efficiency_target);
    w_u64(os, static_cast<uint64_t>(r.entangled_config_.measurement_shots));
    w_i8(os, static_cast<int8_t>(r.entangled_config_.use_adaptive_entanglement));

    {
        std::ostringstream oss;
        oss << r.rng_;
        const std::string blob = oss.str();
        w_sz(os, blob.size());
        if (!blob.empty()) {
            os.write(blob.data(), static_cast<std::streamsize>(blob.size()));
        }
    }

    for (size_t e = 0; e < r.config_.total_experts; ++e) {
        for (size_t j = 0; j < r.config_.routing_qutrits; ++j) {
            const int8_t w = static_cast<int8_t>(r.routing_weights_[e][j]);
            w_i8(os, w);
        }
    }

    for (size_t e = 0; e < r.config_.total_experts; ++e) {
        const auto& mat = r.expert_weights_[e];
        w_mat_trit(os, mat);
    }

    w_mat_i32(os, r.entanglement_coupling_);
    w_mat_i32(os, r.state_action_scores_);

    w_sz(os, r.last_system_state_.size());
    for (int32_t x : r.last_system_state_) {
        w_i32(os, x);
    }

    w_vec_u32(os, r.load_history_);
    w_vec_u32(os, r.latency_history_);

    w_sz(os, r.energy_history_.size());
    for (ternary::EnergyTrit et : r.energy_history_) {
        w_i8(os, static_cast<int8_t>(et));
    }

    w_mat_u32(os, r.priority_performance_history_);
    w_vec_u32(os, r.priority_queue_sizes_);

    w_sz(os, r.priority_fairness_metrics_.size());
    for (ternary::ProbTrit pt : r.priority_fairness_metrics_) {
        w_i8(os, static_cast<int8_t>(pt));
    }

    w_vec_vec_sz(os, r.priority_reserved_experts_);
    w_mat_i32(os, r.expert_performance_history_);
    w_mat_u32(os, r.load_prediction_history_);

    w_sz(os, r.bottleneck_flags_.size());
    for (ternary::Trit t : r.bottleneck_flags_) {
        w_i8(os, static_cast<int8_t>(t));
    }

    w_sz(os, r.ml_balancing_weights_.size());
    for (const auto& row : r.ml_balancing_weights_) {
        w_sz(os, row.size());
        for (ternary::ProbTrit pt : row) {
            w_i8(os, static_cast<int8_t>(pt));
        }
    }

    w_vec_vec_sz(os, r.expert_groups_);
    w_vec_sz(os, r.expert_loads_);
    w_vec_sz(os, r.expert_request_counts_);
}

bool moe_router_checkpoint_read(MoERouter& r, std::istream& is, uint32_t checkpoint_fmt) {
    if (checkpoint_fmt < 4) {
        return true;
    }

    uint32_t magic = 0;
    is.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    if (!is) {
        return false;
    }

    if (magic == kRouterChunkRTW1) {
        uint64_t ne = 0;
        uint64_t rq = 0;
        if (!r_u64(is, ne) || !r_u64(is, rq)) {
            return false;
        }
        if (ne != static_cast<uint64_t>(r.config_.total_experts) ||
            rq != static_cast<uint64_t>(r.config_.routing_qutrits)) {
            return false;
        }
        uint8_t inited = 0;
        if (!r_u8(is, inited)) {
            return false;
        }
        (void)inited;
        r.routing_weights_.resize(r.config_.total_experts);
        for (size_t e = 0; e < r.config_.total_experts; ++e) {
            r.routing_weights_[e].resize(r.config_.routing_qutrits);
            for (size_t j = 0; j < r.config_.routing_qutrits; ++j) {
                int8_t w = 0;
                if (!r_i8(is, w) || !trit_ok(w)) {
                    return false;
                }
                r.routing_weights_[e][j] = static_cast<ternary::Trit>(w);
            }
        }
        r.routing_weights_initialized_ = true;
        return static_cast<bool>(is);
    }
    if (magic != kRouterFullRUF2) {
        return false;
    }

    uint32_t schema = 0;
    if (!r_u32(is, schema) || schema != kRuf2SchemaVersion) {
        return false;
    }

    uint64_t ne = 0;
    uint64_t rq = 0;
    if (!r_u64(is, ne) || !r_u64(is, rq)) {
        return false;
    }
    if (ne != static_cast<uint64_t>(r.config_.total_experts) ||
        rq != static_cast<uint64_t>(r.config_.routing_qutrits)) {
        return false;
    }

    uint64_t cae = 0;
    if (!r_u64(is, cae)) {
        return false;
    }
    r.current_active_experts_ = static_cast<size_t>(cae);

    if (!r_u32(is, r.last_load_measurement_)) {
        return false;
    }

    uint8_t b = 0;
    if (!r_u8(is, b)) {
        return false;
    }
    r.routing_weights_initialized_ = (b != 0);
    if (!r_u8(is, b)) {
        return false;
    }
    r.entanglement_initialized_ = (b != 0);
    if (!r_u8(is, b)) {
        return false;
    }
    r.advanced_selection_initialized_ = (b != 0);

    if (!r_u32(is, r.ternary_seed_) || !r_u32(is, r.selection_episode_)) {
        return false;
    }

    int8_t i8 = 0;
    if (!r_i8(is, i8) || !trit_ok(i8)) {
        return false;
    }
    r.tropical_initialized_ = static_cast<ternary::Trit>(i8);
    if (!r_i8(is, i8) || !trit_ok(i8)) {
        return false;
    }
    r.energy_per_expert_ = static_cast<ternary::EnergyTrit>(i8);
    if (!r_i8(is, i8) || !trit_ok(i8)) {
        return false;
    }
    r.load_balancing_accuracy_ = static_cast<ternary::ProbTrit>(i8);

    if (!r_u32(is, r.scaling_decision_count_) || !r_u32(is, r.last_scaling_time_) || !r_u32(is, r.thermal_current_) ||
        !r_u32(is, r.priority_preemptions_) || !r_u32(is, r.load_balancing_episodes_)) {
        return false;
    }

    if (!r_u32(is, r.entangled_config_.entanglement_strength) ||
        !r_u32(is, r.entangled_config_.coherence_threshold) ||
        !r_u32(is, r.entangled_config_.efficiency_target)) {
        return false;
    }
    uint64_t shots = 0;
    if (!r_u64(is, shots)) {
        return false;
    }
    r.entangled_config_.measurement_shots = static_cast<size_t>(shots);
    if (!r_i8(is, i8) || !trit_ok(i8)) {
        return false;
    }
    r.entangled_config_.use_adaptive_entanglement = static_cast<ternary::Trit>(i8);

    size_t rng_len = 0;
    if (!r_sz(is, rng_len) || rng_len > 65536) {
        return false;
    }
    std::string rng_blob(rng_len, '\0');
    if (rng_len > 0) {
        is.read(rng_blob.data(), static_cast<std::streamsize>(rng_len));
        if (!is) {
            return false;
        }
    }
    {
        std::istringstream iss(rng_blob);
        iss >> r.rng_;
        if (rng_len > 0 && iss.fail()) {
            return false;
        }
    }

    r.routing_weights_.resize(r.config_.total_experts);
    for (size_t e = 0; e < r.config_.total_experts; ++e) {
        r.routing_weights_[e].resize(r.config_.routing_qutrits);
        for (size_t j = 0; j < r.config_.routing_qutrits; ++j) {
            if (!r_i8(is, i8) || !trit_ok(i8)) {
                return false;
            }
            r.routing_weights_[e][j] = static_cast<ternary::Trit>(i8);
        }
    }

    r.expert_weights_.resize(r.config_.total_experts);
    for (size_t e = 0; e < r.config_.total_experts; ++e) {
        if (!r_mat_trit(is, r.expert_weights_[e], 1ull << 16, 1ull << 16)) {
            return false;
        }
    }

    constexpr size_t kMaxExperts = 65536;
    constexpr size_t kMaxCoupling = 8192;
    if (!r_mat_i32(is, r.entanglement_coupling_, kMaxCoupling, kMaxCoupling)) {
        return false;
    }
    if (!r_mat_i32(is, r.state_action_scores_, 4096, kMaxExperts)) {
        return false;
    }

    size_t lss = 0;
    if (!r_sz(is, lss) || lss > 4096) {
        return false;
    }
    r.last_system_state_.resize(lss);
    for (size_t i = 0; i < lss; ++i) {
        if (!r_i32(is, r.last_system_state_[i])) {
            return false;
        }
    }

    if (!r_vec_u32(is, r.load_history_) || r.load_history_.size() > 1000000) {
        return false;
    }
    if (!r_vec_u32(is, r.latency_history_) || r.latency_history_.size() > 1000000) {
        return false;
    }

    size_t eh = 0;
    if (!r_sz(is, eh) || eh > 1000000) {
        return false;
    }
    r.energy_history_.resize(eh);
    for (size_t i = 0; i < eh; ++i) {
        if (!r_i8(is, i8) || !trit_ok(i8)) {
            return false;
        }
        r.energy_history_[i] = static_cast<ternary::EnergyTrit>(i8);
    }

    if (!r_mat_u32(is, r.priority_performance_history_, 256, 1ull << 20)) {
        return false;
    }
    if (!r_vec_u32(is, r.priority_queue_sizes_) || r.priority_queue_sizes_.size() > 4096) {
        return false;
    }

    size_t pfm = 0;
    if (!r_sz(is, pfm) || pfm > 4096) {
        return false;
    }
    r.priority_fairness_metrics_.resize(pfm);
    for (size_t i = 0; i < pfm; ++i) {
        if (!r_i8(is, i8) || !trit_ok(i8)) {
            return false;
        }
        r.priority_fairness_metrics_[i] = static_cast<ternary::ProbTrit>(i8);
    }

    if (!r_vec_vec_sz(is, r.priority_reserved_experts_, 256, kMaxExperts)) {
        return false;
    }
    if (!r_mat_i32(is, r.expert_performance_history_, kMaxExperts, 65536)) {
        return false;
    }
    if (!r_mat_u32(is, r.load_prediction_history_, kMaxExperts, 65536)) {
        return false;
    }

    size_t bn = 0;
    if (!r_sz(is, bn) || bn > kMaxExperts) {
        return false;
    }
    r.bottleneck_flags_.resize(bn);
    for (size_t i = 0; i < bn; ++i) {
        if (!r_i8(is, i8) || !trit_ok(i8)) {
            return false;
        }
        r.bottleneck_flags_[i] = static_cast<ternary::Trit>(i8);
    }

    size_t mlr = 0;
    if (!r_sz(is, mlr) || mlr > kMaxExperts) {
        return false;
    }
    r.ml_balancing_weights_.resize(mlr);
    for (size_t ri = 0; ri < mlr; ++ri) {
        size_t mlc = 0;
        if (!r_sz(is, mlc) || mlc > 4096) {
            return false;
        }
        r.ml_balancing_weights_[ri].resize(mlc);
        for (size_t cj = 0; cj < mlc; ++cj) {
            if (!r_i8(is, i8) || !trit_ok(i8)) {
                return false;
            }
            r.ml_balancing_weights_[ri][cj] = static_cast<ternary::ProbTrit>(i8);
        }
    }

    if (!r_vec_vec_sz(is, r.expert_groups_, 256, kMaxExperts)) {
        return false;
    }

    if (!r_vec_sz(is, r.expert_loads_, r.config_.total_experts)) {
        return false;
    }
    if (!r_vec_sz(is, r.expert_request_counts_, r.config_.total_experts)) {
        return false;
    }

    return static_cast<bool>(is);
}

void MoERouter::SerializeRouterState(std::ostream& os) {
    moe_router_checkpoint_write(*this, os);
}

bool MoERouter::DeserializeRouterState(std::istream& is, uint32_t checkpoint_fmt) {
    return moe_router_checkpoint_read(*this, is, checkpoint_fmt);
}

} // namespace q_mini_wasm_v2::core::moe
