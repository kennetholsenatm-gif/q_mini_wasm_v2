#include "tableau_kernels.hpp"
#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <mutex>
#include <thread>
#include <vector>

#if defined(USE_SYCL) && USE_SYCL
#include "gf3_layers_sycl.hpp"
#endif

namespace q_mini_wasm_v2::sycl_kernels {

// ============================================================================
// Parallel tableau (host threads — not SYCL device kernels; WASM / legacy callers)
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

sycl::queue& default_sycl_queue_instance() {
    static std::mutex mutex;
    static std::unique_ptr<sycl::queue> queue;
    std::lock_guard<std::mutex> lock(mutex);
    if (!queue) {
        queue = std::make_unique<sycl::queue>(sycl::default_selector_v);
    }
    return *queue;
}

std::mutex g_sycl_reserve_mutex;
std::vector<uint8_t*> g_sycl_reserved_ptrs;
size_t g_sycl_reserved_total = 0;

} // namespace

size_t reserve_sycl_device_memory_bytes(
    size_t target_bytes,
    size_t chunk_bytes,
    bool touch_pages,
    std::string* note
) {
    std::lock_guard<std::mutex> lock(g_sycl_reserve_mutex);
    // Same queue/device/context as GF3 FF + MoE SYCL kernels (`gf3_layers_target_device`). Using
    // default_selector here reserved ~30GiB on the wrong adapter — VRAM/UMA budget never fed training.
    sycl::queue& q = gf3_layers_queue_for_thread();

    for (auto* ptr : g_sycl_reserved_ptrs) {
        if (ptr != nullptr) {
            try {
                sycl::free(ptr, q);
            } catch (...) {
                // Best-effort cleanup only.
            }
        }
    }
    g_sycl_reserved_ptrs.clear();
    g_sycl_reserved_total = 0;

    if (target_bytes == 0) {
        if (note) {
            *note = "target_bytes=0";
        }
        return 0;
    }

    const size_t chunk = std::max<size_t>(size_t{1}, chunk_bytes);
    size_t remaining = target_bytes;
    size_t chunks_ok = 0;
    std::string fail_reason;
    while (remaining > 0) {
        const size_t ask = std::min(remaining, chunk);
        uint8_t* ptr = nullptr;
        try {
            ptr = sycl::malloc_device<uint8_t>(ask, q);
        } catch (const std::exception& ex) {
            fail_reason = ex.what();
            break;
        } catch (...) {
            fail_reason = "unknown_exception";
            break;
        }
        if (ptr == nullptr) {
            fail_reason = "nullptr";
            break;
        }
        if (touch_pages) {
            try {
                q.memset(ptr, 0, ask).wait();
            } catch (...) {
                try {
                    sycl::free(ptr, q);
                } catch (...) {
                    // Best-effort cleanup only.
                }
                fail_reason = "memset_failed";
                break;
            }
        }
        g_sycl_reserved_ptrs.push_back(ptr);
        g_sycl_reserved_total += ask;
        remaining -= ask;
        ++chunks_ok;
    }

    if (note) {
        *note = "requested=" + std::to_string(target_bytes) +
                " reserved=" + std::to_string(g_sycl_reserved_total) +
                " chunk=" + std::to_string(chunk) +
                " chunks_ok=" + std::to_string(chunks_ok) +
                (fail_reason.empty() ? "" : " fail=" + fail_reason);
    }
    return g_sycl_reserved_total;
}

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

/** TritPack5 polynomial byte (matches @c q::ternary::pack_5trits) for device quantize kernels. */
static inline uint8_t dev_pack_5trits_tb(const int8_t* t5) {
    uint32_t acc = 0;
    static constexpr uint32_t POW3[5] = {1u, 3u, 9u, 27u, 81u};
    for (int i = 0; i < 5; ++i) {
        const uint32_t u = static_cast<uint32_t>(static_cast<int>(t5[i]) + 1);
        acc += u * POW3[static_cast<size_t>(i)];
    }
    return static_cast<uint8_t>(acc);
}

