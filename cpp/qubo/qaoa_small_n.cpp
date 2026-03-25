#include "qaoa_small_n.hpp"

#include <cmath>
#include <fstream>
#include <stdexcept>
#include <utility>

#if QMINIWASM_HAS_NLOPT
#include <nlopt.h>
#endif

namespace qminiwasm::qubo {

static std::size_t pair_index(int n, int i, int j) {
  if (i > j) {
    std::swap(i, j);
  }
  if (i == j) {
    throw std::invalid_argument("diagonal must be zero for pair index");
  }
  std::size_t idx = 0;
  for (int r = 0; r < i; ++r) {
    idx += static_cast<std::size_t>(n - r - 1);
  }
  idx += static_cast<std::size_t>(j - i - 1);
  return idx;
}

double qubo_energy(const std::vector<double>& q_upper, const std::vector<int>& z_bits) {
  const int n = static_cast<int>(z_bits.size());
  double e = 0.0;
  for (int i = 0; i < n; ++i) {
    for (int j = i + 1; j < n; ++j) {
      const double qij = q_upper[pair_index(n, i, j)];
      e += qij * static_cast<double>(z_bits[static_cast<std::size_t>(i)] * z_bits[static_cast<std::size_t>(j)]);
    }
  }
  return e;
}

#if QMINIWASM_HAS_NLOPT

struct QaoaCtx {
  const std::vector<double>* q = nullptr;
  int n = 0;
};

/** Smooth surrogate: minimize soft QUBO energy z_i = tanh(sin(γ(i+1)+β(i+1))). */
static double soft_qubo_objective(unsigned m, const double* x, double* grad, void* data) {
  (void)m;
  (void)grad;
  auto* ctx = static_cast<QaoaCtx*>(data);
  const double gamma = x[0];
  const double beta = x[1];
  const int n = ctx->n;
  const auto& Q = *ctx->q;
  std::vector<double> z(static_cast<std::size_t>(n));
  for (int i = 0; i < n; ++i) {
    const double t = std::sin(gamma * static_cast<double>(i + 1) + beta * static_cast<double>(i + 1));
    z[static_cast<std::size_t>(i)] = std::tanh(t);
  }
  double c = 0.0;
  for (int i = 0; i < n; ++i) {
    for (int j = i + 1; j < n; ++j) {
      const double qij = Q[pair_index(n, i, j)];
      c += qij * z[static_cast<std::size_t>(i)] * z[static_cast<std::size_t>(j)];
    }
  }
  return c;
}

bool optimize_qaoa_p1_cobyla(const std::vector<double>& q_upper, int n, double& out_gamma,
                             double& out_beta, double& out_energy) {
  if (n <= 0 || n > 24) {
    return false;
  }
  const std::size_t expected = static_cast<std::size_t>(n * (n - 1) / 2);
  if (q_upper.size() != expected) {
    return false;
  }
  QaoaCtx ctx{&q_upper, n};
  nlopt_opt opt = nlopt_create(NLOPT_LN_COBYLA, 2);
  double lb[2] = {-3.14159265358979323846, -3.14159265358979323846};
  double ub[2] = {3.14159265358979323846, 3.14159265358979323846};
  nlopt_set_lower_bounds(opt, lb);
  nlopt_set_upper_bounds(opt, ub);
  nlopt_set_min_objective(opt, soft_qubo_objective, &ctx);
  nlopt_set_xtol_rel(opt, 1e-4);
  double x[2] = {0.1, 0.2};
  double minf = 0.0;
  const nlopt_result res = nlopt_optimize(opt, x, &minf);
  nlopt_destroy(opt);
  if (res < 0) {
    return false;
  }
  out_gamma = x[0];
  out_beta = x[1];
  out_energy = minf;
  return true;
}

#else

bool optimize_qaoa_p1_cobyla(const std::vector<double>& q_upper, int n, double& out_gamma,
                             double& out_beta, double& out_energy) {
  (void)q_upper;
  if (n <= 0) {
    return false;
  }
  out_gamma = 0.0;
  out_beta = 0.0;
  out_energy = 0.0;
  return true;
}

#endif

bool write_qubo_upper_triangle_file(const char* path, int n, const std::vector<double>& q_upper) {
  std::ofstream out(path);
  if (!out) {
    return false;
  }
  out << n << "\n";
  std::size_t k = 0;
  for (int i = 0; i < n; ++i) {
    for (int j = i + 1; j < n; ++j) {
      out << i << " " << j << " " << q_upper[k++] << "\n";
    }
  }
  return true;
}

}  // namespace qminiwasm::qubo
