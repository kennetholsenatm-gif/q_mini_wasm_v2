#include "geometric_context.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace q_mini_wasm_v2::core::inference {

GeometricContextWindow::GeometricContextWindow(const ContextConfig& config)
    : config_(config) {}

GeometricContextWindow::~GeometricContextWindow() = default;

GeometricContextWindow::Multivector GeometricContextWindow::create_multivector(
    const std::vector<ternary::Trit>& trits
) {
    Multivector mv;
    size_t dim = trits.size();
    
    mv.scalar.resize(dim);
    mv.vector.resize(dim);
    mv.bivector.resize(dim * (dim - 1) / 2);
    
    for (size_t i = 0; i < dim; ++i) {
        double val = static_cast<double>(trits[i]);
        mv.scalar[i] = val;
        mv.vector[i] = val * 0.5;
    }
    
    size_t idx = 0;
    for (size_t i = 0; i < dim; ++i) {
        for (size_t j = i + 1; j < dim; ++j) {
            mv.bivector[idx++] = mv.vector[i] * mv.vector[j];
        }
    }
    
    normalize_multivector(mv);
    mv.norm = compute_norm(mv);
    return mv;
}

void GeometricContextWindow::normalize_multivector(Multivector& mv) {
    double norm = compute_norm(mv);
    if (norm < 1e-10) return;
    
    for (auto& v : mv.scalar) v /= norm;
    for (auto& v : mv.vector) v /= norm;
    for (auto& v : mv.bivector) v /= norm;
}

double GeometricContextWindow::compute_norm(const Multivector& mv) {
    double sum = 0.0;
    for (auto v : mv.scalar) sum += v * v;
    for (auto v : mv.vector) sum += v * v;
    for (auto v : mv.bivector) sum += v * v;
    return std::sqrt(sum);
}

GeometricContextWindow::GeometricState GeometricContextWindow::initialize_state(
    size_t sequence_length
) {
    GeometricState state;
    state.sequence.reserve(sequence_length);
    state.current_length = 0;
    state.total_geometric_norm = 0.0;
    return state;
}