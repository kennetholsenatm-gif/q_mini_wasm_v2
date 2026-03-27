// pybind11 extension: packed unsigned ternary digits {0,1,2} dot int8 activations.
// Per lane: digit * activation - offset_per_lane (matches WASM scalar kernel).

#include <cstdint>
#include <vector>
#include <algorithm>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <array>

#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>

namespace py = pybind11;
static constexpr int TRITS_PER_BYTE = 5;

static inline std::uint8_t signed_to_digit(int w) {
  if (w == -1) return 0;
  if (w == 0) return 1;
  if (w == 1) return 2;
  throw std::invalid_argument("expected ternary weight -1, 0, or 1");
}

static inline int digit_to_signed(std::uint8_t d) {
  if (d == 0) return -1;
  if (d == 1) return 0;
  if (d == 2) return 1;
  throw std::invalid_argument("expected base-3 digit 0..2");
}

static py::bytes pack_ternary_list_native(const std::vector<int>& weights) {
  static const int POW3_BY_K[6][5] = {
      {0, 0, 0, 0, 0},
      {1, 0, 0, 0, 0},
      {3, 1, 0, 0, 0},
      {9, 3, 1, 0, 0},
      {27, 9, 3, 1, 0},
      {81, 27, 9, 3, 1},
  };

  std::string out;
  out.reserve((weights.size() + TRITS_PER_BYTE - 1) / TRITS_PER_BYTE);
  std::size_t i = 0;
  while (i < weights.size()) {
    const int k = static_cast<int>(
        std::min<std::size_t>(TRITS_PER_BYTE, weights.size() - i));
    int byte_val = 0;
    for (int j = 0; j < k; ++j) {
      const auto trit = static_cast<int>(signed_to_digit(weights[i + static_cast<std::size_t>(j)]));
      byte_val += trit * POW3_BY_K[k][j];
    }
    out.push_back(static_cast<char>(byte_val & 0xFF));
    i += static_cast<std::size_t>(k);
  }
  return py::bytes(out);
}

static py::list unpack_ternary_list_native(py::bytes packed_b, int total_trits) {
  if (total_trits < 0) {
    throw std::invalid_argument("total_trits must be non-negative");
  }
  static const int POW3_BY_K[6][5] = {
      {0, 0, 0, 0, 0},
      {1, 0, 0, 0, 0},
      {3, 1, 0, 0, 0},
      {9, 3, 1, 0, 0},
      {27, 9, 3, 1, 0},
      {81, 27, 9, 3, 1},
  };
  static const std::array<int, 6> MAX_VALID_BY_K = {0, 2, 8, 26, 80, 242};
  static const auto DECODE_LUT_BY_K = []() {
    std::array<std::array<std::array<int8_t, 5>, 256>, 6> lut{};
    for (int k = 1; k <= TRITS_PER_BYTE; ++k) {
      for (int b = 0; b <= 255; ++b) {
        if (b > MAX_VALID_BY_K[k]) {
          continue;
        }
        int rem = b;
        for (int j = 0; j < k; ++j) {
          const int power = POW3_BY_K[k][j];
          const int trit = rem / power;
          rem %= power;
          lut[k][b][j] = static_cast<int8_t>(trit - 1);
        }
      }
    }
    return lut;
  }();

  std::string packed = packed_b;
  std::vector<int> out;
  out.reserve(static_cast<std::size_t>(total_trits));
  int produced = 0;
  for (unsigned char byte_val_uc : packed) {
    if (produced >= total_trits) {
      break;
    }
    const int remaining = total_trits - produced;
    const int k = std::min(TRITS_PER_BYTE, remaining);
    const int byte_val = static_cast<int>(byte_val_uc);
    if (byte_val <= MAX_VALID_BY_K[k]) {
      const auto& row = DECODE_LUT_BY_K[k][byte_val];
      for (int j = 0; j < k; ++j) {
        out.push_back(static_cast<int>(row[j]));
      }
      produced += k;
    } else {
      int rem = byte_val;
      for (int j = 0; j < k; ++j) {
        const int power = POW3_BY_K[k][j];
        const int trit = rem / power;
        rem %= power;
        out.push_back(digit_to_signed(static_cast<std::uint8_t>(trit)));
        produced += 1;
      }
    }
  }
  return py::cast(out);
}

static py::array_t<float> encode_linear_memory_u8_native(
    py::bytes mem_b,
    int result_i32,
    int first_arg,
    int d_model,
    int meta_slots) {
  if (d_model <= 0) {
    throw std::invalid_argument("d_model must be > 0");
  }
  if (meta_slots < 0 || meta_slots > d_model) {
    throw std::invalid_argument("meta_slots out of range");
  }
  const int body_slots = d_model - meta_slots;
  const float ENCODING_VERSION = 1.0f;

  std::string mem = mem_b;
  py::array_t<float> arr(d_model);
  auto r = arr.mutable_unchecked<1>();
  for (py::ssize_t i = 0; i < static_cast<py::ssize_t>(d_model); ++i) {
    r(i) = 0.0f;
  }
  r(0) = ENCODING_VERSION;
  r(1) = static_cast<float>(std::abs(first_arg) % 65536) / 65535.0f;
  r(2) = static_cast<float>(std::abs(result_i32) % 65536) / 65535.0f;
  r(3) = static_cast<float>(std::min<std::size_t>(mem.size(), 16777215ULL)) / 16777215.0f;
  const int n = static_cast<int>(std::min<std::size_t>(static_cast<std::size_t>(body_slots), mem.size()));
  for (int i = 0; i < n; ++i) {
    const auto v = static_cast<std::uint8_t>(static_cast<unsigned char>(mem[static_cast<std::size_t>(i)]));
    r(meta_slots + i) = static_cast<float>(v) / 255.0f;
  }
  return arr;
}

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
  m.def("pack_ternary_list", &pack_ternary_list_native, py::arg("weights"),
        "Pack {-1,0,1} sequence into bytes (MSB-first, 5 trits/byte).");
  m.def("unpack_ternary_list", &unpack_ternary_list_native, py::arg("packed"),
        py::arg("total_trits"),
        "Unpack bytes to signed trits {-1,0,1} with requested output length.");
  m.def("encode_linear_memory_u8", &encode_linear_memory_u8_native, py::arg("mem"),
        py::arg("result_i32") = 0, py::arg("first_arg") = 0,
        py::arg("d_model") = 4096, py::arg("meta_slots") = 8,
        "Encode bytes into fixed-width float vector for WLES features.");
  m.def("abi_version", []() { return 1; });
  m.def("native_capabilities", []() {
    py::dict caps;
    caps["abi_version"] = 1;
    caps["dot_u8_i8"] = true;
    caps["pack_ternary_list"] = true;
    caps["unpack_ternary_list"] = true;
    caps["encode_linear_memory_u8"] = true;
    return caps;
  });
}
