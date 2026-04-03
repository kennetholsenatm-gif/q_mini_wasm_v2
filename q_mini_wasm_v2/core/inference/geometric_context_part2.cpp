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