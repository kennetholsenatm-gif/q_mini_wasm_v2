// pybind11 extension: packed unsigned ternary digits {0,1,2} dot int8 activations.
// Per lane: digit * activation - offset_per_lane (matches WASM scalar kernel).

#include <cstdint>
#include <stdexcept>
#include <string>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace py = pybind11;

static int dot_u8_i8_bytes(const std::string& w, const std::string& a, int offset_per_lane) {
  if (w.size() != a.size()) {
    throw std::invalid_argument("weights and activations must have the same length");
  }
  const std::size_t n = w.size();
  long long acc = 0;
  for (std::size_t i = 0; i < n; ++i) {
    const auto wi = static_cast<std::uint8_t>(static_cast<unsigned char>(w[i]));
    const auto ai = static_cast<std::int8_t>(static_cast<unsigned char>(a[i]));
    acc += static_cast<long long>(wi) * static_cast<long long>(ai) -
           static_cast<long long>(offset_per_lane);
  }
  return static_cast<int>(acc);
}

PYBIND11_MODULE(_native_ternary, m) {
  m.doc() = "CPU reference dot for packed ternary digits × int8 activations.";
  m.def("dot_u8_i8", &dot_u8_i8_bytes, py::arg("weights_u8"), py::arg("activations_i8"),
        py::arg("offset_per_lane") = 1,
        R"(Sum over i of (weights[i] * signed_int8(activations[i]) - offset_per_lane).

        ``activations`` bytes are interpreted as two's-complement int8.)");
  m.def("abi_version", []() { return 1; });
}
