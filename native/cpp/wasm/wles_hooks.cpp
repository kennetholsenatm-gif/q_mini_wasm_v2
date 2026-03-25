#include "wles_hooks.hpp"

#include <fstream>

namespace qminiwasm::wles {

bool save_linear_memory(const char* path, const std::uint8_t* data, std::size_t len) {
  if (path == nullptr || data == nullptr) {
    return false;
  }
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  if (!f) {
    return false;
  }
  f.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(len));
  return static_cast<bool>(f);
}

bool load_linear_memory(const char* path, std::vector<std::uint8_t>& out) {
  if (path == nullptr) {
    return false;
  }
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    return false;
  }
  f.seekg(0, std::ios::end);
  const auto sz = f.tellg();
  if (sz < 0) {
    return false;
  }
  f.seekg(0, std::ios::beg);
  out.resize(static_cast<std::size_t>(sz));
  if (!out.empty()) {
    f.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
  }
  return static_cast<bool>(f);
}

}  // namespace qminiwasm::wles
