/**
 * Contract: MoERouter::route_topk(dense trits) must agree with
 * route_topk_from_tritpack5(pack_batch_t5(same trits), trit_count)
 * for the int32 directory-line mapping used in extract_ternary_route_input.
 */
#include "../core/moe/router.hpp"
#include "../core/ternary/packing.hpp"
#include "../core/ternary/trit.hpp"
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

void i32_directory_payload_to_trits(const std::vector<int32_t>& values, size_t moe_dim,
                                    std::vector<q_mini_wasm_v2::core::ternary::Trit>& out) {
    using q_mini_wasm_v2::core::ternary::Trit;
    out.clear();
    for (int32_t v : values) {
        if (out.size() >= moe_dim) {
            break;
        }
        int im = static_cast<int>(v % 3);
        if (im < 0) {
            im += 3;
        }
        const int8_t mapped = static_cast<int8_t>(im - 1);
        out.push_back(static_cast<Trit>(mapped));
    }
    if (out.size() < moe_dim) {
        out.resize(moe_dim, Trit::ZERO);
    } else if (out.size() > moe_dim) {
        out.resize(moe_dim);
    }
}

bool routes_match(const std::vector<size_t>& a, const std::vector<size_t>& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i] != b[i]) {
            return false;
        }
    }
    return true;
}

} // namespace

int main() {
#if !defined(USE_SYCL) || !USE_SYCL
    std::cout << "test_moe_route_dense_vs_tritpack5: skipped (requires USE_SYCL=1; symplectic routing has no CPU logits)\n";
    return 0;
#else
    using namespace q_mini_wasm_v2::core::moe;
    using namespace q_mini_wasm_v2::core::ternary;

    constexpr size_t E = 32;
    constexpr size_t R = 12;
    constexpr size_t k = 2;
    ExpertConfig cfg{E, k, R};
    cfg.sycl_route_mode = SyclRouteMode::On; // GPU-mandatory symplectic routing when E>=8
    MoERouter router(cfg);

    // ASCII-like int32 stream (same shape as directory corpus lines)
    const std::vector<int32_t> raw = {72, 101, 108, 108, 111, 32, 119, 111, 114, 108, 100, 33};

    std::vector<Trit> trits;
    i32_directory_payload_to_trits(raw, R, trits);

    std::vector<int8_t> lanes(R);
    for (size_t i = 0; i < R; ++i) {
        lanes[i] = static_cast<int8_t>(trits[i]);
    }
    std::vector<uint8_t> packed;
    q::ternary::pack_batch_t5(lanes, packed);

    const size_t need = (trits.size() + 4) / 5;
    if (packed.size() < need) {
        std::cerr << "test_moe_route_dense_vs_tritpack5: pack too short need=" << need
                  << " got=" << packed.size() << '\n';
        return 1;
    }

    const auto dense = router.route_topk(trits);
    const auto t5 = router.route_topk_from_tritpack5(packed, trits.size());

    if (!routes_match(dense, t5)) {
        std::cerr << "route_topk vs route_topk_from_tritpack5 mismatch dense.size=" << dense.size()
                  << " t5.size=" << t5.size() << '\n';
        return 1;
    }

    // Second payload to catch off-by-one / padding issues
    const std::vector<int32_t> raw2 = {9, 8, 7, 6, 5, 4, 3, 2, 1, 0, -1, -2};
    std::vector<Trit> trits2;
    i32_directory_payload_to_trits(raw2, R, trits2);
    std::vector<int8_t> lanes2(R);
    for (size_t i = 0; i < R; ++i) {
        lanes2[i] = static_cast<int8_t>(trits2[i]);
    }
    std::vector<uint8_t> packed2;
    q::ternary::pack_batch_t5(lanes2, packed2);
    if (!routes_match(router.route_topk(trits2), router.route_topk_from_tritpack5(packed2, trits2.size()))) {
        std::cerr << "route parity failed on second corpus vector\n";
        return 1;
    }

    std::cout << "test_moe_route_dense_vs_tritpack5: passed\n";
    return 0;
#endif
}
