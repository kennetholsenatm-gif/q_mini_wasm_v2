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

GeometricContextWindow::Multivector GeometricContextWindow::geometric_product(
    const Multivector& a,
    const Multivector& b
) {
    Multivector result;
    size_t dim = a.scalar.size();
    
    result.scalar.resize(dim);
    result.vector.resize(dim);
    result.bivector.resize(dim * (dim - 1) / 2);
    
    for (size_t i = 0; i < dim; ++i) {
        result.scalar[i] = a.scalar[i] * b.scalar[i] + a.vector[i] * b.vector[i];
        result.vector[i] = a.scalar[i] * b.vector[i] + a.vector[i] * b.scalar[i];
    }
    
    size_t idx = 0;
    for (size_t i = 0; i < dim; ++i) {
        for (size_t j = i + 1; j < dim; ++j) {
            result.bivector[idx] = a.vector[i] * b.vector[j] - a.vector[j] * b.vector[i];
            ++idx;
        }
    }
    
    normalize_multivector(result);
    result.norm = compute_norm(result);
    return result;
}

GeometricContextWindow::Multivector GeometricContextWindow::inner_product(
    const Multivector& a,
    const Multivector& b
) {
    Multivector result;
    size_t dim = a.scalar.size();
    
    result.scalar.resize(dim);
    result.vector.resize(dim, 0.0);
    result.bivector.resize(dim * (dim - 1) / 2, 0.0);
    
    for (size_t i = 0; i < dim; ++i) {
        result.scalar[i] = a.scalar[i] * b.scalar[i] + a.vector[i] * b.vector[i];
    }
    
    result.norm = compute_norm(result);
    return result;
}

GeometricContextWindow::Multivector GeometricContextWindow::wedge_product(
    const Multivector& a,
    const Multivector& b
) {
    Multivector result;
    size_t dim = a.scalar.size();
    
    result.scalar.resize(dim, 0.0);
    result.vector.resize(dim, 0.0);
    result.bivector.resize(dim * (dim - 1) / 2);
    
    size_t idx = 0;
    for (size_t i = 0; i < dim; ++i) {
        for (size_t j = i + 1; j < dim; ++j) {
            result.bivector[idx] = a.vector[i] * b.vector[j] - a.vector[j] * b.vector[i];
            ++idx;
        }
    }
    
    result.norm = compute_norm(result);
    return result;
}

GeometricContextWindow::GeometricState GeometricContextWindow::update_context(
    const GeometricState& state,
    const Multivector& new_token
) {
    GeometricState result = state;
    
    if (result.current_length >= config_.max_context_length) {
        result.sequence.erase(result.sequence.begin());
        result.current_length--;
    }
    
    Multivector context_token = new_token;
    
    if (!result.sequence.empty()) {
        const auto& last = result.sequence.back();
        context_token = geometric_product(last, new_token);
    }
    
    if (config_.enable_manifold_normalization) {
        normalize_multivector(context_token);
    }
    
    result.sequence.push_back(context_token);
    result.current_length++;
    result.total_geometric_norm += context_token.norm;
    
    return result;
}

GeometricContextWindow::GeometricState GeometricContextWindow::apply_manifold_normalization(
    const GeometricState& state
) {
    GeometricState result = state;
    
    for (auto& mv : result.sequence) {
        normalize_multivector(mv);
        mv.norm = compute_norm(mv);
    }
    
    result.total_geometric_norm = 0.0;
    for (const auto& mv : result.sequence) {
        result.total_geometric_norm += mv.norm;
    }
    
    return result;
}

double GeometricContextWindow::compute_context_similarity(
    const GeometricState& state,
    size_t i,
    size_t j
) {
    if (i >= state.sequence.size() || j >= state.sequence.size()) {
        return 0.0;
    }
    
    const auto& a = state.sequence[i];
    const auto& b = state.sequence[j];
    
    double similarity = 0.0;
    size_t count = 0;
    
    for (size_t k = 0; k < std::min(a.scalar.size(), b.scalar.size()); ++k) {
        similarity += a.scalar[k] * b.scalar[k];
        count++;
    }
    
    for (size_t k = 0; k < std::min(a.vector.size(), b.vector.size()); ++k) {
        similarity += a.vector[k] * b.vector[k];
        count++;
    }
    
    return count > 0 ? similarity / count : 0.0;
}

std::vector<double> GeometricContextWindow::extract_context_vector(
    const GeometricState& state,
    size_t position
) {
    if (position >= state.sequence.size()) {
        return {};
    }
    
    const auto& mv = state.sequence[position];
    std::vector<double> context;
    context.reserve(mv.scalar.size() + mv.vector.size());
    
    context.insert(context.end(), mv.scalar.begin(), mv.scalar.end());
    context.insert(context.end(), mv.vector.begin(), mv.vector.end());
    
    return context;
}

std::vector<GeometricContextWindow::Multivector> GeometricContextWindow::tokens_to_multivectors(
    const std::vector<std::vector<ternary::Trit>>& tokens
) {
    std::vector<Multivector> multivectors;
    multivectors.reserve(tokens.size());
    
    for (const auto& token : tokens) {
        multivectors.push_back(create_multivector(token));
    }
    
    return multivectors;
}

std::unique_ptr<GeometricContextWindow> create_geometric_context(
    const GeometricContextWindow::ContextConfig& config
) {
    return std::make_unique<GeometricContextWindow>(config);
}

} // namespace q_mini_wasm_v2::core::inference