#pragma once

#include <cstddef>
#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Experimental host hook ABI for multi-instance WASM routing/clustering.
 * Register callbacks from the host process; all functions are optional (NULL).
 * Call only from the thread that drives WASM execution unless you add your own sync.
 *
 * **Expert fleet:** ``on_route_hint``\ 's ``features`` / ``dim`` are intended as the query
 * vector for ``qmw_expert_fleet_topk`` in [`router/expert_fleet_c_api.h`](../router/expert_fleet_c_api.h)
 * after the host builds a row-major candidate matrix from ``QmwExpertMemberDescriptor``\ s (out of tree).
 */
typedef struct QmwWASMHostCallbacks {
  void* user_data;
  void (*on_before_execute)(void* user_data, const std::uint8_t* wasm_bytes, std::size_t wasm_len,
                            const char* func_name, const std::int32_t* args, std::size_t arg_count);
  void (*on_after_execute)(void* user_data, const std::uint8_t* wasm_bytes, std::size_t wasm_len,
                           const char* func_name, const std::int32_t* args, std::size_t arg_count,
                           int execute_rc, std::int32_t result);
  void (*on_linear_memory_snapshot)(void* user_data, std::uint64_t instance_id,
                                    const std::uint8_t* mem, std::size_t len, const char* path_or_null);
  void (*on_route_hint)(void* user_data, std::uint64_t instance_id, const double* features,
                        std::size_t dim);
} QmwWASMHostCallbacks;

/** Bump when QmwWASMHostCallbacks layout changes. */
int qmw_wasm_hooks_abi_version(void);

void qmw_wasm_hooks_set(const QmwWASMHostCallbacks* callbacks);
void qmw_wasm_hooks_clear(void);

void qmw_wasm_hooks_notify_before_execute(const std::uint8_t* wasm_bytes, std::size_t wasm_len,
                                          const char* func_name, const std::int32_t* args,
                                          std::size_t arg_count);
void qmw_wasm_hooks_notify_after_execute(const std::uint8_t* wasm_bytes, std::size_t wasm_len,
                                         const char* func_name, const std::int32_t* args,
                                         std::size_t arg_count, int execute_rc, std::int32_t result);
void qmw_wasm_hooks_notify_linear_memory_snapshot(std::uint64_t instance_id, const std::uint8_t* mem,
                                                   std::size_t len, const char* path_or_null);
void qmw_wasm_hooks_notify_route_hint(std::uint64_t instance_id, const double* features, std::size_t dim);

#ifdef __cplusplus
}
#endif
