#include "qminiwasm/training/native_checkpoint.hpp"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>

namespace qminiwasm::training {
namespace {

std::string json_escape(std::string_view s) {
  std::string o;
  o.reserve(s.size() + 8);
  for (char c : s) {
    switch (c) {
      case '\\':
        o += "\\\\";
        break;
      case '"':
        o += "\\\"";
        break;
      case '\n':
        o += "\\n";
        break;
      case '\r':
        o += "\\r";
        break;
      case '\t':
        o += "\\t";
        break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          std::ostringstream hex;
          hex << "\\u" << std::hex << std::setw(4) << std::setfill('0')
              << static_cast<int>(static_cast<unsigned char>(c));
          o += hex.str();
        } else {
          o += c;
        }
    }
  }
  return o;
}

}  // namespace

bool write_native_training_checkpoint(const std::string& path,
                                      const std::string& run_id,
                                      const std::size_t epoch,
                                      const double train_loss,
                                      const double val_loss,
                                      const double learning_rate,
                                      std::string* error_message) {
  if (path.empty()) {
    return true;
  }
  try {
    const std::filesystem::path p(path);
    if (p.has_parent_path()) {
      std::filesystem::create_directories(p.parent_path());
    }
    std::ostringstream json;
    json << std::setprecision(17);
    json << '{';
    json << "\"format\":\"qminiwasm_native_training_engine\",";
    json << "\"version\":1,";
    json << "\"run_id\":\"" << json_escape(run_id) << "\",";
    json << "\"epoch\":" << epoch << ',';
    json << "\"train_loss\":" << train_loss << ',';
    json << "\"val_loss\":" << val_loss << ',';
    json << "\"learning_rate\":" << learning_rate;
    json << '}';
    const std::string body = json.str();
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
      if (error_message != nullptr) {
        *error_message = "failed to open checkpoint file for write: " + path;
      }
      return false;
    }
    out.write(body.data(), static_cast<std::streamsize>(body.size()));
    if (!out) {
      if (error_message != nullptr) {
        *error_message = "failed to write checkpoint file: " + path;
      }
      return false;
    }
    return true;
  } catch (const std::exception& e) {
    if (error_message != nullptr) {
      *error_message = std::string("checkpoint write error: ") + e.what();
    }
    return false;
  }
}

}  // namespace qminiwasm::training
