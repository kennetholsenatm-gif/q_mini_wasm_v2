#include "tableau_kernels.hpp"
#include <algorithm>
#include <cstdint>
#include <memory>
#include <mutex>
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

namespace {

cl::sycl::queue& default_sycl_queue_instance() {
    static std::mutex mutex;
    static std::unique_ptr<cl::sycl::queue> queue;
    std::lock_guard<std::mutex> lock(mutex);
    if (!queue) {
        queue = std::make_unique<cl::sycl::queue>(cl::sycl::default_selector{});
    }
    return *queue;
}

} // namespace

/** Polynomial TritPack5: trit @p tri_in_row within a row starting at byte @p row_byte_base in @p acc. */
template<typename Acc>
inline int8_t read_trit_t5_poly_bw(const Acc& acc, size_t row_byte_base, size_t tri_in_row) {
    const size_t bi = tri_in_row / 5u;
    const unsigned lane = static_cast<unsigned>(tri_in_row % 5u);
    uint8_t x = acc[row_byte_base + bi];
    for (unsigned k = 0; k < lane; ++k) {
        x = static_cast<uint8_t>(x / 3u);
    }
    return static_cast<int8_t>(static_cast<int>(x % 3u) - 1);
}

void gf3_uint8_mul_batch_sycl(cl::sycl::queue& q, uint8_t* result, const uint8_t* a, const uint8_t* b, size_t count) {
    if (count == 0 || result == nullptr || a == nullptr || b == nullptr) {
        return;
    }
    q.parallel_for(cl::sycl::range<1>(count), [=](cl::sycl::id<1> idx) {
        const size_t i = idx[0];
        const int av = static_cast<int>(a[i] % 3u);
        const int bv = static_cast<int>(b[i] % 3u);
        result[i] = static_cast<uint8_t>((av * bv) % 3);
    }).wait();
}

void gf3_uint8_add_batch_sycl(cl::sycl::queue& q, uint8_t* result, const uint8_t* a, const uint8_t* b, size_t count) {
    if (count == 0 || result == nullptr || a == nullptr || b == nullptr) {
        return;
    }
    q.parallel_for(cl::sycl::range<1>(count), [=](cl::sycl::id<1> idx) {
        const size_t i = idx[0];
        const int av = static_cast<int>(a[i] % 3u);
        const int bv = static_cast<int>(b[i] % 3u);
        result[i] = static_cast<uint8_t>((av + bv) % 3);
    }).wait();
}

void wasm_tableau_hadamard_sycl(cl::sycl::queue& q, uint8_t* tableau, size_t num_qutrits, size_t target) {
    if (tableau == nullptr || num_qutrits == 0 || target >= num_qutrits) {
        return;
    }
    const size_t stride = 2 * num_qutrits;
    const size_t nrows = num_qutrits;
    q.parallel_for(cl::sycl::range<1>(nrows), [=](cl::sycl::id<1> idx) {
        const size_t row = idx[0];
        const size_t xi = row * stride + target;
        const size_t zi = row * stride + num_qutrits + target;
        const uint8_t t = tableau[xi];
        tableau[xi] = tableau[zi];
        tableau[zi] = t;
    }).wait();
}

void wasm_tableau_phase_sycl(cl::sycl::queue& q, uint8_t* tableau, size_t num_qutrits, size_t target) {
    if (tableau == nullptr || num_qutrits == 0 || target >= num_qutrits) {
        return;
    }
    const size_t stride = 2 * num_qutrits;
    const size_t nrows = num_qutrits;
    q.parallel_for(cl::sycl::range<1>(nrows), [=](cl::sycl::id<1> idx) {
        const size_t row = idx[0];
        const size_t z_idx = row * stride + num_qutrits + target;
        const size_t x_idx = row * stride + target;
        tableau[z_idx] = static_cast<uint8_t>(
            (static_cast<unsigned>(tableau[z_idx]) + static_cast<unsigned>(tableau[x_idx])) % 3u);
    }).wait();
}

