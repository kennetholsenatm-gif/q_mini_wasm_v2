#include "residual_kv_cache.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace qminiwasm::kv {

ResidualKvCache::ResidualKvCache(std::size_t head_dim) : head_dim_(head_dim) {}

void ResidualKvCache::set_reference(const std::int16_t* ref_fp16, std::size_t len) {
  if (len != head_dim_) {
    throw std::invalid_argument("reference length mismatch");
  }
  ref_.assign(ref_fp16, ref_fp16 + len);
}

std::size_t ResidualKvCache::push_delta_xor(const std::int16_t* slice_fp16, std::size_t len,
                                            std::uint8_t block_bits) {
  if (len != head_dim_ || ref_.size() != head_dim_) {
    throw std::invalid_argument("slice/reference length mismatch");
  }
  if (block_bits == 0 || block_bits > 8) {
    throw std::invalid_argument("block_bits must be 1..8");
  }
  const std::size_t block_len = static_cast<std::size_t>(1u) << block_bits;
  stored_.clear();
  for (std::size_t base = 0; base < head_dim_; base += block_len) {
    const std::size_t end = std::min(base + block_len, head_dim_);
    std::uint8_t mask = 0;
    std::uint8_t bits = 0;
    bool any = false;
    for (std::size_t i = base; i < end; ++i) {
      const auto d = static_cast<std::uint16_t>(slice_fp16[i] ^ ref_[i]);
      if (d != 0) {
        any = true;
      }
      mask = static_cast<std::uint8_t>((mask << 1) | (d != 0 ? 1u : 0u));
      bits = static_cast<std::uint8_t>(bits + 1);
    }
    if (any) {
      stored_.push_back(Entry{static_cast<std::uint16_t>(base / block_len), mask});
    }
  }
  last_decoded_.assign(head_dim_, 0);
  for (std::size_t base = 0; base < head_dim_; base += block_len) {
    const std::size_t end = std::min(base + block_len, head_dim_);
    for (std::size_t i = base; i < end; ++i) {
      last_decoded_[i] = ref_[i];
    }
  }
  for (const Entry& e : stored_) {
    const std::size_t base = static_cast<std::size_t>(e.block_index) * block_len;
    const std::size_t end = std::min(base + block_len, head_dim_);
    std::uint8_t m = e.xor_payload;
    std::size_t k = end - base;
    while (k-- > 0) {
      const std::size_t i = base + k;
      if ((m & 1u) != 0) {
        last_decoded_[i] = slice_fp16[i];
      }
      m = static_cast<std::uint8_t>(m >> 1);
    }
  }
  return stored_.size() * sizeof(Entry);
}

void ResidualKvCache::decode_last(std::vector<std::int16_t>& out_fp16) const {
  out_fp16 = last_decoded_;
}

}  // namespace qminiwasm::kv
