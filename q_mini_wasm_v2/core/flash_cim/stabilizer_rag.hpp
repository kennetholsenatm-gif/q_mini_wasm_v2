#pragma once

#include <cstdint>
#include <span>
#include "../memory/arena.hpp"
#include "../stabilizer/tableau.hpp"
#include "../ternary/trit.hpp"
#include "../qgnn/message_passing.hpp"

/**
 * @brief Stabilizer State RAG Retrieval Engine
 * 
 * Phase 2 Consolidation §62-66: Retrieval-Augmented Generation via Stabilizer Tableaus
 * 
 * Replaces standard floating-point vector databases with discrete stabilizer state
 * matching algorithm running natively on Flash Compute-in-Memory hardware.
 * 
 * All retrieval operations are performed using GF(3) symplectic inner products
 * without floating point operations, cosine similarity, or external vector databases.
 */

namespace q_mini_wasm_v2::core::flash_cim {

class StabilizerRag {
public:
    /**
     * @brief Construct RAG engine attached to shared memory arena
     */
    explicit StabilizerRag(memory::MemoryArena& arena) noexcept;

    /**
     * @brief Add document to knowledge base
     * @param document_text Raw document text
     * @return Index position of stored document
     */
    size_t add_document(std::string_view document_text) noexcept;

    /**
     * @brief Add pre-encoded stabilizer tableau to knowledge base
     * @param tableau Stabilizer state representation of document
     * @return Index position of stored document
     */
    size_t add_stabilizer_document(std::span<const stabilizer::StabilizerTableau> tableau) noexcept;

    /**
     * @brief Retrieve most semantically aligned documents
     * @param query Query stabilizer state
     * @param k Number of results to return
     * @return Indices of top k matching documents
     */
    std::span<const size_t> retrieve(
        std::span<const ternary::Trit> query_state,
        size_t k = 4
    ) noexcept;

    /**
     * @brief Symplectic alignment score between two stabilizer states
     * @return Alignment score {-1, 0, +1} where +1 indicates perfect alignment
     */
    static constexpr int8_t alignment_score(
        std::span<const ternary::Trit> a,
        std::span<const ternary::Trit> b
    ) noexcept {
        return qgnn::MessagePassingKernel::symplectic_attention(a, b);
    }

    /**
     * @brief Batch retrieve for multiple queries
     */
    void batch_retrieve(
        std::span<const std::span<const ternary::Trit>> queries,
        std::span<std::span<size_t>> results
    ) noexcept;

    /**
     * @brief Get direct access to the stabilizer knowledge base
     */
    std::span<const stabilizer::StabilizerTableau> get_knowledge_base() const noexcept;

    /**
     * @brief Total number of documents stored
     */
    size_t document_count() const noexcept;

private:
    memory::MemoryArena& arena_;
    std::span<stabilizer::StabilizerTableau> knowledge_base_;
    size_t next_index_ = 0;

    void encode_document(std::string_view text, stabilizer::StabilizerTableau& out) noexcept;
};

} // namespace q_mini_wasm_v2::core::flash_cim