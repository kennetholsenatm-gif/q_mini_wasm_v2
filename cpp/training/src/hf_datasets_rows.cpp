#include "qminiwasm/training/hf_datasets_rows.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <sstream>
#include <stdexcept>

namespace qminiwasm::training {
namespace {

std::string url_encode_component(const std::string& s) {
  static const char* hex = "0123456789ABCDEF";
  std::string out;
  out.reserve(s.size() * 3);
  for (unsigned char c : s) {
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
        c == '.' || c == '~') {
      out.push_back(static_cast<char>(c));
    } else {
      out.push_back('%');
      out.push_back(hex[c >> 4]);
      out.push_back(hex[c & 0xF]);
    }
  }
  return out;
}

std::string trim_leading_ws(const std::string& s) {
  std::size_t i = 0;
  while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i])) != 0) {
    ++i;
  }
  return s.substr(i);
}

// Bare curl often gets HTML (Cloudflare / bot wall). Send Accept + User-Agent; optional HF_TOKEN for gated sets.
std::string build_hf_rows_curl_cmd(const std::string& url) {
  const char* tok = std::getenv("HF_TOKEN");
#ifdef _WIN32
  std::string cmd = std::string("curl.exe -sS -L --max-time 120 ")
                    + "-H \"Accept: application/json\" "
                    + "-H \"User-Agent: qminiwasm-native-training/1.0 (+https://github.com)\" ";
  if (tok != nullptr && tok[0] != '\0') {
    std::string t(tok);
    for (char& c : t) {
      if (c == '"' || c == '\r' || c == '\n') {
        c = '_';
      }
    }
    cmd += "-H \"Authorization: Bearer ";
    cmd += t;
    cmd += "\" ";
  }
  cmd += "\"";
  for (char c : url) {
    if (c == '"') {
      cmd += "\"\"";
    } else {
      cmd += c;
    }
  }
  cmd += "\"";
  return cmd;
#else
  std::string cmd = std::string("curl -sS -L --max-time 120 ")
                    + "-H 'Accept: application/json' "
                    + "-H 'User-Agent: qminiwasm-native-training/1.0 (+https://github.com)' ";
  if (tok != nullptr && tok[0] != '\0') {
    cmd += "-H 'Authorization: Bearer ";
    for (const char* p = tok; *p != '\0'; ++p) {
      if (*p == '\'') {
        cmd += "'\\''";
      } else {
        cmd += *p;
      }
    }
    cmd += "' ";
  }
  cmd += "'";
  for (char c : url) {
    if (c == '\'') {
      cmd += "'\\''";
    } else {
      cmd += c;
    }
  }
  cmd += "'";
  return cmd;
#endif
}

std::string read_popen_curl(const std::string& url, std::string* error_message) {
  const std::string cmd = build_hf_rows_curl_cmd(url);
#if defined(_WIN32)
  FILE* p = _popen(cmd.c_str(), "r");
#else
  FILE* p = popen(cmd.c_str(), "r");
#endif
  if (p == nullptr) {
    if (error_message != nullptr) {
      *error_message = "popen curl failed (is curl in PATH?)";
    }
    return {};
  }
  std::string out;
  std::array<char, 8192> buf{};
  while (true) {
    std::size_t n = std::fread(buf.data(), 1, buf.size(), p);
    if (n > 0) {
      out.append(buf.data(), n);
    }
    if (n < buf.size()) {
      break;
    }
  }
#if defined(_WIN32)
  const int rc = _pclose(p);
#else
  const int rc = pclose(p);
#endif
  if (rc != 0) {
    if (error_message != nullptr) {
      *error_message = "curl exited non-zero (" + std::to_string(rc) + ")";
    }
    return {};
  }
  return out;
}

void encode_text_to_row(const std::string& text, std::int64_t io_dim, std::uint64_t seed,
                        float* out_row) {
  std::uint64_t h = 14695981039346656037ULL ^ seed;
  for (unsigned char c : text) {
    h ^= static_cast<std::uint64_t>(c);
    h *= 1099511628211ULL;
  }
  for (std::int64_t i = 0; i < io_dim; ++i) {
    h ^= static_cast<std::uint64_t>(i + 1) * 0xD6E8FEB866B4DCLL;
    h *= 1099511628211ULL;
    const double u = static_cast<double>(h & ((1ULL << 53) - 1)) / static_cast<double>(1ULL << 53);
    out_row[static_cast<std::size_t>(i)] = static_cast<float>(2.0 * u - 1.0);
  }
}

