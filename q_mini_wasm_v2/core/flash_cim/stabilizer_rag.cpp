#include "stabilizer_rag.hpp"
#include <algorithm>
#include <numeric>
#include "../inference/ternary_tokenizer.hpp"

namespace q_mini_wasm_v2::core::flash_cim {

StabilizerRag::StabilizerRag(memory::MemoryArena& arena) noexcept
    : arena_(arena)
{
    // Allocate knowledge base storage in shared arena
    knowledge_base_ = arena_.allocate_array<stabilizer::StabilizerTableau>(65536);
}

size_t StabilizerRag::add_document(std::string_view document_text) noexcept
{
    if (next_index_ >= knowledge_base_.size()) {
        return SIZE_MAX;
    }

    encode_document(document_text, knowledge_base_[next_index_]);
    return next_index_++;
}

size_t StabilizerRag::add_stabilizer_document(std::span<const stabilizer::StabilizerTableau> tableau) noexcept
{
    if (next_index_ >= knowledge_base_.size() || tableau.empty()) {
        return SIZE_MAX;
    }

    knowledge_base_[next_index_] = tableau[0];
    return next_index_++;
}

std::span<const size_t> StabilizerRag::retrieve(std::span<const ternary::Trit> query_state, size_t k) noexcept
{
    struct ScoredDocument {
        int8_t score;
        size_t index;
    };

    auto* scores = arena_.allocate_array<ScoredDocument>(next_index_);
    auto* results = arena_.allocate_array<size_t>(k);

    // Compute symplectic alignment scores for all documents
    for (size_t i = 0; i < next_index_; ++i) {
        scores[i] = {
            alignment_score(query_state, knowledge_base_[i].get_trits()),
            i
        };
    }

    // Partial sort to get top k results
    std::partial_sort(
        scores,
        scores + k,
        scores + next_index_,
        [](const ScoredDocument& a, const ScoredDocument& b) {
            return a.score > b.score;
        }
    );

    // Extract result indices
    for (size_t i = 0; i < k; ++i) {
        results[i] = scores[i].index;
    }

    return std::span<const size_t>(results, k);
}

void StabilizerRag::batch_retrieve(
    std::span<const std::span<const ternary::Trit>> queries,
    std::span<std::span<size_t>> results
) noexcept
{
    for (size_t q = 0; q < queries.size(); ++q) {
        auto res = retrieve(queries[q], results[q].size());
        std::copy(res.begin(), res.end(), results[q].begin());
    }
}

std::span<const stabilizer::StabilizerTableau> StabilizerRag::get_knowledge_base() const noexcept
{
    return knowledge_base_.first(next_index_);
}

size_t StabilizerRag::document_count() const noexcept
{
    return next_index_;
}

void StabilizerRag::encode_document(std::string_view text, stabilizer::StabilizerTableau& out) noexcept
{
    // Use ternary tokenizer to convert text directly to GF(3) stabilizer state
    inference::TernaryTokenizer tokenizer;
    auto tokens = tokenizer.tokenize(text);

    // Encode tokens into stabilizer tableau
    out.encode_tokens(tokens);
}

} // namespace q_mini_wasm_v2::core::flash_cim