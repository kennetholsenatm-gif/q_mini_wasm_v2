#include "../router/expert_fleet_c_api.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

namespace {

double l2_sq(const double* a, const double* b, std::size_t dim) {
  double s = 0.0;
  for (std::size_t d = 0; d < dim; ++d) {
    const double t = a[d] - b[d];
    s += t * t;
  }
  return s;
}

bool brute_topk(const double* query, const double* candidates, std::size_t rows, std::size_t dim, std::size_t k,
                std::size_t* out_indices) {
  if (rows == 0 || k == 0) {
    return false;
  }
  const std::size_t kk = std::min(rows, k);
  std::vector<double> dist(rows);
  for (std::size_t r = 0; r < rows; ++r) {
    dist[r] = l2_sq(query, candidates + r * dim, dim);
  }
  std::vector<unsigned char> used(rows, 0);
  for (std::size_t t = 0; t < kk; ++t) {
    double best = std::numeric_limits<double>::infinity();
    std::size_t best_i = rows;
    for (std::size_t r = 0; r < rows; ++r) {
      if (used[r]) {
        continue;
      }
      if (dist[r] < best) {
        best = dist[r];
        best_i = r;
      }
    }
    if (best_i == rows) {
      return false;
    }
    used[best_i] = 1;
    out_indices[t] = best_i;
  }
  return true;
}

bool idx_sets_equal(const std::size_t* a, const std::size_t* b, std::size_t n) {
  for (std::size_t i = 0; i < n; ++i) {
    if (a[i] != b[i]) {
      return false;
    }
  }
  return true;
}

}  // namespace

bool test_expert_fleet() {
  if (qmw_expert_fleet_abi_version() < 1) {
    return false;
  }

  constexpr std::size_t dim = 3;
  constexpr std::size_t rows = 5;
  const double query[dim] = {1.0, 0.0, 0.0};
  /* Distances squared from query all distinct for stable top-3 ordering. */
  const double candidates[rows * dim] = {
      /* row0 */ 0.0, 0.0, 0.0,   // 1.0
      /* row1 */ 1.5, 0.0, 0.0,   // 0.25
      /* row2 */ 1.05, 0.0, 0.0,  // 0.0025 — nearest
      /* row3 */ 1.2, 0.0, 0.0,    // 0.04
      /* row4 */ 1.08, 0.0, 0.0,   // 0.0064
  };

#if QMINIWASM_HAS_NATIVE_DQAOA_ROUTING
  constexpr std::size_t k = 3;
  std::size_t got[k]{};
  std::size_t want[k]{};
  if (!brute_topk(query, candidates, rows, dim, k, want)) {
    return false;
  }
  const std::size_t nout = qmw_expert_fleet_topk(query, candidates, rows, dim, k, got);
  if (nout != k) {
    return false;
  }
  if (!idx_sets_equal(got, want, k)) {
    return false;
  }

  constexpr std::size_t n = 4;
  double adj[n * n]{};
  for (std::size_t i = 0; i < n; ++i) {
    for (std::size_t j = 0; j < n; ++j) {
      adj[i * n + j] = (i == j) ? 1.0 : 0.1;
    }
  }
  std::size_t labels[n]{};
  qmw_expert_fleet_partition(adj, n, 2, labels);
  bool ok_label = false;
  for (std::size_t i = 0; i < n; ++i) {
    if (labels[i] < 2) {
      ok_label = true;
    }
  }
  if (!ok_label) {
    return false;
  }
#else
  std::size_t got1 = 0;
  got1 = qmw_expert_fleet_topk(query, candidates, rows, dim, 2, nullptr);
  if (got1 != 0) {
    return false;
  }
  std::size_t idx[2]{99, 99};
  got1 = qmw_expert_fleet_topk(query, candidates, rows, dim, 2, idx);
  if (got1 != 0) {
    return false;
  }
  std::size_t labels3[3] = {1, 1, 1};
  qmw_expert_fleet_partition(nullptr, 0, 2, labels3);
  if (labels3[0] != 1 || labels3[1] != 1) {
    return false;
  }
  double adj2[4] = {1, 0, 0, 1};
  std::size_t lb[2]{7, 8};
  qmw_expert_fleet_partition(adj2, 2, 2, lb);
  if (lb[0] != 0 || lb[1] != 0) {
    return false;
  }
#endif

  QmwExpertMemberDescriptor d{};
  d.instance_id = 42;
  d.capability_mask = 3;
  d.embedding_dim = 8;
  if (d.instance_id != 42ULL) {
    return false;
  }

  return true;
}