void gf3_uint8_mul_batch_sycl(sycl::queue& q, uint8_t* result, const uint8_t* a, const uint8_t* b, size_t count) {
    if (count == 0 || result == nullptr || a == nullptr || b == nullptr) {
        return;
    }
    q.parallel_for(sycl::range<1>(count), [=](sycl::id<1> idx) {
        const size_t i = idx[0];
        const int av = static_cast<int>(a[i] % 3u);
        const int bv = static_cast<int>(b[i] % 3u);
        result[i] = static_cast<uint8_t>((av * bv) % 3);
    }).wait();
}

void gf3_uint8_add_batch_sycl(sycl::queue& q, uint8_t* result, const uint8_t* a, const uint8_t* b, size_t count) {
    if (count == 0 || result == nullptr || a == nullptr || b == nullptr) {
        return;
    }
    q.parallel_for(sycl::range<1>(count), [=](sycl::id<1> idx) {
        const size_t i = idx[0];
        const int av = static_cast<int>(a[i] % 3u);
        const int bv = static_cast<int>(b[i] % 3u);
        result[i] = static_cast<uint8_t>((av + bv) % 3);
    }).wait();
}

void wasm_tableau_hadamard_sycl(sycl::queue& q, uint8_t* tableau, size_t num_qutrits, size_t target) {
    if (tableau == nullptr || num_qutrits == 0 || target >= num_qutrits) {
        return;
    }
    const size_t stride = 2 * num_qutrits;
    const size_t nrows = num_qutrits;
    q.parallel_for(sycl::range<1>(nrows), [=](sycl::id<1> idx) {
        const size_t row = idx[0];
        const size_t xi = row * stride + target;
        const size_t zi = row * stride + num_qutrits + target;
        const uint8_t t = tableau[xi];
        tableau[xi] = tableau[zi];
        tableau[zi] = t;
    }).wait();
}

void wasm_tableau_phase_sycl(sycl::queue& q, uint8_t* tableau, size_t num_qutrits, size_t target) {
    if (tableau == nullptr || num_qutrits == 0 || target >= num_qutrits) {
        return;
    }
    const size_t stride = 2 * num_qutrits;
    const size_t nrows = num_qutrits;
    q.parallel_for(sycl::range<1>(nrows), [=](sycl::id<1> idx) {
        const size_t row = idx[0];
        const size_t z_idx = row * stride + num_qutrits + target;
        const size_t x_idx = row * stride + target;
        tableau[z_idx] = static_cast<uint8_t>(
            (static_cast<unsigned>(tableau[z_idx]) + static_cast<unsigned>(tableau[x_idx])) % 3u);
    }).wait();
}

