#include "qminiwasm/training/libtorch_ternary_trainer.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
  if (argc < 6) {
    std::cerr << "usage: qminiwasm_tpem_roundtrip <in_interchange_v2.pt> <out_interchange_v2.pt> <seed> <steps> <lr>\n";
    return 2;
  }
  const std::string in_path = argv[1];
  const std::string out_path = argv[2];
  const std::uint64_t seed = static_cast<std::uint64_t>(std::stoull(argv[3]));
  const int steps = std::stoi(argv[4]);
  const double lr = std::stod(argv[5]);

  std::string err;
  auto trainer = qminiwasm::training::LibTorchTpemTrainer::create(lr, seed, &err);
  if (!trainer->load_interchange(in_path, false, 0, 0, &err)) {
    std::cerr << "load_interchange: " << err << '\n';
    return 1;
  }
  for (int i = 0; i < steps; ++i) {
    const std::uint64_t mix = seed ^ (static_cast<std::uint64_t>(i) * 0x100000001B3ULL);
    (void)trainer->train_step(4, mix);
  }
  if (!trainer->save_interchange(out_path, "tpem_roundtrip", 1, 0.0, 0.0, lr, &err)) {
    std::cerr << "save_interchange: " << err << '\n';
    return 1;
  }
  return 0;
}
