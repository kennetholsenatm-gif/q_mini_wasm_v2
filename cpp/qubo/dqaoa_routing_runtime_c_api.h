#pragma once

#include <cstddef>

extern "C" {

std::size_t qmw_route_topk_l2_f64(const double* query, const double* candidates, std::size_t rows, std::size_t dim,
                                  std::size_t k, std::size_t* out_indices);

void qmw_route_assign_clusters(const double* adjacency, std::size_t n, std::size_t num_clusters, std::size_t* labels);

}
