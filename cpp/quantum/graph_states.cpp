#include "graph_states.hpp"

#include <algorithm>
#include <random>
#include <stdexcept>

namespace qminiwasm::quantum {

QutritGraphState::QutritGraphState(int n_qutrits, const std::vector<std::vector<int>>& adjacency)
    : n_(n_qutrits), tab_(n_qutrits) {
  if (n_qutrits < 1) {
    throw std::invalid_argument("n_qutrits must be >= 1");
  }

  if (adjacency.empty()) {
    // Default: no edges
    adjacency_.resize(n_, std::vector<int>(n_, 0));
  } else {
    if (static_cast<int>(adjacency.size()) != n_) {
      throw std::invalid_argument("Adjacency matrix size must match n_qutrits");
    }
    for (const auto& row : adjacency) {
      if (static_cast<int>(row.size()) != n_) {
        throw std::invalid_argument("Adjacency matrix must be square");
      }
    }
    // Copy and mod 3
    adjacency_.resize(n_, std::vector<int>(n_, 0));
    for (int i = 0; i < n_; ++i) {
      for (int j = 0; j < n_; ++j) {
        adjacency_[i][j] = ((adjacency[i][j] % 3) + 3) % 3;
      }
    }
  }
}

void QutritGraphState::initialize() {
  tab_ = QutritTableau(n_);

  // Apply transversal Hadamard to create maximal superposition
  for (int i = 0; i < n_; ++i) {
    tab_.apply_h3(i);
  }

  // Apply CZ₃ entanglement according to adjacency matrix
  // Only upper triangle to avoid double-application
  for (int i = 0; i < n_; ++i) {
    for (int j = i + 1; j < n_; ++j) {
      int weight = adjacency_[i][j];
      if (weight != 0) {
        // Apply CZ₃^weight
        for (int k = 0; k < weight; ++k) {
          tab_.apply_cz3(i, j);
        }
      }
    }
  }
}

void QutritGraphState::encode_features(const std::vector<int>& features) {
  if (static_cast<int>(features.size()) != n_) {
    throw std::invalid_argument("Features size must match n_qutrits");
  }

  for (int i = 0; i < n_; ++i) {
    // Map {-1, 0, 1} to {2, 0, 1} for GF(3)
    int gf3_val = ((features[i] + 3) % 3);
    if (gf3_val == 1) {
      tab_.apply_s3(i);
    } else if (gf3_val == 2) {
      tab_.apply_s3(i);
      tab_.apply_s3(i);  // S₃²
    }
  }
}

std::vector<int> QutritGraphState::extract_features() const {
  std::vector<int> features;
  features.reserve(n_);

  const auto& r = tab_.r();
  for (int i = 0; i < n_; ++i) {
    // Measure stabilizer generator K_i
    // Expectation value is in {1, ω, ω²} → mapped to {0, 1, 2}
    features.push_back(r[i] % 3);
  }

  return features;
}

std::vector<std::string> QutritGraphState::stabilizer_generators() const {
  std::vector<std::string> generators;

  for (int i = 0; i < n_; ++i) {
    std::string parts;
    // X on vertex i
    parts += "X" + std::to_string(i);

    // Z on neighbors
    for (int j = 0; j < n_; ++j) {
      if (i != j) {
        int weight = adjacency_[i][j];
        if (weight == 1) {
          parts += " Z" + std::to_string(j);
        } else if (weight == 2) {
          parts += " Z²" + std::to_string(j);
        }
      }
    }

    generators.push_back(parts);
  }

  return generators;
}

// ============================================================================
// Graph Generators
// ============================================================================

std::vector<std::vector<int>> cyclic_graph(int n) {
  std::vector<std::vector<int>> adj(n, std::vector<int>(n, 0));
  for (int i = 0; i < n; ++i) {
    int j = (i + 1) % n;
    adj[i][j] = 1;
    adj[j][i] = 1;
  }
  return adj;
}

std::vector<std::vector<int>> complete_graph(int n) {
  std::vector<std::vector<int>> adj(n, std::vector<int>(n, 0));
  for (int i = 0; i < n; ++i) {
    for (int j = 0; j < n; ++j) {
      if (i != j) {
        adj[i][j] = 1;
      }
    }
  }
  return adj;
}

std::vector<std::vector<int>> star_graph(int n) {
  std::vector<std::vector<int>> adj(n, std::vector<int>(n, 0));
  for (int i = 1; i < n; ++i) {
    adj[0][i] = 1;
    adj[i][0] = 1;
  }
  return adj;
}

std::vector<std::vector<int>> random_graph(int n, double edge_prob, unsigned int seed) {
  std::mt19937 rng(seed);
  std::uniform_real_distribution<double> dist(0.0, 1.0);
  std::uniform_int_distribution<int> weight_dist(1, 2);

  std::vector<std::vector<int>> adj(n, std::vector<int>(n, 0));

  for (int i = 0; i < n; ++i) {
    for (int j = i + 1; j < n; ++j) {
      if (dist(rng) < edge_prob) {
        int weight = weight_dist(rng);
        adj[i][j] = weight;
        adj[j][i] = weight;
      }
    }
  }

  return adj;
}

std::vector<int> graph_state_projection(
    const std::vector<int>& input_features,
    const std::string& graph_type,
    int n_output) {
  int n_input = static_cast<int>(input_features.size());
  int n_out = (n_output < 0) ? n_input : n_output;

  // Create graph
  std::vector<std::vector<int>> adj;
  if (graph_type == "cyclic") {
    adj = cyclic_graph(n_input);
  } else if (graph_type == "complete") {
    adj = complete_graph(n_input);
  } else if (graph_type == "star") {
    adj = star_graph(n_input);
  } else if (graph_type == "random") {
    adj = random_graph(n_input);
  } else {
    throw std::invalid_argument("Unknown graph type: " + graph_type);
  }

  // Create and initialize graph state
  QutritGraphState gs(n_input, adj);
  gs.initialize();

  // Encode input features
  gs.encode_features(input_features);

  // Extract output features
  auto output = gs.extract_features();

  // Truncate or pad to desired output size
  if (n_out < n_input) {
    output.resize(n_out);
  } else if (n_out > n_input) {
    output.resize(n_out, 0);
  }

  return output;
}

}  // namespace qminiwasm::quantum