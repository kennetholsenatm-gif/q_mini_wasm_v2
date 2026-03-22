#include <wasmedge/wasmedge.h>

#include <string>

namespace qminiwasm::wasmedge_host {

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
