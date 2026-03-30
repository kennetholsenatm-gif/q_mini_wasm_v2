#include "qminiwasm/training/training_engine.hpp"

#include <cassert>
#include <cstdlib>
#include <string>
#include <vector>

int main() {
  using qminiwasm::training::TrainingPhaseNative;
  using qminiwasm::training::effective_cascade_policy;
  using qminiwasm::training::phase_at_global_epoch;

  std::vector<TrainingPhaseNative> phases;
  TrainingPhaseNative a{};
  a.name = "sft";
  a.epochs = 2;
  a.cascade_policy_optimizer = "";
  TrainingPhaseNative b{};
  b.name = "mopd";
  b.epochs = 3;
  b.cascade_policy_optimizer = "GRPO";
  phases.push_back(a);
  phases.push_back(b);

  assert(phase_at_global_epoch(0, phases)->name == "sft");
  assert(phase_at_global_epoch(1, phases)->name == "sft");
  assert(phase_at_global_epoch(2, phases)->name == "mopd");
  assert(phase_at_global_epoch(99, phases)->name == "mopd");

  assert(effective_cascade_policy(&phases[0], "cispo") == "cispo");
  assert(effective_cascade_policy(&phases[1], "cispo") == "grpo");

  return EXIT_SUCCESS;
}
