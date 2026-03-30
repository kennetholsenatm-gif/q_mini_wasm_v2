#include "dqaoa_routing_runtime_c_api.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <utility>
#include <vector>

#if QMINIWASM_HAS_QUANTUM
#include "../quantum/quantum_bridge_c_api.h"
#endif

extern "C" std::size_t qmw_route_topk_l2_f64(const double* query, const double* candidates, std::size_t rows,
                                              std::size_t dim, std::size_t k, std::size_t* out_indices) {
  if (query == nullptr || candidates == nullptr || out_indices == nullptr || rows == 0 || dim == 0 || k == 0) {
    return 0;
  }
  std::vector<std::pair<double, std::size_t>> scored;
  scored.reserve(rows);
  for (std::size_t r = 0; r < rows; ++r) {
    double s = 0.0;
    for (std::size_t d = 0; d < dim; ++d) {
      const double diff = query[d] - candidates[r * dim + d];
      s += diff * diff;
    }
    scored.emplace_back(s, r);
  }
  const std::size_t out_n = std::min(rows, k);
  std::partial_sort(scored.begin(), scored.begin() + out_n, scored.end(),
                    [](const auto& a, const auto& b) { return a.first < b.first; });
  for (std::size_t i = 0; i < out_n; ++i) out_indices[i] = scored[i].second;
  return out_n;
}

extern "C" void qmw_route_assign_clusters(const double* adjacency, std::size_t n, std::size_t num_clusters,
                                           std::size_t* labels) {
  if (adjacency == nullptr || labels == nullptr || n == 0) return;
  const std::size_t k = std::max<std::size_t>(1, std::min<std::size_t>(num_clusters, n));
  std::vector<std::pair<double, std::size_t>> degree;
  degree.reserve(n);
  for (std::size_t i = 0; i < n; ++i) {
    double d = 0.0;
    for (std::size_t j = 0; j < n; ++j) d += adjacency[i * n + j];
    degree.emplace_back(-d, i);
  }
  std::sort(degree.begin(), degree.end());
  for (std::size_t rank = 0; rank < n; ++rank) {
    labels[degree[rank].second] = rank % k;
  }
}

#if QMINIWASM_HAS_QUANTUM
extern "C" double qmw_routing_trinary_expval_pauli_z0(const char* openqasm, unsigned long long seed, int* err_out) {
  return qmw_openqasm_expval_pauli_z0(openqasm, seed, err_out);
}
#else
extern "C" double qmw_routing_trinary_expval_pauli_z0(const char* openqasm, unsigned long long seed, int* err_out) {
  (void)openqasm;
  (void)seed;
  if (err_out != nullptr) {
    *err_out = -1;
  }
  return 0.0;
}
#endif
