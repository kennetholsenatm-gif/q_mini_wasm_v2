#pragma once

#include "qutrit_export.hpp"
#include "qutrit_tableau.hpp"

#include <memory>
#include <string>
#include <vector>

namespace qminiwasm::quantum {

/**
 * Qutrit graph state for deterministic feature projection.
 * 
 * Graph states provide high-dimensional, entangled feature hashing without
 * requiring learned weights or floating-point matrix multiplications.
 * 
 * A qutrit graph state |G⟩ is defined by a graph G = (V, E) where:
 * - Vertices V correspond to qutrits representing ternary features
 * - Edges E correspond to CZ₃ entanglement operations
 */
class QUTRIT_API QutritGraphState {
 public:
  /** Initialize qutrit graph state. */
  explicit QutritGraphState(int n_qutrits, const std::vector<std::vector<int>>& adjacency = {});

  /** Number of qutrits. */
  int num_qutrits() const { return n_; }

  /** Get the adjacency matrix. */
  const std::vector<std::vector<int>>& adjacency() const { return adjacency_; }

  /** Initialize the graph state in maximal superposition. */
  void initialize();

  /** Encode ternary input features via phase rotations. */
  void encode_features(const std::vector<int>& features);

  /** Extract output features via stabilizer generator measurements. */
  std::vector<int> extract_features() const;

  /** Compute the stabilizer generators K_i for the graph state. */
  std::vector<std::string> stabilizer_generators() const;

  /** Access the underlying tableau. */
  const QutritTableau& tableau() const { return tab_; }

 private:
  int n_;
  std::vector<std::vector<int>> adjacency_;  // GF(3) adjacency matrix
  QutritTableau tab_;
};

// Graph generators

/** Create a cyclic graph adjacency matrix over GF(3). */
std::vector<std::vector<int>> cyclic_graph(int n);

/** Create a complete graph adjacency matrix over GF(3). */
std::vector<std::vector<int>> complete_graph(int n);

/** Create a star graph adjacency matrix over GF(3). */
std::vector<std::vector<int>> star_graph(int n);

/** Create a random graph adjacency matrix over GF(3). */
std::vector<std::vector<int>> random_graph(int n, double edge_prob = 0.5, unsigned int seed = 42);

/** Project ternary features using a qutrit graph state. */
std::vector<int> graph_state_projection(
    const std::vector<int>& input_features,
    const std::string& graph_type = "cyclic",
    int n_output = -1);

}  // namespace qminiwasm::quantum