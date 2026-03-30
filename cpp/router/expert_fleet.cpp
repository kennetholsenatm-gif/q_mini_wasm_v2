#include "expert_fleet_c_api.h"

#if QMINIWASM_HAS_NATIVE_DQAOA_ROUTING
#include "../qubo/dqaoa_routing_runtime_c_api.h"
#endif

#include <cstring>

extern "C" {

int qmw_expert_fleet_abi_version(void) { return 1; }

size_t qmw_expert_fleet_topk(const double* query, const double* candidates, size_t rows, size_t dim, size_t k,
                             size_t* out_indices) {
#if QMINIWASM_HAS_NATIVE_DQAOA_ROUTING
  return qmw_route_topk_l2_f64(query, candidates, rows, dim, k, out_indices);
#else
  (void)query;
  (void)candidates;
  (void)rows;
  (void)dim;
  (void)k;
  (void)out_indices;
  return 0;
#endif
}

void qmw_expert_fleet_partition(const double* adjacency, size_t n, size_t num_clusters, size_t* labels) {
#if QMINIWASM_HAS_NATIVE_DQAOA_ROUTING
  qmw_route_assign_clusters(adjacency, n, num_clusters, labels);
#else
  (void)adjacency;
  (void)num_clusters;
  if (labels != nullptr && n > 0) {
    std::memset(labels, 0, n * sizeof(size_t));
  }
#endif
}

}
