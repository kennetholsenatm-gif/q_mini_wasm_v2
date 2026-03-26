#include "internal/balanced_sampler.hpp"

#include <algorithm>
#include <numeric>
#include <random>

namespace qminiwasm::training::internal {

BalancedSampler::BalancedSampler(std::size_t classes, std::size_t samples_per_class, std::uint64_t seed)
    : classes_(classes == 0 ? 1 : classes),
      samples_per_class_(samples_per_class == 0 ? 1 : samples_per_class),
      base_seed_(seed),
      class_offsets_(classes_, 0) {}

void BalancedSampler::reseed_for_epoch(std::size_t epoch) {
  std::mt19937_64 rng(base_seed_ ^ static_cast<std::uint64_t>(epoch * 0x9E3779B185EBCA87ULL));
  for (std::size_t i = 0; i < classes_; ++i) {
    class_offsets_[i] = static_cast<std::size_t>(rng() % samples_per_class_);
  }
  class_cursor_ = static_cast<std::size_t>(rng() % classes_);
}

std::vector<SampleRef> BalancedSampler::next_micro_batch(std::size_t micro_batch_size) {
  const std::size_t batch = micro_batch_size == 0 ? 1 : micro_batch_size;
  std::vector<SampleRef> out;
  out.reserve(batch);
  for (std::size_t i = 0; i < batch; ++i) {
    const std::size_t class_id = class_cursor_;
    const std::size_t sample_offset = class_offsets_[class_id];
    out.push_back(SampleRef{
        .sample_id = class_id * samples_per_class_ + sample_offset,
        .class_id = class_id,
    });
    class_offsets_[class_id] = (sample_offset + 1) % samples_per_class_;
    class_cursor_ = (class_cursor_ + 1) % classes_;
  }
  return out;
}

}  // namespace qminiwasm::training::internal
