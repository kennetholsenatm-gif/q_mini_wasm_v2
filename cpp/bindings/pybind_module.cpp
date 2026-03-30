#include "../kernels/ternary_forward.hpp"
#include "../pack/w158_pack.hpp"
#include "../simd/cpuid.hpp"
#include "../wasm/linear_memory_encode.hpp"

#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace py = pybind11;

PYBIND11_MODULE(qminiwasm_cpp_native, m) {
  m.doc() = "qminiwasm native C++ bridges (WLES linear-memory encode, pack, SIMD matvec)";

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

  m.def(
      "encode_linear_memory_u8",
      [](py::bytes mem_b, int result_i32, int first_arg, int d_model, int meta_slots) {
        if (d_model <= 0) {
          throw std::invalid_argument("d_model must be > 0");
        }
        std::string mem = static_cast<std::string>(mem_b);
        py::array_t<float> arr(std::vector<py::ssize_t>{static_cast<py::ssize_t>(d_model)});
        float* p = arr.mutable_data();
        qminiwasm::wasm::encode_linear_memory_u8(
            mem.empty() ? nullptr : reinterpret_cast<const std::uint8_t*>(mem.data()), mem.size(),
            static_cast<std::int32_t>(result_i32), static_cast<std::int32_t>(first_arg), d_model, meta_slots, p);
        return arr;
      },
      py::arg("mem"), py::arg("result_i32") = 0, py::arg("first_arg") = 0, py::arg("d_model") = 4096,
      py::arg("meta_slots") = 8);
}
