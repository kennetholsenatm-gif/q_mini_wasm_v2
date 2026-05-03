#include "runtime_orchestrator.hpp"
#include "../ternary/packing.hpp"

namespace q_mini_wasm_v2::core::moe {

namespace {

void trits_to_ff_pack5(const std::vector<ternary::Trit>& trits, size_t input_dim, std::vector<uint8_t>& out) {
    std::vector<int8_t> lanes(input_dim, 0);
    for (size_t i = 0; i < trits.size() && i < input_dim; ++i) {
        lanes[i] = static_cast<int8_t>(trits[i]);
    }
    q::ternary::pack_batch_t5(lanes, out);
}

} // namespace

std::future<Goodness> RuntimeOrchestrator::submit_ff_training(
    ExpertNetwork& expert,
    size_t layer,
    const std::vector<std::vector<ternary::Trit>>& positive,
    const std::vector<std::vector<ternary::Trit>>& negative) {
    
    // Create promise/future pair
    std::promise<Goodness> promise;
    std::future<Goodness> future = promise.get_future();
    
    // Execute training synchronously and set result
    try {
        // Flatten the batch of samples - take first sample from each batch
        std::vector<ternary::Trit> pos_sample;
        if (!positive.empty()) {
            pos_sample = positive[0];
        }
        
        std::vector<ternary::Trit> neg_sample;
        if (!negative.empty()) {
            neg_sample = negative[0];
        }

        const size_t in_d = expert.GetConfig().input_dim;
        std::vector<uint8_t> pos_pack;
        std::vector<uint8_t> neg_pack;
        trits_to_ff_pack5(pos_sample, in_d, pos_pack);
        trits_to_ff_pack5(neg_sample, in_d, neg_pack);

        // Train the expert
        expert.TrainForwardForward(pos_pack, neg_pack);

        // Goodness on final-layer outputs (same as pipeline / TrainForwardForward internals)
        const auto route_g = expert.last_forward_forward_route_goodness();
        Goodness goodness;
        goodness.positive_goodness = static_cast<double>(route_g.first);
        goodness.negative_goodness = static_cast<double>(route_g.second);
        goodness.delta = goodness.positive_goodness - goodness.negative_goodness;
        
        promise.set_value(goodness);
    } catch (...) {
        promise.set_exception(std::current_exception());
    }
    
    return future;
}

std::future<std::vector<size_t>> RuntimeOrchestrator::submit_moe_routing(
    MoERouter& router,
    const std::vector<ternary::Trit>& input) {
    
    std::promise<std::vector<size_t>> promise;
    std::future<std::vector<size_t>> future = promise.get_future();
    
    try {
        // Get routing decision directly with ternary input
        auto expert_indices = router.route_topk(input);
        promise.set_value(expert_indices);
    } catch (...) {
        promise.set_exception(std::current_exception());
    }
    
    return future;
}

void RuntimeOrchestrator::wait_all() {
    // Wait for all pending tasks
    for (auto& task : pending_tasks_) {
        if (task.valid()) {
            task.wait();
        }
    }
    pending_tasks_.clear();
}

} // namespace q_mini_wasm_v2::core::moe
