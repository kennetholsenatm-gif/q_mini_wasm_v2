#include "entanglement_token.hpp"
#include <random>
#include <algorithm>
#include <cmath>

namespace q_mini_wasm_v2::core::inference {

EntanglementTokenManager::EntanglementTokenManager(const TokenConfig& config)
    : config_(config) {
    initialize_embeddings();
    shared_tableau_ = stabilizer::create_tableau(config.embedding_dim);
}

EntanglementTokenManager::~EntanglementTokenManager() = default;

EntanglementTokenManager::EntangledSequence EntanglementTokenManager::tokenize(const std::vector<size_t>& input_ids) {
    EntangledSequence seq;
    seq.sequence_length = input_ids.size();
    seq.tableau = shared_tableau_;
    for (size_t id : input_ids) {
        EntanglementToken token;
        token.token_id = id;
        token.coherence = 1.0;
        token.stabilizer_state = trits_to_stabilizer(get_embedding(id));
        token.phase_vector.resize(token.stabilizer_state.size(), 0);
        seq.tokens.push_back(token);
    }
    return seq;
}

std::pair<EntanglementTokenManager::EntanglementToken, EntanglementTokenManager::EntanglementToken> 
EntanglementTokenManager::entangle_tokens(
    const EntanglementToken& control,
    const EntanglementToken& target
) {
    return {control, target}; // Stub
}

EntanglementTokenManager::EntangledSequence EntanglementTokenManager::create_sequence_entanglement(
    const std::vector<EntanglementToken>& tokens
) {
    EntangledSequence seq;
    seq.tokens = tokens;
    seq.sequence_length = tokens.size();
    seq.tableau = shared_tableau_;
    return seq;
}

EntanglementTokenManager::EntangledSequence EntanglementTokenManager::internalize_context(
    const EntangledSequence& sequence,
    const EntangledSequence& context_tokens
) {
    return sequence; // Stub
}

void EntanglementTokenManager::initialize_embeddings() {
    std::mt19937 gen(42);
    std::uniform_int_distribution<int> dist(0, 2);
    
    embeddings_.reserve(config_.vocab_size);
    for (size_t i = 0; i < config_.vocab_size; ++i) {
        std::vector<ternary::Trit> embedding(config_.embedding_dim);
        for (size_t j = 0; j < config_.embedding_dim; ++j) {
            embedding[j] = static_cast<ternary::Trit>(dist(gen) - 1);
        }
        embeddings_.push_back(embedding);
    }
}

std::vector<ternary::Trit> EntanglementTokenManager::get_embedding(size_t token_id) const {
    if (token_id >= embeddings_.size()) {
        return std::vector<ternary::Trit>(config_.embedding_dim, ternary::Trit::ZERO);
    }
    return embeddings_[token_id];
}

std::vector<int8_t> EntanglementTokenManager::trits_to_stabilizer(
    const std::vector<ternary::Trit>& trits
) {
    std::vector<int8_t> stabilizer(trits.size());
    for (size_t i = 0; i < trits.size(); ++i) {
        stabilizer[i] = ternary::to_gf3(trits[i]);
    }
    return stabilizer;
}

EntanglementTokenManager::EntanglementToken EntanglementTokenManager::apply_superposition(
    const EntanglementToken& token
) {
    EntanglementToken result = token;
    for (size_t i = 0; i < config_.embedding_dim && i < result.stabilizer_state.size(); ++i) {
        shared_tableau_->apply_hadamard(i);
        result.stabilizer_state[i] = (result.stabilizer_state[i] + 1) % 3;
    }
    result.coherence = 0.7;
    return result;
}

double EntanglementTokenManager::compute_pairwise_entanglement(
    const EntanglementToken& t1,
    const EntanglementToken& t2
) const {
    if (t1.stabilizer_state.size() != t2.stabilizer_state.size()) {
        return 0.0;
    }
    
    double correlation = 0.0;
    for (size_t i = 0; i < t1.stabilizer_state.size(); ++i) {
        int diff = (t1.stabilizer_state[i] - t2.stabilizer_state[i] + 3) % 3;
        correlation += (diff == 0) ? 1.0 : 0.5;
    }
    return correlation / t1.stabilizer_state.size();
}

double EntanglementTokenManager::compute_entanglement_entropy(
    const EntangledSequence& sequence
) const {
    if (sequence.tokens.size() < 2) return 0.0;
    
    double total_entropy = 0.0;
    for (size_t i = 0; i + 1 < sequence.tokens.size(); ++i) {
        double p_entangled = compute_pairwise_entanglement(
            sequence.tokens[i],
            sequence.tokens[i + 1]
        );
        
        if (p_entangled > 0 && p_entangled < 1) {
            total_entropy -= p_entangled * std::log2(p_entangled);
            total_entropy -= (1.0 - p_entangled) * std::log2(1.0 - p_entangled);
        }
    }
    return total_entropy / (sequence.tokens.size() - 1);
}

size_t EntanglementTokenManager::measure_token(const EntanglementToken& token) {
    size_t measured = 0;
    for (size_t i = 0; i < token.stabilizer_state.size(); ++i) {
        measured = (measured * 3 + token.stabilizer_state[i]) % config_.vocab_size;
    }
    return measured;
}

std::unique_ptr<EntanglementTokenManager> create_token_manager(
    const EntanglementTokenManager::TokenConfig& config
) {
    return std::make_unique<EntanglementTokenManager>(config);
}

} // namespace q_mini_wasm_v2::core::inference