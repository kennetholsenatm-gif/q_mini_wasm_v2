#pragma once

#include <cstddef>
#include <string>

namespace qminiwasm::training {

// Writes a small JSON manifest when LibTorch is disabled. With LibTorch, checkpoints are interchange v2; see README.
bool write_native_training_checkpoint(const std::string& path,
                                      const std::string& run_id,
                                      std::size_t epoch,
                                      double train_loss,
                                      double val_loss,
                                      double learning_rate,
                                      std::string* error_message);

}  // namespace qminiwasm::training