std::string row_to_text(const nlohmann::json& row_obj, const std::vector<std::string>* field_order) {
  if (!row_obj.is_object()) {
    return {};
  }
  if (field_order != nullptr && !field_order->empty()) {
    std::string acc;
    for (const auto& key : *field_order) {
      auto it = row_obj.find(key);
      if (it == row_obj.end() || it->is_null()) {
        continue;
      }
      if (it->is_string()) {
        acc += it->get<std::string>();
      } else {
        acc += it->dump();
      }
      acc.push_back('\n');
    }
    if (!acc.empty()) {
      return acc;
    }
  }
  std::string acc;
  for (auto it = row_obj.begin(); it != row_obj.end(); ++it) {
    if (it.value().is_string()) {
      acc += it.value().get<std::string>();
      acc.push_back('\n');
    }
  }
  if (acc.empty()) {
    acc = row_obj.dump();
  }
  return acc;
}

// Multi-config Hub datasets have no "default" subset; empty gRPC config used to become "default" → HF Not found.
bool hf_resolve_config_from_info(const std::string& dataset_id, std::string* out_config,
                                 std::string* error_message) {
  if (out_config == nullptr) {
    return false;
  }
  std::ostringstream url;
  url << "https://datasets-server.huggingface.co/info?dataset=" << url_encode_component(dataset_id);
  std::string err;
  const std::string body = read_popen_curl(url.str(), &err);
  if (body.empty()) {
    if (error_message != nullptr) {
      *error_message = "HF info fetch failed: " + err;
    }
    return false;
  }
  const std::string body_trim = trim_leading_ws(body);
  if (!body_trim.empty() && body_trim[0] == '<') {
    if (error_message != nullptr) {
      *error_message = "HF info returned HTML, not JSON. Snippet: " + body_trim.substr(0, 160);
    }
    return false;
  }
  nlohmann::json j;
  try {
    j = nlohmann::json::parse(body);
  } catch (const std::exception& e) {
    if (error_message != nullptr) {
      *error_message = std::string("HF info JSON parse: ") + e.what();
    }
    return false;
  }
  if (j.contains("error") && !j["error"].is_null()) {
    if (error_message != nullptr) {
      if (j["error"].is_string()) {
        *error_message = "HF info API error: " + j["error"].get<std::string>();
      } else {
        *error_message = "HF info API error: " + j["error"].dump();
      }
    }
    return false;
  }
  if (!j.contains("dataset_info") || !j["dataset_info"].is_object()) {
    if (error_message != nullptr) {
      *error_message = "HF info: missing dataset_info object";
    }
    return false;
  }
  const auto& di = j["dataset_info"];
  if (di.empty()) {
    if (error_message != nullptr) {
      *error_message = "HF info: dataset has no configs";
    }
    return false;
  }
  if (di.contains("all") && di["all"].is_object()) {
    *out_config = "all";
    return true;
  }
  if (di.contains("default") && di["default"].is_object()) {
    *out_config = "default";
    return true;
  }
  std::string best;
  for (auto it = di.begin(); it != di.end(); ++it) {
    if (!it.value().is_object()) {
      continue;
    }
    if (best.empty() || it.key() < best) {
      best = it.key();
    }
  }
  if (best.empty()) {
    if (error_message != nullptr) {
      *error_message = "HF info: no usable config names";
    }
    return false;
  }
  *out_config = best;
  return true;
}

}  // namespace