void wasm_tableau_csum_sycl(cl::sycl::queue& q, uint8_t* tableau, size_t num_qutrits, size_t control, size_t target) {
    if (tableau == nullptr || num_qutrits == 0 || control >= num_qutrits || target >= num_qutrits
        || control == target) {
        return;
    }
    const size_t stride = 2 * num_qutrits;
    const size_t nrows = 2 * num_qutrits;
    q.parallel_for(cl::sycl::range<1>(nrows), [=](cl::sycl::id<1> idx) {
        const size_t row = idx[0];
        const size_t xt_idx = row * stride + target;
        const size_t xc_idx = row * stride + control;
        tableau[xt_idx] = static_cast<uint8_t>(
            (static_cast<unsigned>(tableau[xt_idx]) + static_cast<unsigned>(tableau[xc_idx])) % 3u);

        const size_t zc_idx = row * stride + num_qutrits + control;
        const size_t zt_idx = row * stride + num_qutrits + target;
        const unsigned zt = static_cast<unsigned>(tableau[zt_idx]) % 3u;
        const unsigned add = (2u * zt) % 3u;
        tableau[zc_idx] = static_cast<uint8_t>((static_cast<unsigned>(tableau[zc_idx]) + add) % 3u);
    }).wait();
}

std::vector<int8_t> quantize_float_buffer_to_trits_sycl(const float* src, size_t src_len, size_t out_dim) {
    if (out_dim == 0) {
        return {};
    }
    std::vector<int8_t> out(out_dim, static_cast<int8_t>(0));
    if (src == nullptr || src_len == 0) {
        return out;
    }
    cl::sycl::queue& q = default_sycl_queue_instance();
    cl::sycl::buffer<float, 1> buf_in(src, cl::sycl::range<1>(src_len));
    cl::sycl::buffer<int8_t, 1> buf_out(out.data(), cl::sycl::range<1>(out_dim));
    constexpr float kPos = 0.33f;
    constexpr float kNeg = -0.33f;
    q.submit([&](cl::sycl::handler& h) {
        auto acc_in = buf_in.get_access<cl::sycl::access::mode::read>(h);
        auto acc_out = buf_out.get_access<cl::sycl::access::mode::write>(h);
        h.parallel_for(cl::sycl::range<1>(out_dim), [=](cl::sycl::id<1> id) {
            const size_t i = id[0];
            if (i >= src_len) {
                acc_out[i] = 0;
                return;
            }
            const float v = acc_in[i];
            if (v > kPos) {
                acc_out[i] = 1;
            } else if (v < kNeg) {
                acc_out[i] = -1;
            } else {
                acc_out[i] = 0;
            }
        });
    }).wait();
    return out;
}

std::vector<int8_t> quantize_i32_buffer_to_trits_sycl(const int32_t* src, size_t src_len, size_t out_dim) {
    if (out_dim == 0) {
        return {};
    }
    std::vector<int8_t> out(out_dim, static_cast<int8_t>(0));
    if (src == nullptr || src_len == 0) {
        return out;
    }
    cl::sycl::queue& q = default_sycl_queue_instance();
    cl::sycl::buffer<int32_t, 1> buf_in(src, cl::sycl::range<1>(src_len));
    cl::sycl::buffer<int8_t, 1> buf_out(out.data(), cl::sycl::range<1>(out_dim));
    q.submit([&](cl::sycl::handler& h) {
        auto acc_in = buf_in.get_access<cl::sycl::access::mode::read>(h);
        auto acc_out = buf_out.get_access<cl::sycl::access::mode::write>(h);
        h.parallel_for(cl::sycl::range<1>(out_dim), [=](cl::sycl::id<1> id) {
            const size_t i = id[0];
            if (i >= src_len) {
                acc_out[i] = 0;
                return;
            }
            int32_t v = acc_in[i];
            int im = static_cast<int>(v % 3);
            if (im < 0) {
                im += 3;
            }
            acc_out[i] = static_cast<int8_t>(im - 1);
        });
    }).wait();
    return out;
}

