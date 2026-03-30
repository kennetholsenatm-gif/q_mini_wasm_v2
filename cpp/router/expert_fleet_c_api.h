#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Metadata for one WASM expert in a fleet (v1; body/IPC out of band). */
typedef struct QmwExpertMemberDescriptor {
  uint64_t instance_id;
  uint32_t capability_mask;
  uint16_t embedding_dim;
  uint16_t reserved;
} QmwExpertMemberDescriptor;

int qmw_expert_fleet_abi_version(void);

/**
 * Expert selection: L2-top-k over row-major ``candidates`` (``rows`` x ``dim``) vs ``query`` (``dim``).
 * Delegates to ``qmw_route_topk_l2_f64`` when ``QMINIWASM_WITH_NATIVE_DQAOA_ROUTING=ON``; otherwise returns 0.
 */
size_t qmw_expert_fleet_topk(const double* query, const double* candidates, size_t rows, size_t dim, size_t k,
                            size_t* out_indices);

/**
 * Partition ``n`` nodes using symmetric ``adjacency`` (row-major ``n`` x ``n``); writes cluster labels.
 * Delegates to ``qmw_route_assign_clusters`` when routing is built; otherwise writes zeros when pointers are valid.
 */
void qmw_expert_fleet_partition(const double* adjacency, size_t n, size_t num_clusters, size_t* labels);

#ifdef __cplusplus
}
#endif
