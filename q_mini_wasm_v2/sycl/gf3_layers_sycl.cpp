// GF(3) linear layer SYCL paths (tropical forward, standard forward, Hebbian update).
#include "gf3_layers_sycl.hpp"

#include <algorithm>
#include <cstdlib>

#if defined(USE_SYCL) && USE_SYCL

#include <CL/sycl.hpp>

namespace q_mini_wasm_v2::sycl_kernels {

namespace {

cl::sycl::queue& gf3_layers_default_queue() {
    static cl::sycl::queue q{cl::sycl::default_selector_v};
    return q;
}

bool env_gf3_sycl_disabled() {
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
    const char* e = std::getenv("QMINI_GF3_SYCL");
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
    return e != nullptr && e[0] == '0' && e[1] == '\0';
}

} // namespace

bool gf3_sycl_forward_enabled_for_shape(size_t input_dim, size_t output_dim) noexcept {
    if (env_gf3_sycl_disabled()) {
        return false;
    }
    const size_t cells = input_dim * output_dim;
    return cells >= 4096u || output_dim >= 512u;
}

static inline int8_t dev_gf3_mul(int8_t a, int8_t b) {
    if (a == 0 || b == 0) {
        return 0;
    }
    if (a == b) {
        return 1;
    }
    return -1;
}

static inline int8_t dev_gf3_add(int8_t a, int8_t b) {
    int8_t sum = static_cast<int8_t>(a + b);
    if (sum > 1) {
        return -1;
    }
    if (sum < -1) {
        return 1;
    }
    return sum;
}

bool gf3_tropical_linear_forward_sycl(
    const std::vector<int8_t>& input_trits,
    const std::vector<int8_t>& weights_row_major,
    const std::vector<int8_t>& bias_trits,
    bool use_bias,
    size_t input_dim,
    size_t output_dim,
    std::vector<int8_t>& out_trits
) {
    if (!gf3_sycl_forward_enabled_for_shape(input_dim, output_dim)) {
        return false;
    }
    if (input_trits.empty() || weights_row_major.size() < input_dim * output_dim) {
        return false;
    }
    if (use_bias && bias_trits.size() < output_dim) {
        return false;
    }

    const size_t in_loop = std::min(input_trits.size(), input_dim);

    out_trits.assign(output_dim, static_cast<int8_t>(0));

    try {
        cl::sycl::queue& q = gf3_layers_default_queue();
        cl::sycl::buffer<int8_t, 1> buf_in(input_trits.data(), cl::sycl::range<1>(input_dim));
        cl::sycl::buffer<int8_t, 1> buf_w(weights_row_major.data(), cl::sycl::range<1>(input_dim * output_dim));
        static int8_t s_dummy_bias = 0;
        cl::sycl::buffer<int8_t, 1> buf_bias(
            use_bias ? bias_trits.data() : &s_dummy_bias, cl::sycl::range<1>(use_bias ? output_dim : 1));
        cl::sycl::buffer<int8_t, 1> buf_out(out_trits.data(), cl::sycl::range<1>(output_dim));

        const size_t in_lim = in_loop;
        const size_t out_d = output_dim;
        const uint8_t ub = use_bias ? 1u : 0u;

        q.submit([&](cl::sycl::handler& h) {
            auto acc_in = buf_in.get_access<cl::sycl::access::mode::read>(h);
            auto acc_w = buf_w.get_access<cl::sycl::access::mode::read>(h);
            auto acc_bias = buf_bias.get_access<cl::sycl::access::mode::read>(h);
            auto acc_out = buf_out.get_access<cl::sycl::access::mode::write>(h);

            h.parallel_for(cl::sycl::range<1>(out_d), [=](cl::sycl::id<1> jid) {
                const size_t j = jid[0];
                int8_t max_val = -2;
                for (size_t i = 0; i < in_lim; ++i) {
                    const int8_t xi = acc_in[i];
                    const int8_t wij = acc_w[i * out_d + j];
                    const int8_t val = static_cast<int8_t>(xi + wij);
                    max_val = (val > max_val) ? val : max_val;
                }
                if (ub) {
                    const int8_t bj = acc_bias[j];
                    max_val = (bj > max_val) ? bj : max_val;
                }
                if (max_val > 1) {
                    max_val = 1;
                }
                if (max_val < -1) {
                    max_val = -1;
                }
                acc_out[j] = max_val;
            });
        }).wait();
        return true;
    } catch (...) {
        return false;
    }
}

bool gf3_standard_linear_forward_sycl(
    const std::vector<int8_t>& input_trits,
    const std::vector<int8_t>& weights_row_major,
    const std::vector<int8_t>& bias_trits,
    bool use_bias,
    size_t input_dim,
    size_t output_dim,
    std::vector<int8_t>& out_trits
) {
    if (!gf3_sycl_forward_enabled_for_shape(input_dim, output_dim)) {
        return false;
    }
    if (input_trits.empty() || weights_row_major.size() < input_dim * output_dim) {
        return false;
    }
    if (use_bias && bias_trits.size() < output_dim) {
        return false;
    }

    const size_t in_loop = std::min(input_trits.size(), input_dim);

    std::vector<int32_t> pre(static_cast<size_t>(output_dim), 0);
    out_trits.assign(output_dim, static_cast<int8_t>(0));

    try {
        cl::sycl::queue& q = gf3_layers_default_queue();
        cl::sycl::buffer<int8_t, 1> buf_in(input_trits.data(), cl::sycl::range<1>(input_dim));
        cl::sycl::buffer<int8_t, 1> buf_w(weights_row_major.data(), cl::sycl::range<1>(input_dim * output_dim));
        static int8_t s_dummy_bias = 0;
        cl::sycl::buffer<int8_t, 1> buf_bias(
            use_bias ? bias_trits.data() : &s_dummy_bias, cl::sycl::range<1>(use_bias ? output_dim : 1));
        cl::sycl::buffer<int32_t, 1> buf_pre(pre.data(), cl::sycl::range<1>(output_dim));

        const size_t in_lim = in_loop;
        const size_t out_d = output_dim;
        const uint8_t ub = use_bias ? 1u : 0u;

        q.submit([&](cl::sycl::handler& h) {
            auto acc_in = buf_in.get_access<cl::sycl::access::mode::read>(h);
            auto acc_w = buf_w.get_access<cl::sycl::access::mode::read>(h);
            auto acc_bias = buf_bias.get_access<cl::sycl::access::mode::read>(h);
            auto acc_pre = buf_pre.get_access<cl::sycl::access::mode::write>(h);

            h.parallel_for(cl::sycl::range<1>(out_d), [=](cl::sycl::id<1> jid) {
                const size_t j = jid[0];
                int32_t sum = 0;
                for (size_t i = 0; i < in_lim; ++i) {
                    const int8_t p = dev_gf3_mul(acc_in[i], acc_w[i * out_d + j]);
                    sum += static_cast<int32_t>(p);
                }
                if (ub) {
                    sum += static_cast<int32_t>(acc_bias[j]);
                }
                acc_pre[j] = sum;
            });
        }).wait();

        for (size_t j = 0; j < output_dim; ++j) {
            const int32_t val = pre[j];
            if (val > 0) {
                out_trits[j] = 1;
            } else if (val < 0) {
                out_trits[j] = -1;
            } else {
                out_trits[j] = 0;
            }
        }
        return true;
    } catch (...) {
        return false;
    }
}

bool gf3_hebbian_update_sycl(
    const std::vector<int8_t>& input_trits,
    int32_t goodness_delta,
    int8_t learning_rate,
    size_t input_dim,
    size_t output_dim,
    std::vector<int8_t>& weights_row_major
) {
    if (!gf3_sycl_forward_enabled_for_shape(input_dim, output_dim)) {
        return false;
    }
    if (goodness_delta == 0 || input_trits.empty()) {
        return false;
    }
    if (weights_row_major.size() < input_dim * output_dim) {
        return false;
    }

    const size_t in_loop = std::min(input_trits.size(), input_dim);

    const int8_t update_sign =
        static_cast<int8_t>((goodness_delta > 0) ? learning_rate : -learning_rate);

    try {
        cl::sycl::queue& q = gf3_layers_default_queue();
        cl::sycl::buffer<int8_t, 1> buf_in(input_trits.data(), cl::sycl::range<1>(input_dim));
        cl::sycl::buffer<int8_t, 1> buf_w(weights_row_major.data(), cl::sycl::range<1>(input_dim * output_dim));

        const size_t in_d = input_dim;
        const size_t in_lim = in_loop;
        const size_t out_d = output_dim;
        const int8_t us = update_sign;

        q.submit([&](cl::sycl::handler& h) {
            auto acc_in = buf_in.get_access<cl::sycl::access::mode::read>(h);
            auto acc_w = buf_w.get_access<cl::sycl::access::mode::read_write>(h);

            h.parallel_for(cl::sycl::range<1>(in_d * out_d), [=](cl::sycl::id<1> lid) {
                const size_t lin = lid[0];
                const size_t i = lin / out_d;
                if (i >= in_lim) {
                    return;
                }
                const int8_t delta = dev_gf3_mul(us, acc_in[i]);
                const int8_t nw = dev_gf3_add(acc_w[lin], delta);
                acc_w[lin] = nw;
            });
        }).wait();
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace q_mini_wasm_v2::sycl_kernels

#else

namespace q_mini_wasm_v2::sycl_kernels {

bool gf3_sycl_forward_enabled_for_shape(size_t, size_t) noexcept {
    return false;
}

bool gf3_tropical_linear_forward_sycl(
    const std::vector<int8_t>&,
    const std::vector<int8_t>&,
    const std::vector<int8_t>&,
    bool,
    size_t,
    size_t,
    std::vector<int8_t>&) {
    return false;
}

bool gf3_standard_linear_forward_sycl(
    const std::vector<int8_t>&,
    const std::vector<int8_t>&,
    const std::vector<int8_t>&,
    bool,
    size_t,
    size_t,
    std::vector<int8_t>&) {
    return false;
}

bool gf3_hebbian_update_sycl(
    const std::vector<int8_t>&, int32_t, int8_t, size_t, size_t, std::vector<int8_t>&) {
    return false;
}

} // namespace q_mini_wasm_v2::sycl_kernels

#endif
