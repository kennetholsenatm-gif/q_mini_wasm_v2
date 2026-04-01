#include "qutrit_clifford_service.hpp"

#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>

#include <stdexcept>

namespace qminiwasm::quantum {

QutritCliffordServiceImpl::QutritCliffordServiceImpl() = default;
QutritCliffordServiceImpl::~QutritCliffordServiceImpl() = default;

// ============================================================================
// Helper Methods
// ============================================================================

QutritTableau* QutritCliffordServiceImpl::GetTableau(int64_t id) {
  std::lock_guard<std::mutex> lock(tableaus_mutex_);
  auto it = tableaus_.find(id);
  return (it != tableaus_.end()) ? it->second.get() : nullptr;
}

QutritGraphState* QutritCliffordServiceImpl::GetGraphState(int64_t id) {
  std::lock_guard<std::mutex> lock(graph_states_mutex_);
  auto it = graph_states_.find(id);
  return (it != graph_states_.end()) ? it->second.get() : nullptr;
}

CliffordScrambling* QutritCliffordServiceImpl::GetScrambling(int64_t id) {
  std::lock_guard<std::mutex> lock(scramblings_mutex_);
  auto it = scramblings_.find(id);
  return (it != scramblings_.end()) ? it->second.get() : nullptr;
}

QutritBellAttention* QutritCliffordServiceImpl::GetAttention(int64_t id) {
  std::lock_guard<std::mutex> lock(attentions_mutex_);
  auto it = attentions_.find(id);
  return (it != attentions_.end()) ? it->second.get() : nullptr;
}

// ============================================================================
// Qutrit Tableau Operations
// ============================================================================

int64_t QutritCliffordServiceImpl::CreateTableau(int32_t n_qutrits) {
  std::lock_guard<std::mutex> lock(tableaus_mutex_);
  int64_t id = next_tableau_id_++;
  tableaus_[id] = std::make_unique<QutritTableau>(n_qutrits);
  return id;
}

bool QutritCliffordServiceImpl::DestroyTableau(int64_t tableau_id) {
  std::lock_guard<std::mutex> lock(tableaus_mutex_);
  return tableaus_.erase(tableau_id) > 0;
}

int64_t QutritCliffordServiceImpl::CopyTableau(int64_t source_id) {
  auto* src = GetTableau(source_id);
  if (!src) return -1;

  std::lock_guard<std::mutex> lock(tableaus_mutex_);
  int64_t id = next_tableau_id_++;
  tableaus_[id] = std::make_unique<QutritTableau>(src->copy());
  return id;
}

bool QutritCliffordServiceImpl::ApplyH3(int64_t tableau_id, int32_t qubit) {
  auto* tab = GetTableau(tableau_id);
  if (!tab) return false;
  try {
    tab->apply_h3(qubit);
    return true;
  } catch (...) {
    return false;
  }
}

bool QutritCliffordServiceImpl::ApplyS3(int64_t tableau_id, int32_t qubit) {
  auto* tab = GetTableau(tableau_id);
  if (!tab) return false;
  try {
    tab->apply_s3(qubit);
    return true;
  } catch (...) {
    return false;
  }
}

bool QutritCliffordServiceImpl::ApplyCZ3(int64_t tableau_id, int32_t control, int32_t target) {
  auto* tab = GetTableau(tableau_id);
  if (!tab) return false;
  try {
    tab->apply_cz3(control, target);
    return true;
  } catch (...) {
    return false;
  }
}

bool QutritCliffordServiceImpl::ApplyX3(int64_t tableau_id, int32_t qubit, int32_t power) {
  auto* tab = GetTableau(tableau_id);
  if (!tab) return false;
  try {
    tab->apply_x3(qubit, power);
    return true;
  } catch (...) {
    return false;
  }
}

bool QutritCliffordServiceImpl::ApplyZ3(int64_t tableau_id, int32_t qubit, int32_t power) {
  auto* tab = GetTableau(tableau_id);
  if (!tab) return false;
  try {
    tab->apply_z3(qubit, power);
    return true;
  } catch (...) {
    return false;
  }
}

int32_t QutritCliffordServiceImpl::MeasureTableau(int64_t tableau_id, int32_t qubit) {
  auto* tab = GetTableau(tableau_id);
  if (!tab) return -2;  // Error code
  try {
    return tab->measure(qubit);
  } catch (...) {
    return -2;
  }
}

std::string QutritCliffordServiceImpl::GetStabilizerString(int64_t tableau_id, int32_t row) {
  auto* tab = GetTableau(tableau_id);
  if (!tab) return "";
  try {
    return tab->stabilizer_string(row);
  } catch (...) {
    return "";
  }
}

// ============================================================================
// Error Correction Operations
// ============================================================================

int64_t QutritCliffordServiceImpl::Encode513(int32_t state) {
  std::lock_guard<std::mutex> lock(tableaus_mutex_);
  int64_t id = next_tableau_id_++;
  tableaus_[id] = std::make_unique<QutritTableau>(encode_513(state));
  return id;
}

int64_t QutritCliffordServiceImpl::Encode312(int32_t state) {
  std::lock_guard<std::mutex> lock(tableaus_mutex_);
  int64_t id = next_tableau_id_++;
  tableaus_[id] = std::make_unique<QutritTableau>(encode_312(state));
  return id;
}

std::vector<int32_t> QutritCliffordServiceImpl::ExtractSyndrome513(int64_t tableau_id) {
  auto* tab = GetTableau(tableau_id);
  if (!tab) return {};

  Code513 code;
  auto syn = code.extract_syndrome(*tab);
  return {syn[0], syn[1], syn[2], syn[3]};
}

std::pair<int32_t, int32_t> QutritCliffordServiceImpl::ExtractSyndrome312(int64_t tableau_id) {
  auto* tab = GetTableau(tableau_id);
  if (!tab) return {-1, -1};

  Code312 code;
  return code.extract_syndrome(*tab);
}

std::pair<int32_t, int32_t> QutritCliffordServiceImpl::IdentifyError513(const std::vector<int32_t>& syndrome) {
  if (syndrome.size() != 4) return {-1, -1};

  Code513 code;
  std::array<int, 4> syn = {syndrome[0], syndrome[1], syndrome[2], syndrome[3]};
  auto result = code.identify_error(syn);

  if (result.has_value()) {
    return {result->first, result->second};
  }
  return {-1, -1};
}

bool QutritCliffordServiceImpl::CorrectError513(int64_t tableau_id, int32_t qutrit, int32_t error_type) {
  auto* tab = GetTableau(tableau_id);
  if (!tab) return false;

  try {
    Code513 code;
    code.correct_error(*tab, qutrit, error_type);
    return true;
  } catch (...) {
    return false;
  }
}

bool QutritCliffordServiceImpl::DetectError312(int64_t tableau_id) {
  auto* tab = GetTableau(tableau_id);
  if (!tab) return false;

  return detect_error_312(*tab);
}

// ============================================================================
// Graph State Operations
// ============================================================================

int64_t QutritCliffordServiceImpl::CreateGraphState(
    int32_t n_qutrits, const std::vector<std::vector<int32_t>>& adjacency) {
  std::lock_guard<std::mutex> lock(graph_states_mutex_);
  int64_t id = next_graph_state_id_++;

  // Convert int32_t to int
  std::vector<std::vector<int>> adj_int;
  for (const auto& row : adjacency) {
    adj_int.emplace_back(row.begin(), row.end());
  }

  graph_states_[id] = std::make_unique<QutritGraphState>(n_qutrits, adj_int);
  return id;
}

bool QutritCliffordServiceImpl::InitializeGraphState(int64_t graph_state_id) {
  auto* gs = GetGraphState(graph_state_id);
  if (!gs) return false;
  try {
    gs->initialize();
    return true;
  } catch (...) {
    return false;
  }
}

bool QutritCliffordServiceImpl::EncodeFeatures(int64_t graph_state_id, const std::vector<int32_t>& features) {
  auto* gs = GetGraphState(graph_state_id);
  if (!gs) return false;
  try {
    std::vector<int> features_int(features.begin(), features.end());
    gs->encode_features(features_int);
    return true;
  } catch (...) {
    return false;
  }
}

std::vector<int32_t> QutritCliffordServiceImpl::ExtractFeatures(int64_t graph_state_id) {
  auto* gs = GetGraphState(graph_state_id);
  if (!gs) return {};

  auto features = gs->extract_features();
  return std::vector<int32_t>(features.begin(), features.end());
}

std::vector<std::string> QutritCliffordServiceImpl::GetStabilizerGenerators(int64_t graph_state_id) {
  auto* gs = GetGraphState(graph_state_id);
  if (!gs) return {};

  return gs->stabilizer_generators();
}

std::vector<std::vector<int32_t>> QutritCliffordServiceImpl::GenerateGraph(
    int32_t n, const std::string& graph_type, double edge_prob, uint32_t seed) {
  std::vector<std::vector<int>> adj;

  if (graph_type == "cyclic") {
    adj = cyclic_graph(n);
  } else if (graph_type == "complete") {
    adj = complete_graph(n);
  } else if (graph_type == "star") {
    adj = star_graph(n);
  } else if (graph_type == "random") {
    adj = random_graph(n, edge_prob, seed);
  } else {
    return {};
  }

  // Convert int to int32_t
  std::vector<std::vector<int32_t>> result;
  for (const auto& row : adj) {
    result.emplace_back(row.begin(), row.end());
  }
  return result;
}

std::vector<int32_t> QutritCliffordServiceImpl::GraphStateProjection(
    const std::vector<int32_t>& input_features, const std::string& graph_type, int32_t n_output) {
  std::vector<int> features_int(input_features.begin(), input_features.end());
  auto result = graph_state_projection(features_int, graph_type, n_output);
  return std::vector<int32_t>(result.begin(), result.end());
}

// ============================================================================
// Scrambling Operations
// ============================================================================

int64_t QutritCliffordServiceImpl::CreateScrambling(int32_t n_qutrits, uint64_t seed, int32_t depth) {
  std::lock_guard<std::mutex> lock(scramblings_mutex_);
  int64_t id = next_scrambling_id_++;
  scramblings_[id] = std::make_unique<CliffordScrambling>(n_qutrits, seed, depth);
  return id;
}

int64_t QutritCliffordServiceImpl::EncryptWeights(int64_t scrambling_id, const std::vector<int32_t>& weights) {
  auto* scrambler = GetScrambling(scrambling_id);
  if (!scrambler) return -1;

  std::vector<int> weights_int(weights.begin(), weights.end());
  auto tab = scrambler->encrypt_weights(weights_int);

  std::lock_guard<std::mutex> lock(tableaus_mutex_);
  int64_t id = next_tableau_id_++;
  tableaus_[id] = std::make_unique<QutritTableau>(std::move(tab));
  return id;
}

int64_t QutritCliffordServiceImpl::EncryptData(int64_t scrambling_id, const std::vector<int32_t>& data) {
  auto* scrambler = GetScrambling(scrambling_id);
  if (!scrambler) return -1;

  std::vector<int> data_int(data.begin(), data.end());
  auto tab = scrambler->encrypt_data(data_int);

  std::lock_guard<std::mutex> lock(tableaus_mutex_);
  int64_t id = next_tableau_id_++;
  tableaus_[id] = std::make_unique<QutritTableau>(std::move(tab));
  return id;
}

std::vector<int32_t> QutritCliffordServiceImpl::DecryptOutput(int64_t scrambling_id, int64_t tableau_id) {
  auto* scrambler = GetScrambling(scrambling_id);
  auto* tab = GetTableau(tableau_id);
  if (!scrambler || !tab) return {};

  auto result = scrambler->decrypt_output(*tab);
  return std::vector<int32_t>(result.begin(), result.end());
}

int64_t QutritCliffordServiceImpl::ScrambleWeights(
    const std::vector<int32_t>& weights, uint64_t seed, int32_t depth) {
  std::vector<int> weights_int(weights.begin(), weights.end());
  auto tab = scramble_weights(weights_int, seed, depth);

  std::lock_guard<std::mutex> lock(tableaus_mutex_);
  int64_t id = next_tableau_id_++;
  tableaus_[id] = std::make_unique<QutritTableau>(std::move(tab));
  return id;
}

std::vector<int32_t> QutritCliffordServiceImpl::ScrambleAndDecrypt(
    const std::vector<int32_t>& data, uint64_t seed, int32_t depth) {
  std::vector<int> data_int(data.begin(), data.end());
  auto result = scramble_and_decrypt(data_int, seed, depth);
  return std::vector<int32_t>(result.begin(), result.end());
}

// ============================================================================
// Attention Operations
// ============================================================================

int64_t QutritCliffordServiceImpl::CreateBellAttention(int32_t n_heads, int32_t n_features) {
  std::lock_guard<std::mutex> lock(attentions_mutex_);
  int64_t id = next_attention_id_++;
  attentions_[id] = std::make_unique<QutritBellAttention>(n_heads, n_features);
  return id;
}

int64_t QutritCliffordServiceImpl::EncodeKeyQuery(
    int64_t attention_id, const std::vector<int32_t>& keys, const std::vector<int32_t>& queries) {
  auto* attention = GetAttention(attention_id);
  if (!attention) return -1;

  std::vector<int> keys_int(keys.begin(), keys.end());
  std::vector<int> queries_int(queries.begin(), queries.end());
  auto tab = attention->encode_key_query(keys_int, queries_int);

  std::lock_guard<std::mutex> lock(tableaus_mutex_);
  int64_t id = next_tableau_id_++;
  tableaus_[id] = std::make_unique<QutritTableau>(std::move(tab));
  return id;
}

std::vector<float> QutritCliffordServiceImpl::BellMeasurement(int64_t attention_id, int64_t tableau_id) {
  auto* attention = GetAttention(attention_id);
  auto* tab = GetTableau(tableau_id);
  if (!attention || !tab) return {};

  return attention->bell_measurement(*tab);
}

std::vector<float> QutritCliffordServiceImpl::ComputeAttention(
    int64_t attention_id, const std::vector<int32_t>& keys, const std::vector<int32_t>& queries) {
  auto* attention = GetAttention(attention_id);
  if (!attention) return {};

  std::vector<int> keys_int(keys.begin(), keys.end());
  std::vector<int> queries_int(queries.begin(), queries.end());
  return attention->compute_attention(keys_int, queries_int);
}

std::vector<float> QutritCliffordServiceImpl::QutritAttention(
    const std::vector<int32_t>& keys, const std::vector<int32_t>& queries, int32_t n_heads) {
  std::vector<int> keys_int(keys.begin(), keys.end());
  std::vector<int> queries_int(queries.begin(), queries.end());
  return qutrit_attention(keys_int, queries_int, n_heads);
}

std::vector<float> QutritCliffordServiceImpl::SoftmaxApproximation(
    const std::vector<float>& scores, float temperature) {
  return softmax_approximation(scores, temperature);
}

std::vector<int32_t> QutritCliffordServiceImpl::TopKAttention(
    const std::vector<float>& scores, int32_t k) {
  auto result = top_k_attention(scores, k);
  return std::vector<int32_t>(result.begin(), result.end());
}

// ============================================================================
// Server Startup
// ============================================================================

void RunQutritCliffordServer(const std::string& address,
                              std::shared_ptr<QutritCliffordServiceImpl> service) {
  grpc::ServerBuilder builder;
  builder.AddListeningPort(address, grpc::InsecureServerCredentials());

  // Note: In a full implementation, you would register the generated service here
  // builder.RegisterService(service.get());

  std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
  std::cout << "QutritCliffordService listening on " << address << std::endl;
  server->Wait();
}

}  // namespace qminiwasm::quantum