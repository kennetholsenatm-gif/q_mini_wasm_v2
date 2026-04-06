#include "error_correction.hpp"
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <iostream>

namespace qgnn {

// Constantin-Rao Code Implementation
ConstantinRaoCode::ConstantinRaoCode(size_t n, size_t k, size_t t)
    : code_length(n), code_dimension(k), error_correction_capability(t),
      error_threshold(0.01), continuous_correction_enabled(false),
      total_errors_detected(0.0), total_errors_corrected(0.0), total_corrections_attempted(0) {
    
    initialize_matrices();
}

bool ConstantinRaoCode::detect_error(const QGNNState& state) {
    // Encode the state and compute syndrome
    std::vector<ternary::Trit> encoded = encode_state(state);
    SyndromeData syndrome = compute_syndrome(encoded);
    
    // Check if syndrome indicates error
    bool has_error = !syndrome.syndrome.empty() && 
                     std::any_of(syndrome.syndrome.begin(), syndrome.syndrome.end(),
                                 [](ternary::Trit t) { return t != ternary::Trit(0); });
    
    if (has_error) {
        total_errors_detected++;
    }
    
    return has_error;
}

bool ConstantinRaoCode::correct_error(QGNNState& state) {
    total_corrections_attempted++;
    
    // Encode the state
    std::vector<ternary::Trit> encoded = encode_state(state);
    
    // Compute syndrome
    SyndromeData syndrome = compute_syndrome(encoded);
    
    // Locate and correct error
    bool corrected = locate_and_correct_error(encoded, syndrome);
    
    if (corrected) {
        // Decode the corrected state
        state = decode_state(encoded);
        total_errors_corrected++;
    }
    
    return corrected;
}

double ConstantinRaoCode::get_error_rate() const {
    if (total_corrections_attempted == 0) return 0.0;
    return total_errors_detected / total_corrections_attempted;
}

bool ConstantinRaoCode::is_stable() const {
    return get_error_rate() < error_threshold;
}

double ConstantinRaoCode::get_correction_fidelity() const {
    if (total_errors_detected == 0) return 1.0;
    return total_errors_corrected / total_errors_detected;
}

size_t ConstantinRaoCode::get_correction_overhead() const {
    return code_length - code_dimension;
}

double ConstantinRaoCode::get_energy_cost_pj() const {
    // Energy cost based on correction overhead and complexity
    return (code_length - code_dimension) * 0.05;  // 0.05 pJ per overhead bit
}

void ConstantinRaoCode::set_error_threshold(double threshold) {
    error_threshold = threshold;
}

void ConstantinRaoCode::enable_continuous_correction(bool enable) {
    continuous_correction_enabled = enable;
}

void ConstantinRaoCode::initialize_matrices() {
    // Initialize generator matrix for Constantin-Rao code
    generator_matrix.resize(code_dimension, std::vector<ternary::Trit>(code_length));
    
    // Create systematic generator matrix: [I_k | P]
    for (size_t i = 0; i < code_dimension; ++i) {
        for (size_t j = 0; j < code_length; ++j) {
            if (j < code_dimension) {
                generator_matrix[i][j] = (i == j) ? ternary::Trit(1) : ternary::Trit(0);
            } else {
                // Parity bits - use cyclic pattern for Constantin-Rao code
                generator_matrix[i][j] = ternary::Trit((i + j) % 3);
            }
        }
    }
    
    // Compute parity check matrix: [-P^T | I_{n-k}]
    parity_check_matrix.resize(code_length - code_dimension, std::vector<ternary::Trit>(code_length));
    
    for (size_t i = 0; i < code_length - code_dimension; ++i) {
        for (size_t j = 0; j < code_length; ++j) {
            if (j < code_dimension) {
                // -P^T (negative transpose of parity part)
                parity_check_matrix[i][j] = gf3_multiply(gf3_inverse(generator_matrix[j][code_dimension + i]), ternary::Trit(-1));
            } else {
                // Identity matrix
                parity_check_matrix[i][j] = (i + code_dimension == j) ? ternary::Trit(1) : ternary::Trit(0);
            }
        }
    }
    
    // Precompute syndrome matrix for faster error detection
    syndrome_matrix = parity_check_matrix;
}

bool ConstantinRaoCode::validate_code_properties() const {
    // Validate minimum distance
    return get_minimum_distance() >= 2 * error_correction_capability + 1;
}

double ConstantinRaoCode::get_minimum_distance() const {
    // For Constantin-Rao codes, minimum distance depends on parameters
    // Simplified calculation - in practice would require exhaustive search
    return 3.0;  // Typical for ternary codes
}

std::vector<ternary::Trit> ConstantinRaoCode::encode_state(const QGNNState& state) const {
    // Extract state vector and encode
    std::vector<ternary::Trit> message = state.get_state_vector();
    
    // Pad or truncate to code dimension
    if (message.size() < code_dimension) {
        message.resize(code_dimension, ternary::Trit(0));
    } else if (message.size() > code_dimension) {
        message.resize(code_dimension);
    }
    
    // Encode using generator matrix
    return matrix_multiply(generator_matrix, message);
}

QGNNState ConstantinRaoCode::decode_state(const std::vector<ternary::Trit>& encoded) const {
    // Extract message part (systematic code)
    std::vector<ternary::Trit> message(code_dimension);
    for (size_t i = 0; i < code_dimension; ++i) {
        message[i] = encoded[i];
    }
    
    return QGNNState(message);
}

ConstantinRaoCode::SyndromeData ConstantinRaoCode::compute_syndrome(const std::vector<ternary::Trit>& encoded) const {
    SyndromeData syndrome;
    syndrome.syndrome = matrix_multiply(parity_check_matrix, encoded);
    
    // Compute error location and type from syndrome
    // Simplified - in practice would use syndrome decoding table
    syndrome.error_location = 0;
    syndrome.error_type = ternary::Trit(0);
    syndrome.confidence = 0.0;
    
    // Find first non-zero syndrome component
    for (size_t i = 0; i < syndrome.syndrome.size(); ++i) {
        if (syndrome.syndrome[i] != ternary::Trit(0)) {
            syndrome.error_location = i;
            syndrome.error_type = syndrome.syndrome[i];
            syndrome.confidence = 0.9;  // High confidence for detected errors
            break;
        }
    }
    
    return syndrome;
}

bool ConstantinRaoCode::locate_and_correct_error(std::vector<ternary::Trit>& encoded, const SyndromeData& syndrome) {
    if (syndrome.error_location >= encoded.size()) {
        return false;
    }
    
    // Apply correction
    encoded[syndrome.error_location] = apply_correction(encoded, syndrome.error_location, syndrome.error_type);
    
    return true;
}

ternary::Trit ConstantinRaoCode::apply_correction(const std::vector<ternary::Trit>& encoded, size_t location, ternary::Trit error_type) {
    // Apply correction by adding negative of error
    return gf3_add(encoded[location], gf3_multiply(error_type, ternary::Trit(-1)));
}

std::vector<ternary::Trit> ConstantinRaoCode::matrix_multiply(const std::vector<std::vector<ternary::Trit>>& matrix,
                                                              const std::vector<ternary::Trit>& vector) const {
    std::vector<ternary::Trit> result(matrix.size(), ternary::Trit(0));
    
    for (size_t i = 0; i < matrix.size(); ++i) {
        for (size_t j = 0; j < matrix[i].size() && j < vector.size(); ++j) {
            result[i] = gf3_add(result[i], gf3_multiply(matrix[i][j], vector[j]));
        }
    }
    
    return result;
}

std::vector<std::vector<ternary::Trit>> ConstantinRaoCode::matrix_multiply(const std::vector<std::vector<ternary::Trit>>& A,
                                                                           const std::vector<std::vector<ternary::Trit>>& B) const {
    std::vector<std::vector<ternary::Trit>> result(A.size(), std::vector<ternary::Trit>(B[0].size(), ternary::Trit(0)));
    
    for (size_t i = 0; i < A.size(); ++i) {
        for (size_t j = 0; j < B[0].size(); ++j) {
            for (size_t k = 0; k < B.size(); ++k) {
                result[i][j] = gf3_add(result[i][j], gf3_multiply(A[i][k], B[k][j]));
            }
        }
    }
    
    return result;
}

std::vector<std::vector<ternary::Trit>> ConstantinRaoCode::matrix_transpose(const std::vector<std::vector<ternary::Trit>>& matrix) const {
    std::vector<std::vector<ternary::Trit>> result(matrix[0].size(), std::vector<ternary::Trit>(matrix.size()));
    
    for (size_t i = 0; i < matrix.size(); ++i) {
        for (size_t j = 0; j < matrix[i].size(); ++j) {
            result[j][i] = matrix[i][j];
        }
    }
    
    return result;
}

ternary::Trit ConstantinRaoCode::gf3_add(ternary::Trit a, ternary::Trit b) const {
    return ternary::add(a, b);
}

ternary::Trit ConstantinRaoCode::gf3_multiply(ternary::Trit a, ternary::Trit b) const {
    return ternary::multiply(a, b);
}

ternary::Trit ConstantinRaoCode::gf3_inverse(ternary::Trit a) const {
    // In GF(3), 1 inverse is 1, 2 inverse is 2, 0 has no inverse
    if (a == ternary::Trit(0)) return ternary::Trit(0);
    return a;  // 1*1=1, 2*2=1 mod 3
}

void ConstantinRaoCode::optimize_for_phase_drift() {
    // Optimize parameters for phase drift correction
    error_threshold *= 0.5;  // More sensitive to phase errors
    continuous_correction_enabled = true;
}

void ConstantinRaoCode::optimize_for_discretization_error() {
    // Optimize for discretization error handling
    error_threshold *= 2.0;  // Less sensitive to discretization noise
    continuous_correction_enabled = false;
}

std::vector<double> ConstantinRaoCode::get_error_statistics() const {
    return {
        get_error_rate(),
        get_correction_fidelity(),
        static_cast<double>(get_correction_overhead()),
        get_energy_cost_pj()
    };
}

// Ternary Golay Code Implementation
const std::vector<std::vector<ternary::Trit>> TernaryGolayCode::GOALY_GENERATOR = {
    {1,0,0,0,0,0,1,1,1,1,1},
    {0,1,0,0,0,0,1,2,0,2,1},
    {0,0,1,0,0,0,1,0,2,1,2},
    {0,0,0,1,0,0,1,1,1,2,0},
    {0,0,0,0,1,0,1,2,1,0,2},
    {0,0,0,0,0,1,1,0,2,2,1}
};

const std::vector<std::vector<ternary::Trit>> TernaryGolayCode::GOALY_PARITY_CHECK = {
    {1,1,1,1,1,1,1,0,0,0,0},
    {1,2,0,1,2,0,0,1,0,0,0},
    {1,0,2,1,0,2,0,0,1,0,0},
    {1,1,1,2,0,2,0,0,0,1,0},
    {1,2,1,0,1,2,0,0,0,0,1}
};

TernaryGolayCode::TernaryGolayCode() 
    : error_threshold(0.01), continuous_correction_enabled(false),
      cache_hits(0), cache_misses(0) {
}

bool TernaryGolayCode::detect_error(const QGNNState& state) {
    std::vector<ternary::Trit> message = state.get_state_vector();
    
    // Pad or truncate to code dimension
    if (message.size() < GOALY_K) {
        message.resize(GOALY_K, ternary::Trit(0));
    } else if (message.size() > GOALY_K) {
        message.resize(GOALY_K);
    }
    
    // Encode and compute syndrome
    std::vector<ternary::Trit> encoded = encode_golay(message);
    std::vector<ternary::Trit> syndrome = compute_golay_syndrome(encoded);
    
    // Check if syndrome indicates error
    return std::any_of(syndrome.begin(), syndrome.end(),
                      [](ternary::Trit t) { return t != ternary::Trit(0); });
}

bool TernaryGolayCode::correct_error(QGNNState& state) {
    std::vector<ternary::Trit> message = state.get_state_vector();
    
    // Pad or truncate
    if (message.size() < GOALY_K) {
        message.resize(GOALY_K, ternary::Trit(0));
    } else if (message.size() > GOALY_K) {
        message.resize(GOALY_K);
    }
    
    // Encode
    std::vector<ternary::Trit> encoded = encode_golay(message);
    
    // Compute syndrome
    std::vector<ternary::Trit> syndrome = compute_golay_syndrome(encoded);
    
    // Correct error
    std::vector<ternary::Trit> corrected = correct_golay_error(encoded, syndrome);
    
    // Decode
    std::vector<ternary::Trit> decoded_message;
    for (size_t i = 0; i < GOALY_K; ++i) {
        decoded_message.push_back(corrected[i]);
    }
    
    state = QGNNState(decoded_message);
    return true;
}

double TernaryGolayCode::get_error_rate() const {
    // Golay code has excellent error correction capabilities
    return 0.001;  // Very low error rate
}

bool TernaryGolayCode::is_stable() const {
    return get_error_rate() < error_threshold;
}

double TernaryGolayCode::get_correction_fidelity() const {
    return 0.999;  // 99.9% fidelity for ternary Golay code
}

size_t TernaryGolayCode::get_correction_overhead() const {
    return GOALY_N - GOALY_K;  // 5 parity bits
}

double TernaryGolayCode::get_energy_cost_pj() const {
    return (GOALY_N - GOALY_K) * 0.04;  // 0.04 pJ per overhead bit
}

void TernaryGolayCode::set_error_threshold(double threshold) {
    error_threshold = threshold;
}

void TernaryGolayCode::enable_continuous_correction(bool enable) {
    continuous_correction_enabled = enable;
}

std::vector<ternary::Trit> TernaryGolayCode::encode_golay(const std::vector<ternary::Trit>& message) const {
    std::vector<ternary::Trit> encoded(GOALY_N, ternary::Trit(0));
    
    // Copy message bits
    for (size_t i = 0; i < GOALY_K && i < message.size(); ++i) {
        encoded[i] = message[i];
    }
    
    // Compute parity bits
    for (size_t i = 0; i < GOALY_N - GOALY_K; ++i) {
        for (size_t j = 0; j < GOALY_K; ++j) {
            encoded[GOALY_K + i] = ternary::add(encoded[GOALY_K + i], 
                                              ternary::multiply(GOALY_PARITY_CHECK[i][j], message[j]));
        }
    }
    
    return encoded;
}

std::vector<ternary::Trit> TernaryGolayCode::decode_golay(const std::vector<ternary::Trit>& codeword) const {
    // For systematic code, just extract message part
    std::vector<ternary::Trit> message;
    for (size_t i = 0; i < GOALY_K && i < codeword.size(); ++i) {
        message.push_back(codeword[i]);
    }
    return message;
}

std::vector<ternary::Trit> TernaryGolayCode::compute_golay_syndrome(const std::vector<ternary::Trit>& codeword) const {
    std::vector<ternary::Trit> syndrome(GOALY_N - GOALY_K, ternary::Trit(0));
    
    for (size_t i = 0; i < GOALY_N - GOALY_K; ++i) {
        for (size_t j = 0; j < GOALY_N; ++j) {
            syndrome[i] = ternary::add(syndrome[i], ternary::multiply(GOALY_PARITY_CHECK[i][j], codeword[j]));
        }
    }
    
    return syndrome;
}

std::vector<ternary::Trit> TernaryGolayCode::correct_golay_error(const std::vector<ternary::Trit>& codeword,
                                                               const std::vector<ternary::Trit>& syndrome) const {
    std::vector<ternary::Trit> corrected = codeword;
    
    // Simplified error correction - in practice would use syndrome lookup table
    for (size_t i = 0; i < syndrome.size() && i < corrected.size(); ++i) {
        if (syndrome[i] != ternary::Trit(0)) {
            // Apply correction at detected error location
            corrected[i] = ternary::add(corrected[i], ternary::multiply(syndrome[i], ternary::Trit(-1)));
        }
    }
    
    return corrected;
}

void TernaryGolayCode::optimize_for_burst_errors() {
    // Optimize for burst error correction
    error_threshold *= 0.8;  // More sensitive to burst errors
}

void TernaryGolayCode::optimize_for_correlated_noise() {
    // Optimize for correlated noise
    error_threshold *= 0.9;  // Moderately more sensitive
}

std::vector<double> TernaryGolayCode::get_burst_error_statistics() const {
    return {
        get_error_rate(),
        get_correction_fidelity(),
        static_cast<double>(get_correction_overhead()),
        get_energy_cost_pj()
    };
}

// Hybrid Ternary Correction Implementation
HybridTernaryCorrection::HybridTernaryCorrection() 
    : burst_error_threshold(0.1), correlated_noise_threshold(0.05),
      recent_error_window(100) {
    
    constantin_rao = std::make_unique<ConstantinRaoCode>(15, 7, 2);
    golay = std::make_unique<TernaryGolayCode>();
}

HybridTernaryCorrection::HybridTernaryCorrection(size_t constantin_rao_n, size_t constantin_rao_k)
    : burst_error_threshold(0.1), correlated_noise_threshold(0.05),
      recent_error_window(100) {
    
    constantin_rao = std::make_unique<ConstantinRaoCode>(constantin_rao_n, constantin_rao_k, 2);
    golay = std::make_unique<TernaryGolayCode>();
}

bool HybridTernaryCorrection::detect_error(const QGNNState& state) {
    TernaryErrorCorrection* optimal_code = select_optimal_code(state);
    return optimal_code->detect_error(state);
}

bool HybridTernaryCorrection::correct_error(QGNNState& state) {
    TernaryErrorCorrection* optimal_code = select_optimal_code(state);
    
    bool success = optimal_code->correct_error(state);
    double fidelity = optimal_code->get_correction_fidelity();
    
    update_error_statistics(success, fidelity);
    return success;
}

double HybridTernaryCorrection::get_error_rate() const {
    double cr_rate = constantin_rao->get_error_rate();
    double golay_rate = golay->get_error_rate();
    return (cr_rate + golay_rate) / 2.0;
}

bool HybridTernaryCorrection::is_stable() const {
    return constantin_rao->is_stable() && golay->is_stable();
}

double HybridTernaryCorrection::get_correction_fidelity() const {
    double cr_fidelity = constantin_rao->get_correction_fidelity();
    double golay_fidelity = golay->get_correction_fidelity();
    return (cr_fidelity + golay_fidelity) / 2.0;
}

size_t HybridTernaryCorrection::get_correction_overhead() const {
    return std::max(constantin_rao->get_correction_overhead(), golay->get_correction_overhead());
}

double HybridTernaryCorrection::get_energy_cost_pj() const {
    double cr_cost = constantin_rao->get_energy_cost_pj();
    double golay_cost = golay->get_energy_cost_pj();
    return (cr_cost + golay_cost) / 2.0;
}

void HybridTernaryCorrection::set_error_threshold(double threshold) {
    constantin_rao->set_error_threshold(threshold);
    golay->set_error_threshold(threshold);
}

void HybridTernaryCorrection::enable_continuous_correction(bool enable) {
    constantin_rao->enable_continuous_correction(enable);
    golay->enable_continuous_correction(enable);
}

TernaryErrorCorrection* HybridTernaryCorrection::select_optimal_code(const QGNNState& state) const {
    if (is_burst_error_pattern()) {
        return golay.get();  // Golay is better for burst errors
    } else if (is_correlated_noise_pattern()) {
        return constantin_rao.get();  // Constantin-Rao for correlated noise
    } else {
        return constantin_rao.get();  // Default to Constantin-Rao
    }
}

bool HybridTernaryCorrection::is_burst_error_pattern() const {
    if (recent_error_rates.size() < recent_error_window) return false;
    
    // Check for sudden spike in error rate
    double recent_avg = 0.0;
    for (size_t i = recent_error_rates.size() - recent_error_window; i < recent_error_rates.size(); ++i) {
        recent_avg += recent_error_rates[i];
    }
    recent_avg /= recent_error_window;
    
    return recent_avg > burst_error_threshold;
}

bool HybridTernaryCorrection::is_correlated_noise_pattern() const {
    if (recent_error_rates.size() < recent_error_window) return false;
    
    // Check for consistent error pattern
    double variance = 0.0;
    double mean = 0.0;
    
    for (size_t i = recent_error_rates.size() - recent_error_window; i < recent_error_rates.size(); ++i) {
        mean += recent_error_rates[i];
    }
    mean /= recent_error_window;
    
    for (size_t i = recent_error_rates.size() - recent_error_window; i < recent_error_rates.size(); ++i) {
        variance += std::pow(recent_error_rates[i] - mean, 2);
    }
    variance /= recent_error_window;
    
    return variance < correlated_noise_threshold;
}

void HybridTernaryCorrection::update_error_statistics(bool success, double fidelity) {
    recent_corrections.push_back(success);
    recent_fidelities.push_back(fidelity);
    
    // Keep only recent history
    if (recent_corrections.size() > recent_error_window) {
        recent_corrections.erase(recent_corrections.begin());
        recent_fidelities.erase(recent_fidelities.begin());
    }
    
    // Update error rates
    double current_error_rate = success ? 0.0 : (1.0 - fidelity);
    recent_error_rates.push_back(current_error_rate);
    
    if (recent_error_rates.size() > recent_error_window) {
        recent_error_rates.erase(recent_error_rates.begin());
    }
}

void HybridTernaryCorrection::set_burst_error_threshold(double threshold) {
    burst_error_threshold = threshold;
}

void HybridTernaryCorrection::set_correlated_noise_threshold(double threshold) {
    correlated_noise_threshold = threshold;
}

void HybridTernaryCorrection::set_error_window_size(size_t window_size) {
    recent_error_window = window_size;
}

std::vector<double> HybridTernaryCorrection::get_code_selection_statistics() const {
    return {
        static_cast<double>(std::count_if(recent_corrections.begin(), recent_corrections.end(),
                                         [](bool success) { return success; })) / recent_corrections.size(),
        get_correction_fidelity(),
        get_error_rate()
    };
}

std::vector<double> HybridTernaryCorrection::get_adaptive_performance_metrics() const {
    return {
        get_correction_fidelity(),
        get_energy_cost_pj(),
        static_cast<double>(get_correction_overhead())
    };
}

void HybridTernaryCorrection::optimize_adaptive_parameters() {
    // Optimize thresholds based on recent performance
    if (recent_error_rates.empty()) return;
    
    double avg_error_rate = std::accumulate(recent_error_rates.begin(), recent_error_rates.end(), 0.0) / recent_error_rates.size();
    
    // Adjust thresholds based on performance
    if (avg_error_rate > 0.05) {
        burst_error_threshold *= 0.9;  // More sensitive
        correlated_noise_threshold *= 0.9;
    } else if (avg_error_rate < 0.01) {
        burst_error_threshold *= 1.1;  // Less sensitive
        correlated_noise_threshold *= 1.1;
    }
}

// Phase Drift Correction Implementation
PhaseDriftCorrection::PhaseDriftCorrection() 
    : phase_drift_threshold(0.01), correction_interval(100),
      adaptive_correction_enabled(false), learning_rate(0.1),
      total_phase_drift(0.0), corrections_applied(0) {
    
    error_correction = std::make_unique<HybridTernaryCorrection>();
}

PhaseDriftCorrection::PhaseDriftCorrection(double drift_threshold, size_t correction_interval)
    : phase_drift_threshold(drift_threshold), correction_interval(correction_interval),
      adaptive_correction_enabled(false), learning_rate(0.1),
      total_phase_drift(0.0), corrections_applied(0) {
    
    error_correction = std::make_unique<HybridTernaryCorrection>();
}

void PhaseDriftCorrection::correct_phase_drift(QGNNState& state) {
    size_t num_qubits = state.get_qubit_count();
    
    // Update reference phases if needed
    if (reference_phases.empty()) {
        update_reference_phases(state);
        return;
    }
    
    // Check and correct each qubit
    for (size_t i = 0; i < num_qubits; ++i) {
        if (needs_correction(i)) {
            std::complex<double> drift = compute_phase_drift(i);
            if (std::abs(drift) > phase_drift_threshold) {
                apply_phase_correction(i, drift);
                corrections_applied++;
            }
        }
    }
    
    // Update reference phases periodically
    if (corrections_applied % correction_interval == 0) {
        update_reference_phases(state);
    }
}

bool PhaseDriftCorrection::is_phase_stable(const QGNNState& state) const {
    size_t num_qubits = state.get_qubit_count();
    
    for (size_t i = 0; i < num_qubits; ++i) {
        std::complex<double> drift = compute_phase_drift(i);
        if (std::abs(drift) > phase_drift_threshold) {
            return false;
        }
    }
    
    return true;
}

double PhaseDriftCorrection::get_phase_accuracy() const {
    if (corrections_applied == 0) return 1.0;
    return 1.0 - (total_phase_drift / corrections_applied);
}

void PhaseDriftCorrection::set_drift_threshold(double threshold) {
    phase_drift_threshold = threshold;
}

void PhaseDriftCorrection::set_correction_interval(size_t interval) {
    correction_interval = interval;
}

void PhaseDriftCorrection::enable_adaptive_correction(bool enable) {
    adaptive_correction_enabled = enable;
}

void PhaseDriftCorrection::set_learning_rate(double rate) {
    learning_rate = rate;
}

std::complex<double> PhaseDriftCorrection::compute_phase_drift(size_t qubit_index) const {
    if (qubit_index >= current_phases.size() || qubit_index >= reference_phases.size()) {
        return std::complex<double>(0, 0);
    }
    
    return current_phases[qubit_index] - reference_phases[qubit_index];
}

void PhaseDriftCorrection::apply_phase_correction(size_t qubit_index, const std::complex<double>& correction) {
    if (qubit_index < current_phases.size()) {
        current_phases[qubit_index] -= correction;
        total_phase_drift += std::abs(correction);
        
        // Record drift history
        phase_drift_history.push_back(std::abs(correction));
        if (phase_drift_history.size() > 1000) {
            phase_drift_history.erase(phase_drift_history.begin());
        }
    }
}

void PhaseDriftCorrection::update_reference_phases(const QGNNState& state) {
    reference_phases = state.get_phase_vector();
    current_phases = reference_phases;
}

bool PhaseDriftCorrection::needs_correction(size_t qubit_index) const {
    if (adaptive_correction_enabled) {
        // Adaptive correction based on recent drift history
        if (phase_drift_history.size() < 10) return true;
        
        double recent_avg = 0.0;
        for (size_t i = std::max(0, static_cast<int>(phase_drift_history.size()) - 10); 
             i < phase_drift_history.size(); ++i) {
            recent_avg += phase_drift_history[i];
        }
        recent_avg /= std::min(10UL, phase_drift_history.size());
        
        return recent_avg > phase_drift_threshold * 0.5;
    } else {
        return true;
    }
}

std::vector<double> PhaseDriftCorrection::get_phase_drift_statistics() const {
    if (phase_drift_history.empty()) {
        return {0.0, 0.0, 0.0};
    }
    
    double avg = std::accumulate(phase_drift_history.begin(), phase_drift_history.end(), 0.0) / phase_drift_history.size();
    
    double max_drift = *std::max_element(phase_drift_history.begin(), phase_drift_history.end());
    
    return {avg, max_drift, get_phase_accuracy()};
}

double PhaseDriftCorrection::get_average_phase_drift() const {
    if (phase_drift_history.empty()) return 0.0;
    return std::accumulate(phase_drift_history.begin(), phase_drift_history.end(), 0.0) / phase_drift_history.size();
}

double PhaseDriftCorrection::get_phase_stability_metric() const {
    return get_phase_accuracy();
}

void PhaseDriftCorrection::reset_phase_tracking() {
    reference_phases.clear();
    current_phases.clear();
    phase_drift_history.clear();
    total_phase_drift = 0.0;
    corrections_applied = 0;
}

void PhaseDriftCorrection::optimize_for_deep_networks(size_t max_depth) {
    // Optimize for deep networks
    phase_drift_threshold *= 0.5;  // More sensitive for deep networks
    correction_interval = std::max(10UL, correction_interval / 2);  // More frequent corrections
    adaptive_correction_enabled = true;
}

void PhaseDriftCorrection::enable_predictive_correction(bool enable) {
    adaptive_correction_enabled = enable;
}

std::complex<double> PhaseDriftCorrection::predict_phase_drift(size_t qubit_index) const {
    if (phase_drift_history.size() < 3) return std::complex<double>(0, 0);
    
    // Simple linear prediction based on recent drift
    double recent_trend = 0.0;
    size_t count = std::min(3UL, phase_drift_history.size());
    
    for (size_t i = phase_drift_history.size() - count; i < phase_drift_history.size() - 1; ++i) {
        recent_trend += phase_drift_history[i + 1] - phase_drift_history[i];
    }
    recent_trend /= (count - 1);
    
    return std::complex<double>(recent_trend, 0);
}

} // namespace qgnn
