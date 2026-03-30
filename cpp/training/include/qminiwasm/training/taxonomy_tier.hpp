#pragma once

#include <cstddef>
#include <string_view>

namespace qminiwasm::training {

/** Training runtime taxonomy (batch slots, prefetch), not EF pages and not inference expert-fleet layout. */
enum class TaxonomyTier {
  kEdgeConstrained = 0,
  kFogNode = 1,
  kXpuCluster = 2,
};

enum class PrecisionMode {
  kInt4 = 0,
  kTernary = 1,
  kFp16 = 2,
};

struct TaxonomyPolicy {
  TaxonomyTier tier = TaxonomyTier::kEdgeConstrained;
  PrecisionMode precision = PrecisionMode::kTernary;
  std::size_t preallocated_samples = 0;
  std::size_t max_parallel_workers = 1;
  std::size_t prefetch_depth = 2;
  std::size_t compute_slots = 1;
};

TaxonomyTier parse_taxonomy_tier(std::string_view value);
const char* to_string(TaxonomyTier tier);
const char* to_string(PrecisionMode mode);

TaxonomyPolicy make_taxonomy_policy(TaxonomyTier tier, std::size_t host_threads_hint);

}  // namespace qminiwasm::training
