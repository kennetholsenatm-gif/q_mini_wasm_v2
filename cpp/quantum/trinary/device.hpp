#pragma once

#include "../openqasm3/ir.hpp"

#include <array>
#include <cstdint>
#include <complex>
#include <expected>
#include <random>
#include <string>
#include <vector>

#include <torch/torch.h>

namespace qminiwasm::quantum::trinary {

/** Qutrit simulator: qubit gates act on {|0>,|1>} subspace; |2> unchanged for single-qubit block-diagonal gates. */
class TrinarySimDevice {
 public:
  void reset(int num_qutrits, std::uint64_t seed);
  int num_wires() const { return n_; }

  void apply_hadamard_subspace(int wire);
  void apply_pauli_x_subspace(int wire);
  void apply_pauli_y_subspace(int wire);
  void apply_pauli_z_subspace(int wire);
  void apply_rx_subspace(int wire, double theta);
  void apply_ry_subspace(int wire, double theta);
  void apply_rz_subspace(int wire, double theta);
  void apply_cx_subspace(int control, int target);
  void apply_cy_subspace(int control, int target);
  void apply_cz_subspace(int control, int target);

  int measure_wire(int wire);
  double expval_pauli_z(int wire) const;

  const torch::Tensor& state() const { return state_; }

  std::expected<void, std::string> run_openqasm_ir(const openqasm3::CircuitIR& circ, std::uint64_t seed = 1);

 private:
  static int ipow3(int n);
  void apply_single_subspace_unitary(int wire, const std::array<std::array<std::complex<float>, 3>, 3>& u);
  void apply_controlled_subspace(int control, int target, bool cy, bool cz);

  int n_{0};
  torch::Tensor state_{};
  std::mt19937_64 rng_{};
};

}  // namespace qminiwasm::quantum::trinary
