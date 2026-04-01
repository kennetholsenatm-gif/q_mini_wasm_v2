#pragma once

#include "qutrit_export.hpp"
#include "qutrit_tableau.hpp"
#include "error_correction.hpp"
#include "graph_states.hpp"
#include "scrambling.hpp"
#include "attention.hpp"

#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>
#include <grpcpp/ext/proto_server_reflection_plugin.h>

#include <memory>
#include <mutex>
#include <unordered_map>

// Forward declarations for generated proto code
namespace qminiwasm {
namespace quantum {
class QutritCliffordService;
}
}

namespace qminiwasm::quantum {

/**
 * gRPC service implementation for qutrit Clifford operations.
 * 
 * Provides remote access to qutrit stabilizer tableau operations,
 * error correction, graph states, scrambling, and attention mechanisms.
 * All operations stay within Gottesman-Knill simulability bounds.
 */
class QUTRIT_API QutritCliffordServiceImpl final {
 public:
  QutritCliffordServiceImpl();
  ~QutritCliffordServiceImpl();

  // Qutrit Tableau Operations
  int64_t CreateTableau(int32_t n_qutrits);
  bool DestroyTableau(int64_t tableau_id);
  int64_t CopyTableau(int64_t source_id);
  bool ApplyH3(int64_t tableau_id, int32_t qubit);
  bool ApplyS3(int64_t tableau_id, int32_t qubit);
  bool ApplyCZ3(int64_t tableau_id, int32_t control, int32_t target);
  bool ApplyX3(int64_t tableau_id, int32_t qubit, int32_t power);
  bool ApplyZ3(int64_t tableau_id, int32_t qubit, int32_t power);
  int32_t MeasureTableau(int64_t tableau_id, int32_t qubit);
  std::string GetStabilizerString(int64_t tableau_id, int32_t row);

  // Error Correction Operations
  int64_t Encode513(int32_t state);
  int64_t Encode312(int32_t state);
  std::vector<int32_t> ExtractSyndrome513(int64_t tableau_id);
  std::pair<int32_t, int32_t> ExtractSyndrome312(int64_t tableau_id);
  std::pair<int32_t, int32_t> IdentifyError513(const std::vector<int32_t>& syndrome);
  bool CorrectError513(int64_t tableau_id, int32_t qutrit, int32_t error_type);
  bool DetectError312(int64_t tableau_id);

  // Graph State Operations
  int64_t CreateGraphState(int32_t n_qutrits, const std::vector<std::vector<int32_t>>& adjacency);
  bool InitializeGraphState(int64_t graph_state_id);
  bool EncodeFeatures(int64_t graph_state_id, const std::vector<int32_t>& features);
  std::vector<int32_t> ExtractFeatures(int64_t graph_state_id);
  std::vector<std::string> GetStabilizerGenerators(int64_t graph_state_id);
  std::vector<std::vector<int32_t>> GenerateGraph(int32_t n, const std::string& graph_type, 
                                                    double edge_prob, uint32_t seed);
  std::vector<int32_t> GraphStateProjection(const std::vector<int32_t>& input_features,
                                             const std::string& graph_type, int32_t n_output);

  // Scrambling Operations
  int64_t CreateScrambling(int32_t n_qutrits, uint64_t seed, int32_t depth);
  int64_t EncryptWeights(int64_t scrambling_id, const std::vector<int32_t>& weights);
  int64_t EncryptData(int64_t scrambling_id, const std::vector<int32_t>& data);
  std::vector<int32_t> DecryptOutput(int64_t scrambling_id, int64_t tableau_id);
  int64_t ScrambleWeights(const std::vector<int32_t>& weights, uint64_t seed, int32_t depth);
  std::vector<int32_t> ScrambleAndDecrypt(const std::vector<int32_t>& data, uint64_t seed, int32_t depth);

  // Attention Operations
  int64_t CreateBellAttention(int32_t n_heads, int32_t n_features);
  int64_t EncodeKeyQuery(int64_t attention_id, const std::vector<int32_t>& keys, 
                          const std::vector<int32_t>& queries);
  std::vector<float> BellMeasurement(int64_t attention_id, int64_t tableau_id);
  std::vector<float> ComputeAttention(int64_t attention_id, const std::vector<int32_t>& keys,
                                       const std::vector<int32_t>& queries);
  std::vector<float> QutritAttention(const std::vector<int32_t>& keys, 
                                      const std::vector<int32_t>& queries, int32_t n_heads);
  std::vector<float> SoftmaxApproximation(const std::vector<float>& scores, float temperature);
  std::vector<int32_t> TopKAttention(const std::vector<float>& scores, int32_t k);

 private:
  // Tableau storage (ID -> unique_ptr)
  std::mutex tableaus_mutex_;
  std::unordered_map<int64_t, std::unique_ptr<QutritTableau>> tableaus_;
  int64_t next_tableau_id_ = 1;

  // Graph state storage
  std::mutex graph_states_mutex_;
  std::unordered_map<int64_t, std::unique_ptr<QutritGraphState>> graph_states_;
  int64_t next_graph_state_id_ = 1;

  // Scrambling storage
  std::mutex scramblings_mutex_;
  std::unordered_map<int64_t, std::unique_ptr<CliffordScrambling>> scramblings_;
  int64_t next_scrambling_id_ = 1;

  // Attention storage
  std::mutex attentions_mutex_;
  std::unordered_map<int64_t, std::unique_ptr<QutritBellAttention>> attentions_;
  int64_t next_attention_id_ = 1;

  // Helper to get tableau by ID
  QutritTableau* GetTableau(int64_t id);
  QutritGraphState* GetGraphState(int64_t id);
  CliffordScrambling* GetScrambling(int64_t id);
  QutritBellAttention* GetAttention(int64_t id);
};

/**
 * Create and run the gRPC server for qutrit operations.
 * @param address Server address (e.g., "0.0.0.0:50052")
 * @param service Shared pointer to the service implementation
 */
QUTRIT_API void RunQutritCliffordServer(const std::string& address,
                                         std::shared_ptr<QutritCliffordServiceImpl> service);

}  // namespace qminiwasm::quantum