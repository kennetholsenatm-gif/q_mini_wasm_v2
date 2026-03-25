#include "../wasm/wles_hooks.hpp"

#include <vector>

#if defined(_WIN32)
#include <io.h>
#define QMW_UNLINK _unlink
#else
#include <unistd.h>
#define QMW_UNLINK unlink
#endif

bool test_wles_hooks() {
  const char* path = "qminiwasm_wles_test.bin";
  std::vector<std::uint8_t> in = {0x01, 0x02, 0xfd, 0x40, 0x00, 0xff};
  if (!qminiwasm::wles::save_linear_memory(path, in.data(), in.size())) {
    return false;
  }
  std::vector<std::uint8_t> out;
  if (!qminiwasm::wles::load_linear_memory(path, out)) {
    QMW_UNLINK(path);
    return false;
  }
  QMW_UNLINK(path);
  return out == in;
}
