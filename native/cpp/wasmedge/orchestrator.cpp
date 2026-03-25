#include <wasmedge/wasmedge.h>

#include <cstdint>
#include <string>

#include "../wasm/wles_hooks.hpp"

namespace qminiwasm::wasmedge_host {

/**
 * Hosts should snapshot guest linear memory after a deterministic trap / suspend point by copying
 * the Wasm memory buffer and calling qminiwasm::wles::save_linear_memory. Targets &lt;180ms cold
 * resume depend on NVMe bandwidth and snapshot size (Micro-enclave tier).
 */
inline bool wles_snapshot_linear_to_file(const char* path, const uint8_t* guest_mem,
                                         uint32_t byte_len) {
  return qminiwasm::wles::save_linear_memory(path, guest_mem, static_cast<std::size_t>(byte_len));
}


/**
 * Configure the built-in WASI import on ``vm`` to preopen host_workspace_path
 * as guest ``/`` (mapping string ``/:<host_path>`` per WasmEdge docs).
 */
bool vm_enable_wasi_preopen_root(WasmEdge_VMContext* vm, const char* host_workspace_path) {
  if (vm == nullptr || host_workspace_path == nullptr) {
    return false;
  }
  WasmEdge_ModuleInstanceContext* wasi =
      WasmEdge_VMGetImportModuleContext(vm, WasmEdge_HostRegistration_Wasi);
  if (wasi == nullptr) {
    return false;
  }
  const std::string mapping = std::string("/:") + host_workspace_path;
  const char* preopens[1] = {mapping.c_str()};
  WasmEdge_ModuleInstanceInitWASI(wasi, nullptr, 0, nullptr, 0, preopens, 1);
  return true;
}

}  // namespace qminiwasm::wasmedge_host
