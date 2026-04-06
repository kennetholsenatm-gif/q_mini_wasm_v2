#include "graph_native.hpp"
#include <algorithm>

namespace q_mini_wasm_v2::core::qgnn {

std::unique_ptr<QGNNGraph> create_qgnn_graph() {
    return std::make_unique<QGNNGraph>();
}

} // namespace q_mini_wasm_v2::core::qgnn
