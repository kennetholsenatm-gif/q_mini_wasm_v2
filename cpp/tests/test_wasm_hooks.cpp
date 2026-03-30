#include "../wasm/wasm_host_hooks_c_api.h"
#include "../wasm/wles_hooks.hpp"

#include "../wasmedge/engine_bridge_c_api.h"

#include <cstdint>
#include <cstring>
#include <vector>

#if defined(_WIN32)
#include <io.h>
#define QMW_UNLINK _unlink
#else
#include <unistd.h>
#define QMW_UNLINK unlink
#endif

namespace {

std::size_t g_before = 0;
std::size_t g_after = 0;
std::size_t g_snapshots = 0;
std::size_t g_route = 0;
std::size_t g_last_snapshot_len = 0;
const char* g_last_path = nullptr;
std::int32_t g_last_result = -1;
int g_last_rc = 0;

void cb_before(void*, const std::uint8_t*, std::size_t, const char*, const std::int32_t*, std::size_t) {
  ++g_before;
}

void cb_after(void*, const std::uint8_t*, std::size_t, const char*, const std::int32_t*, std::size_t, int execute_rc,
              std::int32_t result) {
  ++g_after;
  g_last_rc = execute_rc;
  g_last_result = result;
}

void cb_snapshot(void*, std::uint64_t, const std::uint8_t* mem, std::size_t len, const char* path_or_null) {
  ++g_snapshots;
  g_last_snapshot_len = len;
  g_last_path = path_or_null;
  (void)mem;
}

void cb_route(void*, std::uint64_t, const double* f, std::size_t dim) {
  ++g_route;
  (void)f;
  (void)dim;
}

}  // namespace

bool test_wasm_hooks() {
  g_before = g_after = g_snapshots = g_route = 0;
  g_last_snapshot_len = 0;
  g_last_path = nullptr;
  g_last_result = -1;
  g_last_rc = 0;

  QmwWASMHostCallbacks cbs{};
  cbs.user_data = nullptr;
  cbs.on_before_execute = cb_before;
  cbs.on_after_execute = cb_after;
  cbs.on_linear_memory_snapshot = cb_snapshot;
  cbs.on_route_hint = cb_route;
  qmw_wasm_hooks_set(&cbs);

  const char* path = "qminiwasm_wasm_hooks_snap_test.bin";
  std::vector<std::uint8_t> mem = {0xab, 0xcd};
  if (!qminiwasm::wles::save_linear_memory(path, mem.data(), mem.size())) {
    qmw_wasm_hooks_clear();
    return false;
  }
  if (g_snapshots != 1 || g_last_snapshot_len != 2 || g_last_path == nullptr || std::strcmp(g_last_path, path) != 0) {
    qmw_wasm_hooks_clear();
    QMW_UNLINK(path);
    return false;
  }
  std::vector<std::uint8_t> roundtrip;
  if (!qminiwasm::wles::load_linear_memory(path, roundtrip) || roundtrip != mem) {
    qmw_wasm_hooks_clear();
    QMW_UNLINK(path);
    return false;
  }
  QMW_UNLINK(path);

  double feat[] = {1.0, 2.0};
  qmw_wasm_hooks_notify_route_hint(42, feat, 2);
  if (g_route != 1) {
    qmw_wasm_hooks_clear();
    return false;
  }

#if QMINIWASM_HAS_NATIVE_WASM_HOST
  const std::uint8_t wasm_fake[] = {0x00, 0x61, 0x73, 0x6d};
  const std::int32_t args[] = {7, 8};
  std::int32_t out = 0;
  const int rc = qmw_wasmedge_execute(wasm_fake, sizeof(wasm_fake), "run", args, 2, &out);
  if (rc != -38 || out != 0 || g_before < 1 || g_after < 1) {
    qmw_wasm_hooks_clear();
    return false;
  }
  if (g_last_rc != -38 || g_last_result != 0) {
    qmw_wasm_hooks_clear();
    return false;
  }
  const int rc2 = qmw_wasmedge_execute(wasm_fake, sizeof(wasm_fake), "run", args, 2, nullptr);
  if (rc2 != -1 || g_last_rc != -1) {
    qmw_wasm_hooks_clear();
    return false;
  }
#endif

  qmw_wasm_hooks_clear();
  if (qmw_wasm_hooks_abi_version() < 1) {
    return false;
  }
  return true;
}
