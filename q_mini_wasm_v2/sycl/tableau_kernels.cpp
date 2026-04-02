#include "tableau_kernels.hpp"
#include <algorithm>
#include <thread>
#include <vector>

namespace q_mini_wasm_v2::sycl_kernels {

// ============================================================================
// Parallel Tableau Operations (CPU fallback without SYCL)
// ============================================================================

void parallel_apply_hadamard(
    std::vector<int8_t>& tableau_data,
    std::vector<int8_t>& phase_data,
    size_t n,
    size_t target_qutrit
) {
    size_t dim = 2 * n;
    
    // Parallel update across rows
    std::vector<std::thread> threads;
    size_t num_threads = std::thread::hardware_concurrency();
    if (num_threads == 0) num_threads = 4;
    
    size_t rows_per_thread = dim / num_threads;
    
    for (size_t t = 0; t < num_threads; ++t) {
        size_t start_row = t * rows_per_thread;
        size_t end_row = (t == num_threads - 1) ? dim : (t + 1) * rows_per_thread;
        
        threads.emplace_back([&tableau_data, &phase_data, n, dim, target_qutrit, start_row, end_row]() {
            for (size_t i = start_row; i < end_row; ++i) {
                // Swap X and Z blocks for target column
                size_t x_idx = i * dim + target_qutrit + n;
                size_t z_idx = i * dim + target_qutrit;
                
                int8_t temp = tableau_data[z_idx];
                tableau_data[z_idx] = (3 - tableau_data[x_idx]) % 3;
                tableau_data[x_idx] = temp;
                
                // Phase correction
                if (tableau_data[z_idx] != 0 && tableau_data[x_idx] != 0) {
                    phase_data[i] = (phase_data[i] + tableau_data[z_idx] * tableau_data[x_idx]) % 3;
                }
            }
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
}

void parallel_apply_phase(
    std::vector<int8_t>& tableau_data,
    std::vector<int8_t>& phase_data,
    size_t n,
    size_t target_qutrit
) {
    size_t dim = 2 * n;
    
    std::vector<std::thread> threads;
    size_t num_threads = std::thread::hardware_concurrency();
    if (num_threads == 0) num_threads = 4;
    
    size_t rows_per_thread = dim / num_threads;
    
    for (size_t t = 0; t < num_threads; ++t) {
        size_t start_row = t * rows_per_thread;
        size_t end_row = (t == num_threads - 1) ? dim : (t + 1) * rows_per_thread;
        
        threads.emplace_back([&tableau_data, &phase_data, n, dim, target_qutrit, start_row, end_row]() {
            for (size_t i = start_row; i < end_row; ++i) {
                size_t x_idx = i * dim + target_qutrit + n;
                size_t z_idx = i * dim + target_qutrit;
                
                int8_t x_ij = tableau_data[x_idx];
                int8_t z_ij = tableau_data[z_idx];
                
                // Z = (Z + X) mod 3
                tableau_data[z_idx] = (z_ij + x_ij) % 3;
                
                // Phase correction
                phase_data[i] = (phase_data[i] + x_ij * z_ij) % 3;
            }
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
}

void parallel_apply_csum(
    std::vector<int8_t>& tableau_data,
    std::vector<int8_t>& phase_data,
    size_t n,
    size_t control,
    size_t target
) {
    size_t dim = 2 * n;
    
    std::vector<std::thread> threads;
    size_t num_threads = std::thread::hardware_concurrency();
    if (num_threads == 0) num_threads = 4;
    
    size_t rows_per_thread = dim / num_threads;
    
    for (size_t t = 0; t < num_threads; ++t) {
        size_t start_row = t * rows_per_thread;
        size_t end_row = (t == num_threads - 1) ? dim : (t + 1) * rows_per_thread;
        
        threads.emplace_back([&tableau_data, &phase_data, n, dim, control, target, start_row, end_row]() {
            for (size_t i = start_row; i < end_row; ++i) {
                // Z[target] = (Z[target] + Z[control]) mod 3
                size_t z_target = i * dim + target;
                size_t z_control = i * dim + control;
                tableau_data[z_target] = (tableau_data[z_target] + tableau_data[z_control]) % 3;
                
                // X[control] = (X[control] - X[target]) mod 3
                size_t x_control = i * dim + control + n;
                size_t x_target = i * dim + target + n;
                int8_t diff = tableau_data[x_control] - tableau_data[x_target];
                if (diff < 0) diff += 3;
                tableau_data[x_control] = diff % 3;
                
                // Phase correction
                phase_data[i] = (phase_data[i] + tableau_data[z_control] * tableau_data[x_target]) % 3;
            }
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
}

std::vector<int8_t> parallel_measure_all(
    std::vector<int8_t>& tableau_data,
    std::vector<int8_t>& phase_data,
    size_t n
) {
    // Simplified measurement - returns phase values
    std::vector<int8_t> outcomes(n);
    for (size_t i = 0; i < n; ++i) {
        outcomes[i] = phase_data[i] % 3;
    }
    return outcomes;
}

std::vector<double> parallel_compute_routing_logits(
    const std::vector<int8_t>& routing_weights,
    const std::vector<int8_t>& input_features,
    size_t num_experts
) {
    std::vector<double> logits(num_experts, 0.0);
    
    size_t feature_size = input_features.size();
    
    std::vector<std::thread> threads;
    size_t num_threads = std::thread::hardware_concurrency();
    if (num_threads == 0) num_threads = 4;
    
    size_t experts_per_thread = num_experts / num_threads;
    
    for (size_t t = 0; t < num_threads; ++t) {
        size_t start_expert = t * experts_per_thread;
        size_t end_expert = (t == num_threads - 1) ? num_experts : (t + 1) * experts_per_thread;
        
        threads.emplace_back([&logits, &routing_weights, &input_features, feature_size, start_expert, end_expert]() {
            for (size_t e = start_expert; e < end_expert; ++e) {
                // Tropical inner product: max_i(w_i + x_i)
                double max_val = -1e9;
                for (size_t i = 0; i < feature_size; ++i) {
                    double val = static_cast<double>(routing_weights[e * feature_size + i]) +
                                static_cast<double>(input_features[i]);
                    max_val = std::max(max_val, val);
                }
                logits[e] = max_val;
            }
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    return logits;
}

void parallel_forward_layer(
    const std::vector<int8_t>& weights,
    const std::vector<int8_t>& biases,
    const std::vector<int8_t>& input,
    std::vector<int8_t>& output
) {
    size_t output_size = output.size();
    size_t input_size = input.size();
    
    std::vector<std::thread> threads;
    size_t num_threads = std::thread::hardware_concurrency();
    if (num_threads == 0) num_threads = 4;
    
    size_t neurons_per_thread = output_size / num_threads;
    
    for (size_t t = 0; t < num_threads; ++t) {
        size_t start_neuron = t * neurons_per_thread;
        size_t end_neuron = (t == num_threads - 1) ? output_size : (t + 1) * neurons_per_thread;
        
        threads.emplace_back([&output, &weights, &biases, &input, input_size, start_neuron, end_neuron]() {
            for (size_t o = start_neuron; o < end_neuron; ++o) {
                int sum = static_cast<int>(biases[o]);
                
                for (size_t i = 0; i < input_size; ++i) {
                    // Ternary multiplication
                    int product = static_cast<int>(input[i]) * static_cast<int>(weights[o * input_size + i]);
                    sum += product;
                }
                
                // Ternary activation
                if (sum > 0) {
                    output[o] = 1;
                } else if (sum < 0) {
                    output[o] = -1;
                } else {
                    output[o] = 0;
                }
            }
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
}

void parallel_mod3_arithmetic(
    const std::vector<int8_t>& a,
    const std::vector<int8_t>& b,
    std::vector<int8_t>& result,
    int operation
) {
    size_t size = a.size();
    result.resize(size);
    
    std::vector<std::thread> threads;
    size_t num_threads = std::thread::hardware_concurrency();
    if (num_threads == 0) num_threads = 4;
    
    size_t elements_per_thread = size / num_threads;
    
    for (size_t t = 0; t < num_threads; ++t) {
        size_t start = t * elements_per_thread;
        size_t end = (t == num_threads - 1) ? size : (t + 1) * elements_per_thread;
        
        threads.emplace_back([&result, &a, &b, operation, start, end]() {
            for (size_t i = start; i < end; ++i) {
                switch (operation) {
                    case 0:  // Add
                        result[i] = (a[i] + b[i]) % 3;
                        break;
                    case 1:  // Subtract
                        result[i] = (a[i] - b[i] + 3) % 3;
                        break;
                    case 2:  // Multiply
                        result[i] = (a[i] * b[i]) % 3;
                        break;
                    default:
                        result[i] = 0;
                }
            }
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
}

#ifdef USE_SYCL

SYCLQueue::SYCLQueue()
    : queue_(cl::sycl::default_selector{})
{
}

SYCLQueue::~SYCLQueue() {
    queue_.wait();
}

void SYCLQueue::submit_tableau_update(
    cl::sycl::buffer<int8_t, 2>& tableau_buf,
    cl::sycl::buffer<int8_t, 1>& phase_buf,
    size_t target_qutrit
) {
    queue_.submit([&](cl::sycl::handler& h) {
        auto tableau_acc = tableau_buf.get_access<cl::sycl::access::mode::read_write>(h);
        auto phase_acc = phase_buf.get_access<cl::sycl::access::mode::read_write>(h);
        
        size_t n = tableau_buf.get_range()[0] / 2;
        
        h.parallel_for(cl::sycl::range<1>(2 * n), [=](cl::sycl::id<1> idx) {
            size_t i = idx[0];
            size_t dim = 2 * n;
            
            // Hadamard operation
            int8_t temp = tableau_acc[i][target_qutrit];
            tableau_acc[i][target_qutrit] = (3 - tableau_acc[i][target_qutrit + n]) % 3;
            tableau_acc[i][target_qutrit + n] = temp;
            
            // Phase correction
            if (tableau_acc[i][target_qutrit] != 0 && tableau_acc[i][target_qutrit + n] != 0) {
                phase_acc[i] = (phase_acc[i] + tableau_acc[i][target_qutrit] * tableau_acc[i][target_qutrit + n]) % 3;
            }
        });
    });
}

void SYCLQueue::wait() {
    queue_.wait();
}

#endif // USE_SYCL

} // namespace q_mini_wasm_v2::sycl_kernels