#include <cmath>
#include <iostream>
#include <memory>

#include "qminiwasm/training/libtorch_ternary_trainer.hpp"

int main() {
  using qminiwasm::training::LibTorchTpemTrainer;
  std::string err;
  auto trainer = LibTorchTpemTrainer::create(1e-3, 42, &err);
  if (!trainer) {
    std::cerr << "joint_cascade_trainer_test: create failed\n";
    return 2;
  }
  if (!trainer->init_geometry(64, 64, 1, 0, 0, &err)) {
    std::cerr << "joint_cascade_trainer_test: init_geometry: " << err << "\n";
    return 3;
  }
  const double grpo =
      trainer->train_step_joint_supervised_cascade(4, 2, 0.1, false, 0.2, 1ULL);
  const double cispo =
      trainer->train_step_joint_supervised_cascade(4, 2, 0.1, true, 0.2, 2ULL);
  if (!std::isfinite(grpo) || !std::isfinite(cispo)) {
    std::cerr << "joint_cascade_trainer_test: non-finite loss\n";
    return 4;
  }
  return 0;
}