std::vector<int8_t> quantize_string_bytes_to_trits_sycl(const char* src, size_t slen, size_t out_dim) {
    if (out_dim == 0) {
        return {};
    }
    std::vector<int8_t> out(out_dim, static_cast<int8_t>(0));
    if (src == nullptr || slen == 0) {
        return out;
    }
    cl::sycl::queue& q = default_sycl_queue_instance();
    cl::sycl::buffer<char, 1> buf_in(src, cl::sycl::range<1>(slen));
    cl::sycl::buffer<int8_t, 1> buf_out(out.data(), cl::sycl::range<1>(out_dim));
    q.submit([&](cl::sycl::handler& h) {
        auto acc_in = buf_in.get_access<cl::sycl::access::mode::read>(h);
        auto acc_out = buf_out.get_access<cl::sycl::access::mode::write>(h);
        h.parallel_for(cl::sycl::range<1>(out_dim), [=](cl::sycl::id<1> id) {
            const size_t i = id[0];
            const char c = acc_in[i % slen];
            const int ci = static_cast<int>(c);
            acc_out[i] = static_cast<int8_t>((ci % 3) - 1);
        });
    }).wait();
    return out;
}

std::vector<int8_t> moe_routing_symplectic_scores_sycl(
    const std::vector<uint8_t>& input_tritpack5,
    size_t input_trit_count,
    const std::vector<uint8_t>& weights_tritpack5,
    size_t total_experts,
    size_t routing_qutrits
) {
    if (total_experts == 0 || routing_qutrits == 0 || input_trit_count == 0) {
        return {};
    }

    const size_t in_bytes = (input_trit_count + 4) / 5;
    if (input_tritpack5.size() < in_bytes) {
        return {};
    }

    const size_t E = total_experts;
    const size_t R = routing_qutrits;
    const size_t w_row_bytes = (R + 4) / 5;
    if (weights_tritpack5.size() < E * w_row_bytes) {
        return {};
    }

    cl::sycl::queue& q = default_sycl_queue_instance();

    std::vector<int8_t> scores(total_experts, static_cast<int8_t>(-1));

    std::vector<uint8_t> in_copy(input_tritpack5.begin(),
                                 input_tritpack5.begin() + static_cast<std::ptrdiff_t>(in_bytes));
    std::vector<uint8_t> w_copy(weights_tritpack5.begin(),
                                weights_tritpack5.begin() + static_cast<std::ptrdiff_t>(E * w_row_bytes));

    cl::sycl::buffer<uint8_t, 1> buf_in(in_copy.data(), cl::sycl::range<1>(in_copy.size()));
    cl::sycl::buffer<uint8_t, 1> buf_w(w_copy.data(), cl::sycl::range<1>(w_copy.size()));
    cl::sycl::buffer<int8_t, 1> buf_out(scores.data(), cl::sycl::range<1>(scores.size()));

    q.submit([&](cl::sycl::handler& h) {
        auto acc_in = buf_in.get_access<cl::sycl::access::mode::read>(h);
        auto acc_w = buf_w.get_access<cl::sycl::access::mode::read>(h);
        auto acc_out = buf_out.get_access<cl::sycl::access::mode::write>(h);

        h.parallel_for(cl::sycl::range<1>(E), [=](cl::sycl::id<1> id) {
            const size_t e = id[0];
            const size_t min_size = input_trit_count < R ? input_trit_count : R;
            int8_t symplectic_sum = 0;

            for (size_t i = 0; i < min_size; i += 2) {
                const int8_t x1 = read_trit_t5_poly_bw(acc_in, size_t{0}, i);
                const int8_t z1 =
                    (i + 1 < min_size) ? read_trit_t5_poly_bw(acc_in, size_t{0}, i + 1u) : static_cast<int8_t>(0);

                const size_t w_row_bytes_l = (R + 4u) / 5u;
                const size_t w_base = e * w_row_bytes_l;
                const int8_t x2 = read_trit_t5_poly_bw(acc_w, w_base, i);
                const int8_t z2 = (i + 1 < min_size) ? read_trit_t5_poly_bw(acc_w, w_base, i + 1u) : static_cast<int8_t>(0);

                int pairing = static_cast<int>(x1) * static_cast<int>(z2) - static_cast<int>(z1) * static_cast<int>(x2);
                while (pairing > 1) {
                    pairing -= 3;
                }
                while (pairing < -1) {
                    pairing += 3;
                }

                int sum = static_cast<int>(symplectic_sum) + pairing;
                while (sum > 1) {
                    sum -= 3;
                }
                while (sum < -1) {
                    sum += 3;
                }
                symplectic_sum = static_cast<int8_t>(sum);
            }

            acc_out[e] = symplectic_sum;
        });
    }).wait();

    return scores;
}

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