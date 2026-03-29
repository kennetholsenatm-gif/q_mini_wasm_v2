#pragma once

#include <cstddef>
#include <cstdint>

namespace qminiwasm::training {

/// Matches Python ``qminiwasm.tpem.trainable_tpem.D_MODEL`` / TernaryWASMExpert default.
inline constexpr std::int64_t kTpemDModel = 4096;

/// First 8 bytes of native interchange files (format version 2).
inline constexpr char kTpemInterchangeMagic[8] = {'Q', 'M', 'W', 'T', 'P', 'E', 'M', '2'};

inline constexpr int kTrainableTpemFormatVersionV2 = 2;

}  // namespace qminiwasm::training