void wasm_tableau_csum_sycl(sycl::queue& q, uint8_t* tableau, size_t num_qutrits, size_t control, size_t target) {
    if (tableau == nullptr || num_qutrits == 0 || control >= num_qutrits || target >= num_qutrits
        || control == target) {
        return;
    }
    const size_t stride = 2 * num_qutrits;
    const size_t nrows = 2 * num_qutrits;
    q.parallel_for(sycl::range<1>(nrows), [=](sycl::id<1> idx) {
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

std::vector<uint8_t> quantize_float_buffer_to_trits_sycl(const float* src, size_t src_len, size_t out_dim) {
    if (out_dim == 0) {
        return {};
    }
    const size_t n_bytes = (out_dim + 4u) / 5u;
    std::vector<uint8_t> out(n_bytes, static_cast<uint8_t>(0));
    if (src == nullptr || src_len == 0) {
        return out;
    }
    sycl::queue& q = default_sycl_queue_instance();
    sycl::buffer<float, 1> buf_in(src, sycl::range<1>(src_len));
    sycl::buffer<uint8_t, 1> buf_out(out.data(), sycl::range<1>(out.size()));
    constexpr float kPos = 0.33f;
    constexpr float kNeg = -0.33f;
    q.submit([&](sycl::handler& h) {
        auto acc_in = buf_in.get_access<sycl::access::mode::read>(h);
        auto acc_out = buf_out.get_access<sycl::access::mode::write>(h);
        h.parallel_for(sycl::range<1>(n_bytes), [=](sycl::id<1> id) {
            const size_t bi = id[0];
            int8_t t5[5];
            for (unsigned lane = 0; lane < 5u; ++lane) {
                const size_t i = bi * 5u + static_cast<size_t>(lane);
                if (i >= out_dim) {
                    t5[lane] = 0;
                    continue;
                }
                if (i >= src_len) {
                    t5[lane] = 0;
                    continue;
                }
                const float v = acc_in[i];
                if (v > kPos) {
                    t5[lane] = 1;
                } else if (v < kNeg) {
                    t5[lane] = -1;
                } else {
                    t5[lane] = 0;
                }
            }
            acc_out[bi] = dev_pack_5trits_tb(t5);
        });
    });
    return out;
}

std::vector<uint8_t> quantize_i32_buffer_to_trits_sycl(const int32_t* src, size_t src_len, size_t out_dim) {
    if (out_dim == 0) {
        return {};
    }
    const size_t n_bytes = (out_dim + 4u) / 5u;
    std::vector<uint8_t> out(n_bytes, static_cast<uint8_t>(0));
    if (src == nullptr || src_len == 0) {
        return out;
    }
    sycl::queue& q = default_sycl_queue_instance();
    sycl::buffer<int32_t, 1> buf_in(src, sycl::range<1>(src_len));
    sycl::buffer<uint8_t, 1> buf_out(out.data(), sycl::range<1>(out.size()));
    q.submit([&](sycl::handler& h) {
        auto acc_in = buf_in.get_access<sycl::access::mode::read>(h);
        auto acc_out = buf_out.get_access<sycl::access::mode::write>(h);
        h.parallel_for(sycl::range<1>(n_bytes), [=](sycl::id<1> id) {
            const size_t bi = id[0];
            int8_t t5[5];
            for (unsigned lane = 0; lane < 5u; ++lane) {
                const size_t i = bi * 5u + static_cast<size_t>(lane);
                if (i >= out_dim) {
                    t5[lane] = 0;
                    continue;
                }
                if (i >= src_len) {
                    t5[lane] = 0;
                    continue;
                }
                int32_t v = acc_in[i];
                int im = static_cast<int>(v % 3);
                if (im < 0) {
                    im += 3;
                }
                t5[lane] = static_cast<int8_t>(im - 1);
            }
            acc_out[bi] = dev_pack_5trits_tb(t5);
        });
    });
    return out;
}

std::vector<uint8_t> quantize_string_bytes_to_trits_sycl(const char* src, size_t slen, size_t out_dim) {
    if (out_dim == 0) {
        return {};
    }
    const size_t n_bytes = (out_dim + 4u) / 5u;
    std::vector<uint8_t> out(n_bytes, static_cast<uint8_t>(0));
    if (src == nullptr || slen == 0) {
        return out;
    }
    sycl::queue& q = default_sycl_queue_instance();
    sycl::buffer<char, 1> buf_in(src, sycl::range<1>(slen));
    sycl::buffer<uint8_t, 1> buf_out(out.data(), sycl::range<1>(out.size()));
    q.submit([&](sycl::handler& h) {
        auto acc_in = buf_in.get_access<sycl::access::mode::read>(h);
        auto acc_out = buf_out.get_access<sycl::access::mode::write>(h);
        h.parallel_for(sycl::range<1>(n_bytes), [=](sycl::id<1> id) {
            const size_t bi = id[0];
            int8_t t5[5];
            for (unsigned lane = 0; lane < 5u; ++lane) {
                const size_t i = bi * 5u + static_cast<size_t>(lane);
                if (i >= out_dim) {
                    t5[lane] = 0;
                    continue;
                }
                const char c = acc_in[i % slen];
                const int ci = static_cast<int>(c);
                t5[lane] = static_cast<int8_t>((ci % 3) - 1);
            }
            acc_out[bi] = dev_pack_5trits_tb(t5);
        });
    });
    return out;
}

std::vector<uint8_t> pack_int8_lanes_to_tritpack5_sycl(const std::vector<int8_t>& lanes, size_t trit_count) {
    if (trit_count == 0) {
        return {};
    }
    const size_t num_bytes = (trit_count + 4u) / 5u;
    std::vector<uint8_t> out(num_bytes, static_cast<uint8_t>(0));
    if (lanes.empty()) {
        return out;
    }

    std::vector<int8_t> lanes_copy(trit_count, static_cast<int8_t>(0));
    const size_t n_copy = std::min(trit_count, lanes.size());
    if (n_copy > 0) {
        std::copy(lanes.begin(), lanes.begin() + static_cast<std::ptrdiff_t>(n_copy), lanes_copy.begin());
    }

    sycl::queue& q = default_sycl_queue_instance();
    sycl::buffer<int8_t, 1> buf_in(lanes_copy.data(), sycl::range<1>(lanes_copy.size()));
    sycl::buffer<uint8_t, 1> buf_out(out.data(), sycl::range<1>(out.size()));

    const size_t tc = trit_count;
    const size_t nb = num_bytes;

    q.submit([&](sycl::handler& h) {
        auto acc_in = buf_in.get_access<sycl::access::mode::read>(h);
        auto acc_out = buf_out.get_access<sycl::access::mode::write>(h);

        h.parallel_for(sycl::range<1>(nb), [=](sycl::id<1> id) {
            const size_t bi = id[0];
            const size_t base = bi * 5u;
            int8_t t5[5];
            for (unsigned k = 0; k < 5u; ++k) {
                const size_t ti = base + static_cast<size_t>(k);
                if (ti < tc) {
                    t5[k] = acc_in[ti];
                } else {
                    t5[k] = 0;
                }
            }
            acc_out[bi] = dev_pack_5trits_tb(t5);
        });
    });

    q.wait();
    return out;
}

SymplecticRoutingScoresDevice::~SymplecticRoutingScoresDevice() {
    free_ptr();
}

void SymplecticRoutingScoresDevice::free_ptr() {
    if (ptr_) {
        try {
            sycl::free(ptr_, q_);
        } catch (...) {
            // best-effort only
        }
        ptr_ = nullptr;
        n_experts_ = 0;
    }
}

SymplecticRoutingScoresDevice::SymplecticRoutingScoresDevice(SymplecticRoutingScoresDevice&& o) noexcept
    : q_(std::move(o.q_))
    , ptr_(o.ptr_)
    , n_experts_(o.n_experts_) {
    o.ptr_ = nullptr;
    o.n_experts_ = 0;
}

SymplecticRoutingScoresDevice& SymplecticRoutingScoresDevice::operator=(SymplecticRoutingScoresDevice&& o) noexcept {
    if (this != &o) {
        free_ptr();
        q_ = std::move(o.q_);
        ptr_ = o.ptr_;
        n_experts_ = o.n_experts_;
        o.ptr_ = nullptr;
        o.n_experts_ = 0;
    }
    return *this;
}

std::vector<int8_t> SymplecticRoutingScoresDevice::copy_to_host() const {
    std::vector<int8_t> h(n_experts_);
    if (!ptr_ || n_experts_ == 0) {
        return h;
    }
    // oneAPI queue::memcpy(Dest, Src, Count, DepEvents)
    q_.memcpy(h.data(), ptr_, n_experts_ * sizeof(int8_t), std::vector<sycl::event>{}).wait();
    return h;
}

std::optional<SymplecticRoutingScoresDevice> SymplecticRoutingScoresDevice::try_create_usm(
    const std::vector<uint8_t>& input_tritpack5,
    size_t input_trit_count,
    const std::vector<uint8_t>& weights_tritpack5,
    size_t total_experts,
    size_t routing_qutrits
) {
    if (total_experts == 0 || routing_qutrits == 0 || input_trit_count == 0) {
        return std::nullopt;
    }

    const size_t in_bytes = (input_trit_count + 4) / 5;
    if (input_tritpack5.size() < in_bytes) {
        return std::nullopt;
    }

    const size_t E = total_experts;
    const size_t R = routing_qutrits;
    const size_t w_row_bytes = (R + 4) / 5;
    if (weights_tritpack5.size() < E * w_row_bytes) {
        return std::nullopt;
    }

    sycl::queue& q = default_sycl_queue_instance();
    int8_t* out_usm = nullptr;
    try {
        out_usm = sycl::malloc_device<int8_t>(E, q);
    } catch (...) {
        return std::nullopt;
    }
    if (out_usm == nullptr) {
        return std::nullopt;
    }

    std::vector<uint8_t> in_copy(input_tritpack5.begin(),
                                 input_tritpack5.begin() + static_cast<std::ptrdiff_t>(in_bytes));
    std::vector<uint8_t> w_copy(weights_tritpack5.begin(),
                                weights_tritpack5.begin() + static_cast<std::ptrdiff_t>(E * w_row_bytes));

    sycl::buffer<uint8_t, 1> buf_in(in_copy.data(), sycl::range<1>(in_copy.size()));
    sycl::buffer<uint8_t, 1> buf_w(w_copy.data(), sycl::range<1>(w_copy.size()));

    const size_t in_trit_c = input_trit_count;
    try {
        q.submit([&](sycl::handler& h) {
            auto acc_in = buf_in.get_access<sycl::access::mode::read>(h);
            auto acc_w = buf_w.get_access<sycl::access::mode::read>(h);

            h.parallel_for(sycl::range<1>(E), [=](sycl::id<1> id) {
                const size_t e = id[0];
                const size_t min_size = in_trit_c < R ? in_trit_c : R;
                int8_t symplectic_sum = 0;

                for (size_t i = 0; i < min_size; i += 2) {
                    const int8_t x1 = read_trit_t5_poly_bw(acc_in, size_t{0}, i);
                    const int8_t z1 =
                        (i + 1 < min_size) ? read_trit_t5_poly_bw(acc_in, size_t{0}, i + 1u) : static_cast<int8_t>(0);

                    const size_t w_row_bytes_l = (R + 4u) / 5u;
                    const size_t w_base = e * w_row_bytes_l;
                    const int8_t x2 = read_trit_t5_poly_bw(acc_w, w_base, i);
                    const int8_t z2 =
                        (i + 1 < min_size) ? read_trit_t5_poly_bw(acc_w, w_base, i + 1u) : static_cast<int8_t>(0);

                    int pairing =
                        static_cast<int>(x1) * static_cast<int>(z2) - static_cast<int>(z1) * static_cast<int>(x2);
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

                out_usm[e] = symplectic_sum;
            });
        });
        q.wait();
    } catch (...) {
        sycl::free(out_usm, q);
        return std::nullopt;
    }

    SymplecticRoutingScoresDevice out;
    out.q_ = q;
    out.ptr_ = out_usm;
    out.n_experts_ = E;
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

    sycl::queue& q = default_sycl_queue_instance();

    std::vector<int8_t> scores(total_experts, static_cast<int8_t>(-1));

    std::vector<uint8_t> in_copy(input_tritpack5.begin(),
                                 input_tritpack5.begin() + static_cast<std::ptrdiff_t>(in_bytes));
    std::vector<uint8_t> w_copy(weights_tritpack5.begin(),
                                weights_tritpack5.begin() + static_cast<std::ptrdiff_t>(E * w_row_bytes));

    sycl::buffer<uint8_t, 1> buf_in(in_copy.data(), sycl::range<1>(in_copy.size()));
    sycl::buffer<uint8_t, 1> buf_w(w_copy.data(), sycl::range<1>(w_copy.size()));
    sycl::buffer<int8_t, 1> buf_out(scores.data(), sycl::range<1>(scores.size()));

    q.submit([&](sycl::handler& h) {
        auto acc_in = buf_in.get_access<sycl::access::mode::read>(h);
        auto acc_w = buf_w.get_access<sycl::access::mode::read>(h);
        auto acc_out = buf_out.get_access<sycl::access::mode::write>(h);

        h.parallel_for(sycl::range<1>(E), [=](sycl::id<1> id) {
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
    });

    return scores;
}

std::vector<std::uint32_t> moe_routing_symplectic_topk_indices_sycl(
    const std::vector<std::uint8_t>& input_tritpack5,
    size_t input_trit_count,
    const std::vector<std::uint8_t>& weights_tritpack5,
    size_t total_experts,
    size_t routing_qutrits,
    size_t k
) {
    if (total_experts == 0 || routing_qutrits == 0 || input_trit_count == 0 || k == 0) {
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

    if (k > E) {
        k = E;
    }

    sycl::queue& q = default_sycl_queue_instance();

    std::vector<int8_t> scores(E, static_cast<int8_t>(-1));
    std::vector<std::uint32_t> topk(k, 0u);

    std::vector<std::uint8_t> in_copy(
        input_tritpack5.begin(), input_tritpack5.begin() + static_cast<std::ptrdiff_t>(in_bytes));
    std::vector<std::uint8_t> w_copy(
        weights_tritpack5.begin(),
        weights_tritpack5.begin() + static_cast<std::ptrdiff_t>(E * w_row_bytes));

    sycl::buffer<std::uint8_t, 1> buf_in(in_copy.data(), sycl::range<1>(in_copy.size()));
    sycl::buffer<std::uint8_t, 1> buf_w(w_copy.data(), sycl::range<1>(w_copy.size()));
    sycl::buffer<int8_t, 1> buf_scores(scores.data(), sycl::range<1>(scores.size()));
    sycl::buffer<std::uint32_t, 1> buf_topk(topk.data(), sycl::range<1>(topk.size()));

    const size_t E_c = E;
    const size_t R_c = R;
    const size_t in_trit = input_trit_count;
    const size_t k_sel = k;

    q.submit([&](sycl::handler& h) {
        auto acc_in = buf_in.get_access<sycl::access::mode::read>(h);
        auto acc_w = buf_w.get_access<sycl::access::mode::read>(h);
        auto acc_out = buf_scores.get_access<sycl::access::mode::write>(h);

        h.parallel_for(sycl::range<1>(E_c), [=](sycl::id<1> id) {
            const size_t e = id[0];
            const size_t min_size = in_trit < R_c ? in_trit : R_c;
            int8_t symplectic_sum = 0;

            for (size_t i = 0; i < min_size; i += 2) {
                const int8_t x1 = read_trit_t5_poly_bw(acc_in, size_t{0}, i);
                const int8_t z1 =
                    (i + 1 < min_size) ? read_trit_t5_poly_bw(acc_in, size_t{0}, i + 1u) : static_cast<int8_t>(0);

                const size_t w_row_bytes_l = (R_c + 4u) / 5u;
                const size_t w_base = e * w_row_bytes_l;
                const int8_t x2 = read_trit_t5_poly_bw(acc_w, w_base, i);
                const int8_t z2 =
                    (i + 1 < min_size) ? read_trit_t5_poly_bw(acc_w, w_base, i + 1u) : static_cast<int8_t>(0);

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
    });

    q.submit([&](sycl::handler& h) {
        auto acc_sc = buf_scores.get_access<sycl::access::mode::read>(h);
        auto acc_top = buf_topk.get_access<sycl::access::mode::write>(h);

        h.single_task([=] {
            size_t pos = 0;
            const int8_t tiers[3] = {1, 0, -1};
            for (int ti = 0; ti < 3; ++ti) {
                const int8_t tier = tiers[ti];
                for (std::uint32_t e = 0; e < static_cast<std::uint32_t>(E_c); ++e) {
                    if (pos >= k_sel) {
                        return;
                    }
                    if (acc_sc[e] == tier) {
                        acc_top[pos] = e;
                        ++pos;
                    }
                }
            }
        });
    });

    q.wait();
    return topk;
}

std::vector<std::uint32_t> moe_routing_symplectic_topk_indices_sycl_batched(
    const std::vector<std::uint8_t>& inputs_tritpack5_rows_concat,
    size_t input_trit_count,
    const std::vector<std::uint8_t>& weights_tritpack5,
    size_t total_experts,
    size_t routing_qutrits,
    size_t k,
    size_t num_rows
) {
    if (num_rows == 0 || total_experts == 0 || routing_qutrits == 0 || input_trit_count == 0 || k == 0) {
        return {};
    }

    const size_t in_bytes = (input_trit_count + 4) / 5;
    if (inputs_tritpack5_rows_concat.size() < in_bytes * num_rows) {
        return {};
    }

    const size_t E = total_experts;
    const size_t R = routing_qutrits;
    const size_t w_row_bytes = (R + 4) / 5;
    if (weights_tritpack5.size() < E * w_row_bytes) {
        return {};
    }

    if (k > E) {
        k = E;
    }

    const size_t N = num_rows;
    sycl::queue& q = default_sycl_queue_instance();

    std::vector<int8_t> scores(N * E, static_cast<int8_t>(-1));
    std::vector<std::uint32_t> topk(N * k, 0u);

    std::vector<std::uint8_t> in_copy(
        inputs_tritpack5_rows_concat.begin(),
        inputs_tritpack5_rows_concat.begin() + static_cast<std::ptrdiff_t>(in_bytes * N));
    std::vector<std::uint8_t> w_copy(
        weights_tritpack5.begin(),
        weights_tritpack5.begin() + static_cast<std::ptrdiff_t>(E * w_row_bytes));

    sycl::buffer<std::uint8_t, 1> buf_in(in_copy.data(), sycl::range<1>(in_copy.size()));
    sycl::buffer<std::uint8_t, 1> buf_w(w_copy.data(), sycl::range<1>(w_copy.size()));
    sycl::buffer<int8_t, 1> buf_scores(scores.data(), sycl::range<1>(scores.size()));
    sycl::buffer<std::uint32_t, 1> buf_topk(topk.data(), sycl::range<1>(topk.size()));

    const size_t E_c = E;
    const size_t R_c = R;
    const size_t in_trit = input_trit_count;
    const size_t k_sel = k;
    const size_t in_bytes_c = in_bytes;
    const size_t N_c = N;

    q.submit([&](sycl::handler& h) {
        auto acc_in = buf_in.get_access<sycl::access::mode::read>(h);
        auto acc_w = buf_w.get_access<sycl::access::mode::read>(h);
        auto acc_out = buf_scores.get_access<sycl::access::mode::write>(h);

        h.parallel_for(sycl::range<1>(N_c * E_c), [=](sycl::id<1> id) {
            const size_t lin = id[0];
            const size_t r = lin / E_c;
            const size_t e = lin - r * E_c;
            const size_t in_row_base = r * in_bytes_c;
            const size_t min_size = in_trit < R_c ? in_trit : R_c;
            int8_t symplectic_sum = 0;

            for (size_t i = 0; i < min_size; i += 2) {
                const int8_t x1 = read_trit_t5_poly_bw(acc_in, in_row_base, i);
                const int8_t z1 =
                    (i + 1 < min_size) ? read_trit_t5_poly_bw(acc_in, in_row_base, i + 1u) : static_cast<int8_t>(0);

                const size_t w_row_bytes_l = (R_c + 4u) / 5u;
                const size_t w_base = e * w_row_bytes_l;
                const int8_t x2 = read_trit_t5_poly_bw(acc_w, w_base, i);
                const int8_t z2 =
                    (i + 1 < min_size) ? read_trit_t5_poly_bw(acc_w, w_base, i + 1u) : static_cast<int8_t>(0);

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

            acc_out[r * E_c + e] = symplectic_sum;
        });
    });

    q.submit([&](sycl::handler& h) {
        auto acc_sc = buf_scores.get_access<sycl::access::mode::read>(h);
        auto acc_top = buf_topk.get_access<sycl::access::mode::write>(h);

        h.parallel_for(sycl::range<1>(N_c), [=](sycl::id<1> rid) {
            const size_t r = rid[0];
            const size_t score_base = r * E_c;
            size_t pos = 0;
            const int8_t tiers[3] = {1, 0, -1};
            for (int ti = 0; ti < 3; ++ti) {
                const int8_t tier = tiers[ti];
                for (std::uint32_t e = 0; e < static_cast<std::uint32_t>(E_c); ++e) {
                    if (pos >= k_sel) {
                        return;
                    }
                    if (acc_sc[score_base + e] == tier) {
                        acc_top[r * k_sel + pos] = e;
                        ++pos;
                    }
                }
            }
        });
    });

    q.wait();
    return topk;
}

std::vector<int32_t> moe_quantum_entangled_scores_from_logits_device_sycl(
    sycl::queue& q,
    const int8_t* logits_device,
    size_t total_experts,
    const std::vector<int32_t>& coupling_flat_rowmajor
) {
    if (logits_device == nullptr || total_experts == 0) {
        return {};
    }
    const size_t E = total_experts;
    if (coupling_flat_rowmajor.size() < E * E) {
        return {};
    }

    std::vector<int32_t> out(E, 0);
    std::vector<int32_t> coupling_copy(
        coupling_flat_rowmajor.begin(),
        coupling_flat_rowmajor.begin() + static_cast<std::ptrdiff_t>(E * E));
    sycl::buffer<int32_t, 1> buf_coupling(coupling_copy.data(), sycl::range<1>(coupling_copy.size()));
    sycl::buffer<int32_t, 1> buf_out(out.data(), sycl::range<1>(out.size()));

    q.submit([&](sycl::handler& h) {
        auto acc_c = buf_coupling.get_access<sycl::access::mode::read>(h);
        auto acc_out = buf_out.get_access<sycl::access::mode::write>(h);
        h.parallel_for(sycl::range<1>(E), [=](sycl::id<1> id) {
            const size_t i = id[0];
            int32_t qscore = static_cast<int32_t>(logits_device[i]);
            for (size_t j = 0; j < E; ++j) {
                if (j == i) {
                    continue;
                }
                const int32_t coupling = acc_c[i * E + j];
                const int32_t interference = (static_cast<int32_t>(logits_device[j]) * coupling) / 100;
                qscore += interference / 4;
            }
            acc_out[i] = qscore;
        });
    });
    q.wait();
    return out;
}

std::vector<int8_t> moe_multi_objective_final_scores_from_logits_device_sycl(
    sycl::queue& q,
    const int8_t* logits_device,
    size_t total_experts,
    std::uint32_t ternary_seed,
    std::uint32_t num_objectives,
    std::int32_t weight_dim0,
    std::int32_t weight_dim1,
    std::int32_t weight_dim2,
    std::int32_t weight_dim3)
{
    if (logits_device == nullptr || total_experts == 0) {
        return {};
    }
    const size_t E = total_experts;

    std::vector<int8_t> out(E);
    sycl::buffer<int8_t, 1> buf_out(out.data(), sycl::range<1>(out.size()));

    q.submit([&](sycl::handler& h) {
        auto acc_out = buf_out.get_access<sycl::access::mode::write>(h);
        h.parallel_for(sycl::range<1>(E), [=](sycl::id<1> id) {
            const size_t e = id[0];
            const int32_t lo = static_cast<int32_t>(logits_device[e]);
            const int32_t o0 = (lo + 128) * 1000 / 255;

            const uint32_t s = ternary_seed;
            const int32_t o1 = 1000 - static_cast<int32_t>(s % 100) * 10;
            const int32_t o2 = 700 + static_cast<int32_t>(s % 300);
            const int32_t o3 = 1000 - static_cast<int32_t>(s % 50) * 10;

            int32_t acc = 0;
            if (num_objectives > 0) {
                acc += (o0 * weight_dim0) / 1000;
            }
            if (num_objectives > 1) {
                acc += (o1 * weight_dim1) / 1000;
            }
            if (num_objectives > 2) {
                acc += (o2 * weight_dim2) / 1000;
            }
            if (num_objectives > 3) {
                acc += (o3 * weight_dim3) / 1000;
            }

            int32_t fs = (acc * 127) / 1000 - 128;
            if (fs > 127) {
                fs = 127;
            }
            if (fs < -128) {
                fs = -128;
            }
            acc_out[e] = static_cast<int8_t>(fs);
        });
    });
    q.wait();
    return out;
}

SYCLQueue::SYCLQueue()
    : queue_(sycl::default_selector_v)
{
}

SYCLQueue::~SYCLQueue() {
    queue_.wait();
}

void SYCLQueue::submit_tableau_update(
    sycl::buffer<int8_t, 2>& tableau_buf,
    sycl::buffer<int8_t, 1>& phase_buf,
    size_t target_qutrit
) {
    queue_.submit([&](sycl::handler& h) {
        auto tableau_acc = tableau_buf.get_access<sycl::access::mode::read_write>(h);
        auto phase_acc = phase_buf.get_access<sycl::access::mode::read_write>(h);
        
        size_t n = tableau_buf.get_range()[0] / 2;
        
        h.parallel_for(sycl::range<1>(2 * n), [=](sycl::id<1> idx) {
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