#pragma once

#include <future>
#include <vector>
#include <cstdint>
#include "../ternary/trit.hpp"
#include "../learning/forward_forward.hpp"
#include "expert_network.hpp"
#include "router.hpp"

namespace q_mini_wasm_v2::core::moe {

struct Goodness {
    double positive_goodness;
    double negative_goodness;
    double delta;
};

class RuntimeOrchestrator {
public:
    RuntimeOrchestrator() = default;
    ~RuntimeOrchestrator() = default;

    // Submit Forward-Forward training task
    std::future<Goodness> submit_ff_training(
        ExpertNetwork& expert,
        size_t layer,
        const std::vector<std::vector<ternary::Trit>>& positive,
        const std::vector<std::vector<ternary::Trit>>& negative);

    // Submit MoE routing task
    std::future<std::vector<size_t>> submit_moe_routing(
        MoERouter& router,
        const std::vector<ternary::Trit>& input);

    // Wait for all pending tasks to complete
    void wait_all();

private:
    // Internal implementation
    std::vector<std::future<void>> pending_tasks_;
};

} // namespace q_mini_wasm_v2::core::moe
