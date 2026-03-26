#include "qminiwasm/training/taxonomy_tier.hpp"

#include <algorithm>
#include <cctype>
#include <string>

namespace qminiwasm::training {

TaxonomyTier parse_taxonomy_tier(std::string_view value) {
  std::string lower(value);
  std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  if (lower == "fog" || lower == "fog_node" || lower == "meso") {
    return TaxonomyTier::kFogNode;
  }
  if (lower == "xpu" || lower == "xpu_cluster" || lower == "cloud" || lower == "macro") {
    return TaxonomyTier::kXpuCluster;
  }
  return TaxonomyTier::kEdgeConstrained;
}

const char* to_string(TaxonomyTier tier) {
  switch (tier) {
    case TaxonomyTier::kEdgeConstrained:
      return "edge_constrained";
    case TaxonomyTier::kFogNode:
      return "fog_node";
    case TaxonomyTier::kXpuCluster:
      return "xpu_cluster";
  }
  return "edge_constrained";
}

const char* to_string(PrecisionMode mode) {
  switch (mode) {
    case PrecisionMode::kInt4:
      return "int4";
    case PrecisionMode::kTernary:
      return "ternary";
    case PrecisionMode::kFp16:
      return "fp16";
  }
  return "ternary";
}

TaxonomyPolicy make_taxonomy_policy(TaxonomyTier tier, std::size_t host_threads_hint) {
  const std::size_t threads = std::max<std::size_t>(1, host_threads_hint);
  switch (tier) {
    case TaxonomyTier::kEdgeConstrained:
      return TaxonomyPolicy{
          .tier = tier,
          .precision = PrecisionMode::kInt4,
          .preallocated_samples = 1024,
          .max_parallel_workers = std::min<std::size_t>(threads, 2),
          .prefetch_depth = 2,
          .compute_slots = 1,
      };
    case TaxonomyTier::kFogNode:
      return TaxonomyPolicy{
          .tier = tier,
          .precision = PrecisionMode::kTernary,
          .preallocated_samples = 8192,
          .max_parallel_workers = std::min<std::size_t>(threads, 6),
          .prefetch_depth = 4,
          .compute_slots = 2,
      };
    case TaxonomyTier::kXpuCluster:
      return TaxonomyPolicy{
          .tier = tier,
          .precision = PrecisionMode::kFp16,
          .preallocated_samples = 65536,
          .max_parallel_workers = std::max<std::size_t>(4, threads),
          .prefetch_depth = 8,
          .compute_slots = 4,
      };
  }
  return TaxonomyPolicy{};
}

}  // namespace qminiwasm::training
