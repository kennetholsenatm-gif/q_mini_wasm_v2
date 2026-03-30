#include "device.hpp"

#include <cmath>
#include <random>
#include <variant>
#include <vector>

namespace qminiwasm::quantum::trinary {

namespace {

int stride_for_wire(int wire) {
  int s = 1;
  for (int i = 0; i < wire; ++i) {
    s *= 3;
  }
  return s;
}

}  // namespace

int TrinarySimDevice::ipow3(int n) {
  int p = 1;
  for (int i = 0; i < n; ++i) {
    p *= 3;
  }
  return p;
}

void TrinarySimDevice::reset(int num_qutrits, std::uint64_t seed) {
  n_ = num_qutrits;
  rng_.seed(seed);
  const int dim = ipow3(n_);
  state_ = torch::zeros({dim}, torch::dtype(torch::kComplexFloat));
  auto* p = state_.data_ptr<c10::complex<float>>();
  p[0] = {1.0F, 0.0F};
}

void TrinarySimDevice::apply_single_subspace_unitary(
    int wire, const std::array<std::array<std::complex<float>, 3>, 3>& u) {
  const int dim = ipow3(n_);
  const int stride = stride_for_wire(wire);
  auto cpu = state_.to(torch::kCPU).contiguous();
  auto* s = cpu.data_ptr<c10::complex<float>>();
  std::vector<std::complex<float>> amp(static_cast<std::size_t>(dim));
  for (int i = 0; i < dim; ++i) {
    amp[static_cast<std::size_t>(i)] = std::complex<float>(s[i].real(), s[i].imag());
  }
  std::vector<std::complex<float>> out(static_cast<std::size_t>(dim), {0.0F, 0.0F});

  for (int k = 0; k < dim; k += 3 * stride) {
    for (int j = 0; j < stride; ++j) {
      const int i0 = k + j + 0 * stride;
      const int i1 = k + j + 1 * stride;
      const int i2 = k + j + 2 * stride;
      const std::complex<float> v0 = amp[static_cast<std::size_t>(i0)];
      const std::complex<float> v1 = amp[static_cast<std::size_t>(i1)];
      const std::complex<float> v2 = amp[static_cast<std::size_t>(i2)];
      for (int r = 0; r < 3; ++r) {
        const std::complex<float> sum =
            static_cast<std::complex<float>>(u[static_cast<std::size_t>(r)][0]) * v0 +
            static_cast<std::complex<float>>(u[static_cast<std::size_t>(r)][1]) * v1 +
            static_cast<std::complex<float>>(u[static_cast<std::size_t>(r)][2]) * v2;
        const int out_idx = k + j + r * stride;
        out[static_cast<std::size_t>(out_idx)] = sum;
      }
    }
  }

  for (int i = 0; i < dim; ++i) {
    s[i] = c10::complex<float>(out[static_cast<std::size_t>(i)].real(), out[static_cast<std::size_t>(i)].imag());
  }
  state_ = std::move(cpu);
}

void TrinarySimDevice::apply_hadamard_subspace(int wire) {
  const float isr2 = static_cast<float>(1.0 / std::sqrt(2.0));
  std::array<std::array<std::complex<float>, 3>, 3> u{};
  u[0][0] = {isr2, 0};
  u[0][1] = {isr2, 0};
  u[1][0] = {isr2, 0};
  u[1][1] = {-isr2, 0};
  u[2][2] = {1, 0};
  apply_single_subspace_unitary(wire, u);
}

void TrinarySimDevice::apply_pauli_x_subspace(int wire) {
  std::array<std::array<std::complex<float>, 3>, 3> u{};
  u[0][1] = {1, 0};
  u[1][0] = {1, 0};
  u[2][2] = {1, 0};
  apply_single_subspace_unitary(wire, u);
}

void TrinarySimDevice::apply_pauli_y_subspace(int wire) {
  std::array<std::array<std::complex<float>, 3>, 3> u{};
  u[0][1] = {0, -1};
  u[1][0] = {0, 1};
  u[2][2] = {1, 0};
  apply_single_subspace_unitary(wire, u);
}

void TrinarySimDevice::apply_pauli_z_subspace(int wire) {
  std::array<std::array<std::complex<float>, 3>, 3> u{};
  u[0][0] = {1, 0};
  u[1][1] = {-1, 0};
  u[2][2] = {1, 0};
  apply_single_subspace_unitary(wire, u);
}

void TrinarySimDevice::apply_rx_subspace(int wire, double theta) {
  const float c = static_cast<float>(std::cos(theta * 0.5));
  const float s = static_cast<float>(std::sin(theta * 0.5));
  std::array<std::array<std::complex<float>, 3>, 3> u{};
  u[0][0] = {c, 0};
  u[0][1] = {0, -s};
  u[1][0] = {0, -s};
  u[1][1] = {c, 0};
  u[2][2] = {1, 0};
  apply_single_subspace_unitary(wire, u);
}

void TrinarySimDevice::apply_ry_subspace(int wire, double theta) {
  const float c = static_cast<float>(std::cos(theta * 0.5));
  const float s = static_cast<float>(std::sin(theta * 0.5));
  std::array<std::array<std::complex<float>, 3>, 3> u{};
  u[0][0] = {c, 0};
  u[0][1] = {-s, 0};
  u[1][0] = {s, 0};
  u[1][1] = {c, 0};
  u[2][2] = {1, 0};
  apply_single_subspace_unitary(wire, u);
}

void TrinarySimDevice::apply_rz_subspace(int wire, double theta) {
  const float p = static_cast<float>(theta * 0.5);
  std::array<std::array<std::complex<float>, 3>, 3> u{};
  u[0][0] = {static_cast<float>(std::cos(p)), static_cast<float>(-std::sin(p))};
  u[1][1] = {static_cast<float>(std::cos(p)), static_cast<float>(std::sin(p))};
  u[2][2] = {1, 0};
  apply_single_subspace_unitary(wire, u);
}

void TrinarySimDevice::apply_cx_subspace(int control, int target) {
  const int dim = ipow3(n_);
  const int sc = stride_for_wire(control);
  const int st = stride_for_wire(target);
  auto cpu = state_.to(torch::kCPU).contiguous();
  auto* buf = cpu.data_ptr<c10::complex<float>>();
  std::vector<std::complex<float>> amp(static_cast<std::size_t>(dim));
  for (int i = 0; i < dim; ++i) {
    amp[static_cast<std::size_t>(i)] = std::complex<float>(buf[i].real(), buf[i].imag());
  }
  std::vector<std::complex<float>> out(static_cast<std::size_t>(dim), {0.0F, 0.0F});
  std::vector<int> tr(static_cast<std::size_t>(n_));
  for (int idx = 0; idx < dim; ++idx) {
    for (int w = 0; w < n_; ++w) {
      tr[static_cast<std::size_t>(w)] = (idx / stride_for_wire(w)) % 3;
    }
    int idx_out = idx;
    if (tr[static_cast<std::size_t>(control)] == 1) {
      const int tt = tr[static_cast<std::size_t>(target)];
      if (tt == 0) {
        idx_out = idx + st;
      } else if (tt == 1) {
        idx_out = idx - st;
      }
    }
    out[static_cast<std::size_t>(idx_out)] = amp[static_cast<std::size_t>(idx)];
  }
  for (int i = 0; i < dim; ++i) {
    buf[i] = c10::complex<float>(out[static_cast<std::size_t>(i)].real(), out[static_cast<std::size_t>(i)].imag());
  }
  (void)sc;
  state_ = std::move(cpu);
}

void TrinarySimDevice::apply_cy_subspace(int control, int target) {
  const int dim = ipow3(n_);
  const int st = stride_for_wire(target);
  auto cpu = state_.to(torch::kCPU).contiguous();
  auto* buf = cpu.data_ptr<c10::complex<float>>();
  std::vector<std::complex<float>> amp(static_cast<std::size_t>(dim));
  for (int i = 0; i < dim; ++i) {
    amp[static_cast<std::size_t>(i)] = std::complex<float>(buf[i].real(), buf[i].imag());
  }
  std::vector<std::complex<float>> out(static_cast<std::size_t>(dim), {0.0F, 0.0F});
  std::vector<int> tr(static_cast<std::size_t>(n_));
  for (int idx = 0; idx < dim; ++idx) {
    for (int w = 0; w < n_; ++w) {
      tr[static_cast<std::size_t>(w)] = (idx / stride_for_wire(w)) % 3;
    }
    if (tr[static_cast<std::size_t>(control)] != 1) {
      out[static_cast<std::size_t>(idx)] = amp[static_cast<std::size_t>(idx)];
      continue;
    }
    const int tt = tr[static_cast<std::size_t>(target)];
    if (tt == 0) {
      const int idx_out = idx + st;
      out[static_cast<std::size_t>(idx_out)] = std::complex<float>(0, 1) * amp[static_cast<std::size_t>(idx)];
    } else if (tt == 1) {
      const int idx_out = idx - st;
      out[static_cast<std::size_t>(idx_out)] = std::complex<float>(0, -1) * amp[static_cast<std::size_t>(idx)];
    } else {
      out[static_cast<std::size_t>(idx)] = amp[static_cast<std::size_t>(idx)];
    }
  }
  for (int i = 0; i < dim; ++i) {
    buf[i] = c10::complex<float>(out[static_cast<std::size_t>(i)].real(), out[static_cast<std::size_t>(i)].imag());
  }
  state_ = std::move(cpu);
}

void TrinarySimDevice::apply_cz_subspace(int control, int target) {
  const int dim = ipow3(n_);
  auto cpu = state_.to(torch::kCPU).contiguous();
  auto* buf = cpu.data_ptr<c10::complex<float>>();
  std::vector<std::complex<float>> amp(static_cast<std::size_t>(dim));
  for (int i = 0; i < dim; ++i) {
    amp[static_cast<std::size_t>(i)] = std::complex<float>(buf[i].real(), buf[i].imag());
  }
  std::vector<std::complex<float>> out(static_cast<std::size_t>(dim), {0.0F, 0.0F});
  std::vector<int> tr(static_cast<std::size_t>(n_));
  for (int idx = 0; idx < dim; ++idx) {
    for (int w = 0; w < n_; ++w) {
      tr[static_cast<std::size_t>(w)] = (idx / stride_for_wire(w)) % 3;
    }
    std::complex<float> phase = {1.0F, 0.0F};
    if (tr[static_cast<std::size_t>(control)] == 1 && tr[static_cast<std::size_t>(target)] == 1) {
      phase = {-1.0F, 0.0F};
    }
    out[static_cast<std::size_t>(idx)] = phase * amp[static_cast<std::size_t>(idx)];
  }
  for (int i = 0; i < dim; ++i) {
    buf[i] = c10::complex<float>(out[static_cast<std::size_t>(i)].real(), out[static_cast<std::size_t>(i)].imag());
  }
  state_ = std::move(cpu);
}

double TrinarySimDevice::expval_pauli_z(int wire) const {
  const int dim = ipow3(n_);
  const int stride = stride_for_wire(wire);
  auto cpu = state_.to(torch::kCPU).contiguous();
  const auto* buf = cpu.data_ptr<c10::complex<float>>();
  double ev = 0.0;
  for (int idx = 0; idx < dim; ++idx) {
    const int tw = (idx / stride) % 3;
    double eig = 0.0;
    if (tw == 0) {
      eig = 1.0;
    } else if (tw == 1) {
      eig = -1.0;
    }
    const float re = buf[idx].real();
    const float im = buf[idx].imag();
    ev += eig * (static_cast<double>(re) * static_cast<double>(re) + static_cast<double>(im) * static_cast<double>(im));
  }
  return ev;
}

int TrinarySimDevice::measure_wire(int wire) {
  const int dim = ipow3(n_);
  const int stride = stride_for_wire(wire);
  auto cpu = state_.to(torch::kCPU).contiguous();
  auto* buf = cpu.data_ptr<c10::complex<float>>();
  std::vector<double> prob(3, 0.0);
  for (int idx = 0; idx < dim; ++idx) {
    const int tw = (idx / stride) % 3;
    const float re = buf[idx].real();
    const float im = buf[idx].imag();
    prob[static_cast<std::size_t>(tw)] +=
        static_cast<double>(re) * static_cast<double>(re) + static_cast<double>(im) * static_cast<double>(im);
  }
  std::discrete_distribution<int> dist(prob.begin(), prob.end());
  const int outcome = dist(rng_);

  std::vector<std::complex<float>> amp(static_cast<std::size_t>(dim));
  for (int i = 0; i < dim; ++i) {
    amp[static_cast<std::size_t>(i)] = std::complex<float>(buf[i].real(), buf[i].imag());
  }
  for (int idx = 0; idx < dim; ++idx) {
    const int tw = (idx / stride) % 3;
    if (tw != outcome) {
      buf[idx] = c10::complex<float>(0.0F, 0.0F);
    } else {
      const double p = prob[static_cast<std::size_t>(outcome)];
      const float inv = p > 1e-20 ? static_cast<float>(1.0 / std::sqrt(p)) : 0.0F;
      buf[idx] = c10::complex<float>(buf[idx].real() * inv, buf[idx].imag() * inv);
    }
  }
  state_ = std::move(cpu);
  return outcome;
}

std::expected<void, std::string> TrinarySimDevice::run_openqasm_ir(const openqasm3::CircuitIR& circ,
                                                                    std::uint64_t seed) {
  if (circ.num_qubits <= 0) {
    return std::unexpected(std::string("circuit has no qubits"));
  }
  reset(circ.num_qubits, seed);
  for (const auto& op : circ.ops) {
    if (std::holds_alternative<openqasm3::OpApply1>(op)) {
      const auto& g = std::get<openqasm3::OpApply1>(op);
      switch (g.gate) {
        case openqasm3::BuiltinGate1::kHadamard:
          apply_hadamard_subspace(g.wire);
          break;
        case openqasm3::BuiltinGate1::kPauliX:
          apply_pauli_x_subspace(g.wire);
          break;
        case openqasm3::BuiltinGate1::kPauliY:
          apply_pauli_y_subspace(g.wire);
          break;
        case openqasm3::BuiltinGate1::kPauliZ:
          apply_pauli_z_subspace(g.wire);
          break;
        case openqasm3::BuiltinGate1::kRx:
          apply_rx_subspace(g.wire, g.param);
          break;
        case openqasm3::BuiltinGate1::kRy:
          apply_ry_subspace(g.wire, g.param);
          break;
        case openqasm3::BuiltinGate1::kRz:
          apply_rz_subspace(g.wire, g.param);
          break;
      }
    } else if (std::holds_alternative<openqasm3::OpApply2>(op)) {
      const auto& g = std::get<openqasm3::OpApply2>(op);
      switch (g.gate) {
        case openqasm3::BuiltinGate2::kCx:
          apply_cx_subspace(g.control, g.target);
          break;
        case openqasm3::BuiltinGate2::kCy:
          apply_cy_subspace(g.control, g.target);
          break;
        case openqasm3::BuiltinGate2::kCz:
          apply_cz_subspace(g.control, g.target);
          break;
      }
    } else if (std::holds_alternative<openqasm3::OpMeasure>(op)) {
      const auto& m = std::get<openqasm3::OpMeasure>(op);
      (void)measure_wire(m.q_wire);
      (void)m.c_bit;
    }
  }
  return {};
}

}  // namespace qminiwasm::quantum::trinary
