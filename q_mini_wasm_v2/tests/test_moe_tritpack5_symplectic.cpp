/**
 * Verifies MoE symplectic routing scores: unpacked int8 reference ==
 * TritPack5-packed decode (polynomial 5-trits/byte; router.cpp / SYCL kernel).
 * Optionally compares to moe_routing_symplectic_scores_sycl when USE_SYCL.
 */
#include "../core/ternary/packing.hpp"
#include <cstdint>
#include <iostream>
#include <random>
#include <vector>

#if defined(USE_SYCL) && USE_SYCL
#include "../sycl/tableau_kernels.hpp"
#endif

namespace {

inline int8_t gf3_add(int8_t a, int8_t b) {
    int sum = static_cast<int>(a) + static_cast<int>(b);
    if (sum > 1) {
        return -1;
    }
    if (sum < -1) {
        return 1;
    }
    return static_cast<int8_t>(sum);
}

std::vector<int8_t> symplectic_scores_unpacked(
    const std::vector<int8_t>& input,
    const std::vector<std::vector<int8_t>>& weights
) {
    const size_t E = weights.size();
    std::vector<int8_t> scores(E, static_cast<int8_t>(-1));
    if (input.empty() || E == 0) {
        return scores;
    }
    for (size_t e = 0; e < E; ++e) {
        const size_t min_size = std::min(input.size(), weights[e].size());
        int8_t symplectic_sum = 0;
        for (size_t i = 0; i < min_size; i += 2) {
            const int8_t x1 = input[i];
            const int8_t z1 = (i + 1 < min_size) ? input[i + 1] : static_cast<int8_t>(0);
            const int8_t x2 = weights[e][i];
            const int8_t z2 =
                (i + 1 < min_size) ? weights[e][i + 1] : static_cast<int8_t>(0);
            int pairing = static_cast<int>(x1) * static_cast<int>(z2) - static_cast<int>(z1) * static_cast<int>(x2);
            while (pairing > 1) {
                pairing -= 3;
            }
            while (pairing < -1) {
                pairing += 3;
            }
            symplectic_sum = gf3_add(symplectic_sum, static_cast<int8_t>(pairing));
        }
        scores[e] = symplectic_sum;
    }
    return scores;
}

std::vector<uint8_t> pack_weights_rowmajor_t5(
    const std::vector<std::vector<int8_t>>& weights
) {
    const size_t E = weights.size();
    if (E == 0) {
        return {};
    }
    const size_t R = weights[0].size();
    const size_t row_bytes = (R + 4) / 5;
    std::vector<uint8_t> wt(E * row_bytes, static_cast<uint8_t>(0));
    for (size_t e = 0; e < E; ++e) {
        std::vector<uint8_t> packed_row;
        q::ternary::pack_batch_t5(weights[e], packed_row);
        for (size_t b = 0; b < row_bytes && b < packed_row.size(); ++b) {
            wt[e * row_bytes + b] = packed_row[b];
        }
    }
    return wt;
}

std::vector<int8_t> symplectic_scores_from_packed(
    const std::vector<uint8_t>& input_packed,
    size_t input_trit_count,
    const std::vector<uint8_t>& weights_packed,
    size_t total_experts,
    size_t routing_qutrits
) {
    std::vector<int8_t> scores(total_experts, static_cast<int8_t>(-1));
    if (input_packed.empty() || input_trit_count == 0 || total_experts == 0) {
        return scores;
    }
    const size_t need_in = (input_trit_count + 4) / 5;
    if (input_packed.size() < need_in) {
        return scores;
    }
    const size_t w_row_bytes = (routing_qutrits + 4) / 5;
    if (weights_packed.size() < total_experts * w_row_bytes) {
        return scores;
    }
    const uint8_t* pin = input_packed.data();
    const uint8_t* pw = weights_packed.data();

    for (size_t e = 0; e < total_experts; ++e) {
        const size_t min_size = input_trit_count < routing_qutrits ? input_trit_count : routing_qutrits;
        int8_t symplectic_sum = 0;
        for (size_t i = 0; i < min_size; i += 2) {
            const int8_t x1 = q::ternary::read_trit_t5_at(pin, i);
            const int8_t z1 =
                (i + 1 < min_size) ? q::ternary::read_trit_t5_at(pin, i + 1) : static_cast<int8_t>(0);
            const int8_t x2 = q::ternary::read_trit_t5_at(pw + e * w_row_bytes, i);
            const int8_t z2 = (i + 1 < min_size) ? q::ternary::read_trit_t5_at(pw + e * w_row_bytes, i + 1)
                                                : static_cast<int8_t>(0);
            int pairing = static_cast<int>(x1) * static_cast<int>(z2) - static_cast<int>(z1) * static_cast<int>(x2);
            while (pairing > 1) {
                pairing -= 3;
            }
            while (pairing < -1) {
                pairing += 3;
            }
            symplectic_sum = gf3_add(symplectic_sum, static_cast<int8_t>(pairing));
        }
        scores[e] = symplectic_sum;
    }
    return scores;
}

bool run_case(size_t E, size_t R, std::mt19937& rng) {
    std::uniform_int_distribution<int> dist(-1, 1);
    std::vector<int8_t> input(R);
    std::vector<std::vector<int8_t>> weights(E, std::vector<int8_t>(R));
    for (size_t j = 0; j < R; ++j) {
        input[j] = static_cast<int8_t>(dist(rng));
    }
    for (size_t e = 0; e < E; ++e) {
        for (size_t j = 0; j < R; ++j) {
            weights[e][j] = static_cast<int8_t>(dist(rng));
        }
    }

    const auto ref = symplectic_scores_unpacked(input, weights);
    std::vector<uint8_t> in_packed;
    q::ternary::pack_batch_t5(input, in_packed);
    const std::vector<uint8_t> w_packed = pack_weights_rowmajor_t5(weights);
    const auto packed = symplectic_scores_from_packed(in_packed, R, w_packed, E, R);

    for (size_t e = 0; e < E; ++e) {
        if (ref[e] != packed[e]) {
            std::cerr << "Mismatch E=" << E << " R=" << R << " expert " << e << " ref=" << static_cast<int>(ref[e])
                      << " packed=" << static_cast<int>(packed[e]) << '\n';
            return false;
        }
    }

#if defined(USE_SYCL) && USE_SYCL
    try {
        auto sycl_scores = q_mini_wasm_v2::sycl_kernels::moe_routing_symplectic_scores_sycl(
            in_packed, R, w_packed, E, R);
        if (sycl_scores.size() != E) {
            std::cerr << "SYCL returned size " << sycl_scores.size() << " expected " << E << '\n';
            return false;
        }
        for (size_t e = 0; e < E; ++e) {
            if (ref[e] != sycl_scores[e]) {
                std::cerr << "SYCL mismatch E=" << E << " R=" << R << " expert " << e << " ref=" << static_cast<int>(ref[e])
                          << " sycl=" << static_cast<int>(sycl_scores[e]) << '\n';
                return false;
            }
        }
    } catch (const std::exception& ex) {
        std::cerr << "SYCL path threw: " << ex.what() << '\n';
        return false;
    }
#endif
    return true;
}

} // namespace

int main() {
    std::mt19937 rng(12345);
    const struct {
        size_t E;
        size_t R;
    } shapes[] = {
        {1, 1},
        {2, 5},
        {3, 7},
        {4, 12},
        {8, 16},
    };
    for (const auto& s : shapes) {
        for (int rep = 0; rep < 50; ++rep) {
            if (!run_case(s.E, s.R, rng)) {
                return 1;
            }
        }
    }
    std::cout << "test_moe_tritpack5_symplectic: all cases passed";
#if defined(USE_SYCL) && USE_SYCL
    std::cout << " (including SYCL)";
#endif
    std::cout << '\n';
    return 0;
}
