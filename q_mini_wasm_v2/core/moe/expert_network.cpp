#include "expert_network.hpp"
#include <random>
#include <algorithm>

namespace q_mini_wasm_v2::core::moe {

// ============================================================================
// ExpertNetwork Base Implementation
// ============================================================================

uint32_t ExpertNetwork::ComputeGoodness(
    const std::vector<ternary::Trit>& activations
) const {
    uint32_t goodness = 0;

    // Count non-zero trits in this vector (for {-1,0,+1}, same as sum of |trit|).
    for (auto val : activations) {
        int8_t v = static_cast<int8_t>(val);
        goodness += (v == 0) ? 0 : 1;  // |v|² for ternary
    }
    
    return goodness;
}

std::vector<ternary::Trit> ExpertNetwork::GenerateNegativeSample(
    const std::vector<ternary::Trit>& positive
) const {
    static std::mt19937 rng(std::random_device{}());
    
    std::vector<ternary::Trit> negative = positive;
    
    // Corrupt ~10% of values
    std::uniform_int_distribution<size_t> pos_dist(0, positive.size() - 1);
    std::uniform_int_distribution<int> val_dist(-1, 1);
    
    size_t num_corruptions = std::max(size_t(1), positive.size() / 10);
    
    for (size_t i = 0; i < num_corruptions; ++i) {
        size_t pos = pos_dist(rng);
        ternary::Trit new_val = static_cast<ternary::Trit>(val_dist(rng));
        
        // Only corrupt if value actually changes
        if (new_val != negative[pos]) {
            negative[pos] = new_val;
        }
    }
    
    return negative;
}

// ============================================================================
// ExpertRegistry Implementation
// ============================================================================

ExpertRegistry& ExpertRegistry::Instance() {
    static ExpertRegistry instance;
    return instance;
}

void ExpertRegistry::Register(const std::string& name, ExpertFactory factory) {
    factories_[name] = factory;
}

std::unique_ptr<ExpertNetwork> ExpertRegistry::Create(
    const std::string& name,
    const ExpertNetwork::ExpertConfig& config
) {
    auto it = factories_.find(name);
    if (it != factories_.end()) {
        return it->second(config);
    }
    return nullptr;
}

std::vector<std::string> ExpertRegistry::GetRegisteredTypes() const {
    std::vector<std::string> types;
    for (const auto& pair : factories_) {
        types.push_back(pair.first);
    }
    return types;
}

} // namespace q_mini_wasm_v2::core::moe
