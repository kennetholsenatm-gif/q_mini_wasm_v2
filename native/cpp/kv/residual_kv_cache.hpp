#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace qminiwasm::kv {

/**
 * Per-layer reference head (fp16 stored as uint16) and XOR-compressed sparse deltas
 * for subsequent token KV slices (toy layout for round-trip tests).
 */
class ResidualKvCache {
 public:
  explicit ResidualKvCache(std::size_t head_dim);

  void set_reference(const std::int16_t* ref_fp16, std::size_t len);

  /** Encode delta = slice - ref into (idx, xor payload) blocks; returns bytes written. */
  std::size_t push_delta_xor(const std::int16_t* slice_fp16, std::size_t len, std::uint8_t block_bits = 4);

  /** Decode last pushed slice back to fp16 (round-trip vs naive store). */
  void decode_last(std::vector<std::int16_t>& out_fp16) const;

  std::size_t num_stored() const { return stored_.size(); }

 private:
  std::size_t head_dim_ = 0;
  std::vector<std::int16_t> ref_{};
  struct Entry {
    std::uint16_t block_index = 0;
    std::uint8_t xor_payload = 0;
  };
  std::vector<Entry> stored_{};
  std::vector<std::int16_t> last_decoded_{};
};

}  // namespace qminiwasm::kv
