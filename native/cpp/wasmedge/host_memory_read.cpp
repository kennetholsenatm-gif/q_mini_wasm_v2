#include <wasmedge/wasmedge.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace qminiwasm::wasmedge_host {

bool read_linear_memory_bounded(const WasmEdge_CallingFrameContext* frame, uint32_t ptr, uint32_t len,
                                std::size_t max_len, std::vector<std::uint8_t>& out) {
  out.clear();
  if (frame == nullptr || len == 0) {
    return false;
  }
  const std::size_t n = static_cast<std::size_t>(len);
  if (n > max_len) {
    return false;
  }
  WasmEdge_MemoryInstanceContext* mem = WasmEdge_CallingFrameGetMemoryInstance(frame, 0);
  if (mem == nullptr) {
    return false;
  }
  out.resize(n);
  const WasmEdge_Result r =
      WasmEdge_MemoryInstanceGetData(mem, out.data(), static_cast<uint64_t>(ptr), static_cast<uint64_t>(len));
  if (!WasmEdge_ResultOK(r)) {
    out.clear();
    return false;
  }
  return true;
}

std::string bytes_to_nul_safe_string(const std::vector<std::uint8_t>& data, std::size_t max_chars) {
  std::size_t end = 0;
  while (end < data.size() && end < max_chars && data[end] != 0) {
    ++end;
  }
  return std::string(reinterpret_cast<const char*>(data.data()), end);
}

}  // namespace qminiwasm::wasmedge_host