bool hf_fetch_encoded_rows(const std::string& dataset_id, const std::string& config_name,
                           const std::string& split, const std::string& revision,
                           const std::vector<std::string>* text_field_order, std::uint32_t max_rows,
                           std::int64_t io_dim, std::uint64_t seed, std::vector<float>* out_row_major,
                           std::string* error_message) {
  if (out_row_major == nullptr || io_dim < 8) {
    if (error_message != nullptr) {
      *error_message = "hf_fetch_encoded_rows: bad args";
    }
    return false;
  }
  out_row_major->clear();
  std::string cfg = config_name;
  if (cfg.empty() || cfg == "default") {
    if (!hf_resolve_config_from_info(dataset_id, &cfg, error_message)) {
      return false;
    }
  }
  const std::uint32_t cap = max_rows > 0 ? max_rows : 2048U;
  std::uint32_t offset = 0;
  const std::uint32_t page = std::min<std::uint32_t>(100, cap);
  while (offset < cap) {
    const std::uint32_t len = std::min(page, cap - offset);
    std::ostringstream url;
    url << "https://datasets-server.huggingface.co/rows?"
        << "dataset=" << url_encode_component(dataset_id) << "&config=" << url_encode_component(cfg)
        << "&split=" << url_encode_component(split) << "&offset=" << offset << "&length=" << len;
    if (!revision.empty()) {
      url << "&revision=" << url_encode_component(revision);
    }
    std::string err;
    const std::string body = read_popen_curl(url.str(), &err);
    if (body.empty()) {
      if (error_message != nullptr) {
        *error_message = "HF rows fetch failed: " + err;
      }
      return false;
    }
    const std::string body_trim = trim_leading_ws(body);
    if (!body_trim.empty() && body_trim[0] == '<') {
      if (error_message != nullptr) {
        const std::size_t snip = std::min<std::size_t>(body_trim.size(), 200);
        *error_message =
            "HF rows endpoint returned HTML, not JSON (bot wall, proxy, or error page). "
            "Ensure outbound HTTPS to datasets-server.huggingface.co; set HF_TOKEN for gated datasets. Snippet: " +
            body_trim.substr(0, snip);
      }
      return false;
    }
    nlohmann::json j;
    try {
      j = nlohmann::json::parse(body);
    } catch (const std::exception& e) {
      if (error_message != nullptr) {
        *error_message = std::string("HF JSON parse: ") + e.what();
      }
      return false;
    }
    if (j.contains("error") && !j["error"].is_null()) {
      if (error_message != nullptr) {
        if (j["error"].is_string()) {
          *error_message = "HF rows API error: " + j["error"].get<std::string>();
        } else {
          *error_message = "HF rows API error: " + j["error"].dump();
        }
      }
      return false;
    }
    if (!j.contains("rows") || j["rows"].is_null() || !j["rows"].is_array()) {
      if (error_message != nullptr) {
        if (j.contains("error") && !j["error"].is_null()) {
          if (j["error"].is_string()) {
            *error_message = "HF rows API error: " + j["error"].get<std::string>();
          } else {
            *error_message = "HF rows API error: " + j["error"].dump();
          }
        } else {
          *error_message =
              "HF response missing rows array (check dataset id, config, split): " + body.substr(0, 240);
        }
      }
      return false;
    }
    const auto& rows = j["rows"];
    if (rows.empty()) {
      break;
    }
    for (const auto& row_wrap : rows) {
      nlohmann::json row_obj;
      if (row_wrap.contains("row")) {
        row_obj = row_wrap["row"];
      } else {
        row_obj = row_wrap;
      }
      const std::string txt = row_to_text(row_obj, text_field_order);
      const std::size_t base = out_row_major->size();
      out_row_major->resize(base + static_cast<std::size_t>(io_dim));
      encode_text_to_row(txt, io_dim, seed ^ static_cast<std::uint64_t>(offset), out_row_major->data() + base);
      offset += 1;
      if (offset >= cap) {
        break;
      }
    }
    if (rows.size() < len) {
      break;
    }
  }
  if (out_row_major->empty()) {
    if (error_message != nullptr) {
      *error_message = "HF fetch produced zero rows";
    }
    return false;
  }
  return true;
}

void hf_append_mesh_blend(std::vector<float>* row_major, std::int64_t io_dim, std::uint32_t base_rows,
                          double mesh_blend_fraction, std::uint64_t seed) {
  if (row_major == nullptr || io_dim < 8 || mesh_blend_fraction <= 0.0 || base_rows == 0) {
    return;
  }
  const auto extra = static_cast<std::uint32_t>(static_cast<double>(base_rows) * mesh_blend_fraction);
  if (extra == 0) {
    return;
  }
  std::mt19937_64 rng(seed ^ 0xC0DEC0DEC0DEC0DEULL);
  for (std::uint32_t i = 0; i < extra; ++i) {
    std::string synthetic = "mesh_synth:";
    synthetic += std::to_string(static_cast<std::uint64_t>(rng()));
    const std::size_t base = row_major->size();
    row_major->resize(base + static_cast<std::size_t>(io_dim));
    encode_text_to_row(synthetic, io_dim, seed ^ rng(), row_major->data() + base);
  }
}

}  // namespace qminiwasm::training
