#include "../kernels/ternary_forward.hpp"
#include "../pack/w158_pack.hpp"
#include "../simd/cpuid.hpp"

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <cstdint>
#include <vector>

namespace py = pybind11;

PYBIND11_MODULE(qminiwasm_cpp_native, m) {
  m.doc() = "qminiwasm native C++ bridges (pack + SIMD-dispatched matvec)";

  m.def(
      "packed_byte_count",
      [](std::size_t n_trits) { return qminiwasm::pack::packed_byte_count(n_trits); },
      py::arg("n_trits"));

  m.def(
      "pack_ternary_msb",
      [](const std::vector<int>& weights) {
        std::vector<std::uint8_t> out;
        qminiwasm::pack::pack_ternary_msb(weights.data(), weights.size(), out);
        return out;
      },
      py::arg("weights"));

  m.def(
      "unpack_ternary_msb",
      [](const std::vector<std::uint8_t>& packed, int total_trits) {
        std::vector<int> out(static_cast<std::size_t>(total_trits));
        qminiwasm::pack::unpack_ternary_msb(packed.data(), packed.size(), total_trits, out.data());
        return out;
      },
      py::arg("packed"), py::arg("total_trits"));

  m.def(
      "matvec_best",
      [](const std::vector<std::uint8_t>& packed_weights, std::size_t num_rows, int in_features,
         const std::vector<std::int8_t>& activations) {
        std::vector<std::int32_t> out(num_rows);
        qminiwasm::kernels::matvec_best(packed_weights.data(), num_rows, in_features, activations.data(),
                                        out.data());
        return out;
      },
      py::arg("packed_weights"), py::arg("num_rows"), py::arg("in_features"), py::arg("activations"));

  m.def("cpu_has_avx512f", []() { return qminiwasm::simd::detect_cpu_features().avx512f; });
  m.def("cpu_has_avx512vnni", []() { return qminiwasm::simd::detect_cpu_features().avx512vnni; });
  m.def("abi_version", []() { return 2; });
}
