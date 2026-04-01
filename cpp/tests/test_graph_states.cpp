#include "../quantum/graph_states.hpp"
#include "../quantum/qutrit_tableau.hpp"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

using namespace qminiwasm::quantum;

void test_cyclic_graph() {
  auto adj = cyclic_graph(3);
  assert(adj.size() == 3);
  assert(adj[0][1] == 1 && adj[1][0] == 1);
  assert(adj[1][2] == 1 && adj[2][1] == 1);
  assert(adj[2][0] == 1 && adj[0][2] == 1);
  assert(adj[0][0] == 0 && adj[1][1] == 0 && adj[2][2] == 0);
  std::cout << "✓ test_cyclic_graph passed" << std::endl;
}

void test_complete_graph() {
  auto adj = complete_graph(3);
  assert(adj.size() == 3);
  assert(adj[0][1] == 1 && adj[0][2] == 1);
  assert(adj[1][0] == 1 && adj[1][2] == 1);
  assert(adj[2][0] == 1 && adj[2][1] == 1);
  assert(adj[0][0] == 0 && adj[1][1] == 0 && adj[2][2] == 0);
  std::cout << "✓ test_complete_graph passed" << std::endl;
}

void test_star_graph() {
  auto adj = star_graph(4);
  assert(adj.size() == 4);
  assert(adj[0][1] == 1 && adj[0][2] == 1 && adj[0][3] == 1);
  assert(adj[1][0] == 1 && adj[2][0] == 1 && adj[3][0] == 1);
  assert(adj[1][2] == 0 && adj[1][3] == 0 && adj[2][3] == 0);
  std::cout << "✓ test_star_graph passed" << std::endl;
}

void test_graph_state_initialization() {
  auto adj = cyclic_graph(3);
  QutritGraphState gs(3, adj);

  gs.initialize();
  assert(gs.num_qutrits() == 3);

  auto generators = gs.stabilizer_generators();
  assert(generators.size() == 3);
  std::cout << "✓ test_graph_state_initialization passed" << std::endl;
  for (const auto& gen : generators) {
    std::cout << "  Generator: " << gen << std::endl;
  }
}

void test_feature_encoding() {
  auto adj = cyclic_graph(3);
  QutritGraphState gs(3, adj);
  gs.initialize();

  std::vector<int> features = {1, 0, -1};
  gs.encode_features(features);

  auto output = gs.extract_features();
  assert(output.size() == 3);
  for (int val : output) {
    assert(val >= 0 && val <= 2);
  }

  std::cout << "✓ test_feature_encoding passed" << std::endl;
  std::cout << "  Input: [1, 0, -1]" << std::endl;
  std::cout << "  Output: [" << output[0] << ", " << output[1] << ", " << output[2] << "]" << std::endl;
}

void test_graph_state_projection() {
  std::vector<int> features = {1, -1, 0, 1};

  auto output = graph_state_projection(features, "cyclic");
  assert(output.size() == 4);
  for (int val : output) {
    assert(val >= 0 && val <= 2);
  }

  std::cout << "✓ test_graph_state_projection passed" << std::endl;
  std::cout << "  Input: [1, -1, 0, 1]" << std::endl;
  std::cout << "  Output: [" << output[0] << ", " << output[1] << ", " << output[2] << ", " << output[3] << "]" << std::endl;
}

void test_different_graph_types() {
  std::vector<int> features = {1, 0, -1};

  auto cyclic_output = graph_state_projection(features, "cyclic");
  auto complete_output = graph_state_projection(features, "complete");
  auto star_output = graph_state_projection(features, "star");

  assert(cyclic_output.size() == 3);
  assert(complete_output.size() == 3);
  assert(star_output.size() == 3);

  std::cout << "✓ test_different_graph_types passed" << std::endl;
  std::cout << "  Cyclic: [" << cyclic_output[0] << ", " << cyclic_output[1] << ", " << cyclic_output[2] << "]" << std::endl;
  std::cout << "  Complete: [" << complete_output[0] << ", " << complete_output[1] << ", " << complete_output[2] << "]" << std::endl;
  std::cout << "  Star: [" << star_output[0] << ", " << star_output[1] << ", " << star_output[2] << "]" << std::endl;
}

void test_output_size_adjustment() {
  std::vector<int> features = {1, 0, -1, 1, 0};

  // Truncate
  auto truncated = graph_state_projection(features, "cyclic", 3);
  assert(truncated.size() == 3);

  // Pad
  auto padded = graph_state_projection(features, "cyclic", 7);
  assert(padded.size() == 7);
  assert(padded[5] == 0 && padded[6] == 0);

  std::cout << "✓ test_output_size_adjustment passed" << std::endl;
}

int main() {
  std::cout << "Running graph state tests..." << std::endl;

  test_cyclic_graph();
  test_complete_graph();
  test_star_graph();
  test_graph_state_initialization();
  test_feature_encoding();
  test_graph_state_projection();
  test_different_graph_types();
  test_output_size_adjustment();

  std::cout << "\nAll graph state tests passed!" << std::endl;
  return 0;
}