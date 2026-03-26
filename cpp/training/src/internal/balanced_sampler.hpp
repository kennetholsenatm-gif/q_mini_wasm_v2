#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace qminiwasm::training::internal {

struct SampleRef {
  std::size_t sample_id = 0;
  std::size_t class_id = 0;
};

class BalancedSampler {
 public:
  BalancedSampler(std::size_t classes, std::size_t samples_per_class, std::uint64_t seed);

  void reseed_for_epoch(std::size_t epoch);
  std::vector<SampleRef> next_micro_batch(std::size_t micro_batch_size);

 private:
  std::size_t classes_;
  std::size_t samples_per_class_;
  std::uint64_t base_seed_;
  std::vector<std::size_t> class_offsets_;
  std::size_t class_cursor_ = 0;
};

}  // namespace qminiwasm::training::internal
