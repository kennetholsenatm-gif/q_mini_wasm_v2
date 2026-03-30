#include "qminiwasm/training/training_engine.hpp"

#include <algorithm>
#include <cctype>
#include <string>

namespace qminiwasm::training {

namespace {

void trim_ascii_inplace(std::string* s) {
  if (s == nullptr) {
    return;
  }
  auto not_space = [](unsigned char c) { return !std::isspace(c); };
  auto& str = *s;
  str.erase(str.begin(), std::find_if(str.begin(), str.end(), not_space));
  str.erase(std::find_if(str.rbegin(), str.rend(), not_space).base(), str.end());
}

void ascii_tolower_inplace(std::string* s) {
  if (s == nullptr) {
    return;
  }
  for (auto& c : *s) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
}

}  // namespace

const TrainingPhaseNative* phase_at_global_epoch(std::size_t global_epoch,
                                                 const std::vector<TrainingPhaseNative>& phases) {
  if (phases.empty()) {
    return nullptr;
  }
  std::size_t cursor = 0;
  for (const auto& ph : phases) {
    const std::size_t n = std::max<std::size_t>(1, ph.epochs);
    if (global_epoch < cursor + n) {
      return &ph;
    }
    cursor += n;
  }
  return &phases.back();
}

std::string effective_cascade_policy(const TrainingPhaseNative* phase, const std::string& root_policy) {
  std::string p = phase != nullptr ? phase->cascade_policy_optimizer : std::string{};
  trim_ascii_inplace(&p);
  ascii_tolower_inplace(&p);
  if (!p.empty()) {
    return p;
  }
  std::string r = root_policy;
  trim_ascii_inplace(&r);
  ascii_tolower_inplace(&r);
  if (r.empty()) {
    return "grpo";
  }
  return r;
}

}  // namespace qminiwasm::training
