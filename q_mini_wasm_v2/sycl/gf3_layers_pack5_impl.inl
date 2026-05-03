// Included from gf3_layers_sycl.cpp when USE_SYCL — batched GF(3) linear ops with TritPack5 for
// activations, biases, and weights (no one-trit-per-int8 on SYCL buffers).

template <typename AccessorT>
static inline int8_t dev_read_t5_idx(const AccessorT& acc, size_t tri) {
    const size_t bi = tri / 5u;
    const unsigned lane = static_cast<unsigned>(tri % 5u);
    uint8_t x = acc[bi];
    for (unsigned k = 0; k < lane; ++k) {
        x = static_cast<uint8_t>(x / 3u);
    }
    const uint8_t u = static_cast<uint8_t>(x % 3u);
    return static_cast<int8_t>(static_cast<int>(u) - 1);
}

/** Bounds-checked Pack5 read — avoids out-of-range accessor indices if host sizing ever drifts from kernel math. */
template <typename AccessorT>
static inline int8_t dev_read_t5_idx_bounded(const AccessorT& acc, size_t tri, size_t max_trits) {
    if (tri >= max_trits) {
        return 0;
    }
    return dev_read_t5_idx(acc, tri);
}

/** USM / raw device pointer variant (same layout as accessor indexing). */
static inline int8_t dev_read_t5_idx_ptr(const uint8_t* acc, size_t tri) {
    const size_t bi = tri / 5u;
    const unsigned lane = static_cast<unsigned>(tri % 5u);
    uint8_t x = acc[bi];
    for (unsigned k = 0; k < lane; ++k) {
        x = static_cast<uint8_t>(x / 3u);
    }
    const uint8_t u = static_cast<uint8_t>(x % 3u);
    return static_cast<int8_t>(static_cast<int>(u) - 1);
}

static inline int8_t dev_read_t5_idx_bounded_ptr(const uint8_t* acc, size_t tri, size_t max_trits) {
    if (tri >= max_trits) {
        return 0;
    }
    return dev_read_t5_idx_ptr(acc, tri);
}

static inline uint8_t dev_pack_5trits(const int8_t* t5) {
    uint32_t acc = 0;
    static const uint32_t POW3[5] = {1u, 3u, 9u, 27u, 81u};
    for (int i = 0; i < 5; ++i) {
        const uint32_t u = static_cast<uint32_t>(static_cast<int>(t5[i]) + 1);
        acc += u * POW3[static_cast<size_t>(i)];
    }
    return static_cast<uint8_t>(acc);
}

static inline bool gf3_mul_overflow_size_local(size_t a, size_t b, size_t& out) {
    if (a == 0 || b == 0) {
        out = 0;
        return false;
    }
    if (b > (std::numeric_limits<size_t>::max() / a)) {
        return true;
    }
    out = a * b;
    return false;
}

bool gf3_tropical_linear_forward_batched_pack5_io_sycl(
    const std::vector<uint8_t>& batch_in_packed,
    const std::vector<uint8_t>& batch_w_packed,
    const std::vector<uint8_t>& batch_bias_packed,
    bool use_bias,
    size_t batch_size,
    size_t input_dim,
    size_t output_dim,
    std::vector<uint8_t>& batch_out_packed,
    bool wait_for_completion) {
    if (!gf3_sycl_batched_shape_ok(batch_size, input_dim, output_dim)) {
        return false;
    }
    const size_t B = batch_size;
    const size_t in_d = input_dim;
    const size_t out_d = output_dim;
    size_t n_in_trits = 0;
    if (gf3_mul_overflow_size_local(B, in_d, n_in_trits)) {
        return false;
    }
    const size_t n_in_bytes = (n_in_trits + 4u) / 5u;
    size_t w_cells = 0;
    if (gf3_mul_overflow_size_local(in_d, out_d, w_cells)) {
        return false;
    }
    size_t n_w_trits = 0;
    if (gf3_mul_overflow_size_local(B, w_cells, n_w_trits)) {
        return false;
    }
    const size_t n_w_bytes = (n_w_trits + 4u) / 5u;
    size_t n_bias_trits = 0;
    if (gf3_mul_overflow_size_local(B, out_d, n_bias_trits)) {
        return false;
    }
    const size_t n_bias_bytes = (n_bias_trits + 4u) / 5u;
    size_t n_out_trits = 0;
    if (gf3_mul_overflow_size_local(B, out_d, n_out_trits)) {
        return false;
    }
    const size_t n_out_bytes = (n_out_trits + 4u) / 5u;

    gf3_sycl_submit_log::log_batched_grid_throttled("gf3_tropical_linear_forward_batched", B, in_d, out_d, n_in_bytes,
                                                    n_w_bytes, n_out_bytes, n_out_bytes,
                                                    "range1d=n_out_bytes (one WI per output pack5 byte)");

    batch_out_packed.assign(n_out_bytes, static_cast<uint8_t>(0));

    try {
        std::vector<uint8_t> pad_in;
        std::vector<uint8_t> pad_w;
        std::vector<uint8_t> pad_bias;
        const uint8_t* in_ptr = batch_in_packed.data();
        const uint8_t* w_ptr = batch_w_packed.data();
        const uint8_t* bias_ptr = batch_bias_packed.data();
        if (batch_in_packed.size() < n_in_bytes) {
            pad_in.assign(batch_in_packed.begin(), batch_in_packed.end());
            pad_in.resize(n_in_bytes, static_cast<uint8_t>(0));
            in_ptr = pad_in.data();
        }
        if (batch_w_packed.size() < n_w_bytes) {
            pad_w.assign(batch_w_packed.begin(), batch_w_packed.end());
            pad_w.resize(n_w_bytes, static_cast<uint8_t>(0));
            w_ptr = pad_w.data();
        }
        if (use_bias && batch_bias_packed.size() < n_bias_bytes) {
            pad_bias.assign(batch_bias_packed.begin(), batch_bias_packed.end());
            pad_bias.resize(n_bias_bytes, static_cast<uint8_t>(0));
            bias_ptr = pad_bias.data();
        }

        sycl::queue& q = gf3_layers_queue_for_thread();
        sycl::buffer<uint8_t, 1> buf_in(in_ptr, sycl::range<1>(n_in_bytes));
        sycl::buffer<uint8_t, 1> buf_w(w_ptr, sycl::range<1>(n_w_bytes));
        static uint8_t s_dummy_bias = 0;
        sycl::buffer<uint8_t, 1> buf_bias(
            use_bias ? bias_ptr : &s_dummy_bias, sycl::range<1>(use_bias ? n_bias_bytes : 1));
        sycl::buffer<uint8_t, 1> buf_out(batch_out_packed.data(), sycl::range<1>(n_out_bytes));

        const uint8_t ub = use_bias ? 1u : 0u;

        q.submit([&](sycl::handler& h) {
            auto acc_in = buf_in.get_access<sycl::access::mode::read>(h);
            auto acc_w = buf_w.get_access<sycl::access::mode::read>(h);
            auto acc_bias = buf_bias.get_access<sycl::access::mode::read>(h);
            auto acc_out = buf_out.get_access<sycl::access::mode::write>(h);

            h.parallel_for(sycl::range<1>(n_out_bytes), [=](sycl::id<1> oid) {
                const size_t ob = oid[0];
                int8_t t5[5];
                for (unsigned lane = 0; lane < 5u; ++lane) {
                    const size_t tglob = ob * 5u + lane;
                    if (tglob >= n_out_trits) {
                        t5[lane] = 0;
                        continue;
                    }
                    const size_t b = tglob / out_d;
                    const size_t j = tglob % out_d;
                    int8_t max_val = -2;
                    for (size_t i = 0; i < in_d; ++i) {
                        const size_t in_tri = b * in_d + i;
                        const int8_t xi = dev_read_t5_idx_bounded(acc_in, in_tri, n_in_trits);
                        const size_t w_tri = b * w_cells + i * out_d + j;
                        const int8_t wij = dev_read_t5_idx_bounded(acc_w, w_tri, n_w_trits);
                        const int8_t val = static_cast<int8_t>(xi + wij);
                        max_val = (val > max_val) ? val : max_val;
                    }
                    if (ub) {
                        const size_t bias_tri = b * out_d + j;
                        const int8_t bj = dev_read_t5_idx_bounded(acc_bias, bias_tri, n_bias_trits);
                        max_val = (bj > max_val) ? bj : max_val;
                    }
                    if (max_val > 1) {
                        max_val = 1;
                    }
                    if (max_val < -1) {
                        max_val = -1;
                    }
                    t5[lane] = max_val;
                }
                acc_out[ob] = dev_pack_5trits(t5);
            });
        });
        (void)wait_for_completion;
        q.wait_and_throw();
        return true;
    } catch (const sycl::exception& e) {
        std::cerr << "[GF3 SYCL] gf3_tropical_linear_forward_batched_pack5_io_sycl: " << e.what() << std::endl;
        return false;
    } catch (const std::exception& e) {
        std::cerr << "[GF3 SYCL] gf3_tropical_linear_forward_batched_pack5_io_sycl: " << e.what() << std::endl;
        return false;
    } catch (...) {
        std::cerr << "[GF3 SYCL] gf3_tropical_linear_forward_batched_pack5_io_sycl: unknown exception\n";
        return false;
    }
}

bool gf3_hebbian_update_batched_pack5_io_sycl(
    const std::vector<uint8_t>& batch_in_packed,
    const std::vector<int32_t>& delta,
    int8_t learning_rate,
    size_t batch_size,
    size_t input_dim,
    size_t output_dim,
    std::vector<uint8_t>& batch_w_packed,
    bool wait_for_completion) {
    if (!gf3_sycl_batched_shape_ok(batch_size, input_dim, output_dim)) {
        return false;
    }
    const size_t B = batch_size;
    const size_t in_d = input_dim;
    const size_t out_d = output_dim;
    size_t w_cells = 0;
    if (gf3_mul_overflow_size_local(in_d, out_d, w_cells)) {
        return false;
    }
    size_t n_w_trits = 0;
    if (gf3_mul_overflow_size_local(B, w_cells, n_w_trits)) {
        return false;
    }
    const size_t n_w_bytes = (n_w_trits + 4u) / 5u;
    size_t n_in_trits = 0;
    if (gf3_mul_overflow_size_local(B, in_d, n_in_trits)) {
        return false;
    }
    const size_t n_in_bytes = (n_in_trits + 4u) / 5u;

    gf3_sycl_submit_log::log_batched_grid_throttled("gf3_hebbian_update_batched", B, in_d, out_d, n_in_bytes, n_w_bytes,
                                                    n_w_bytes, n_w_bytes,
                                                    "range1d=n_w_bytes (one WI per weight pack5 byte)");

    if (batch_w_packed.size() < n_w_bytes || delta.size() < B) {
        return false;
    }

    try {
        std::vector<int32_t> delta_buf(delta.begin(), delta.begin() + static_cast<std::ptrdiff_t>(B));
        std::vector<uint8_t> pad_in;
        const uint8_t* in_ptr = batch_in_packed.data();
        if (batch_in_packed.size() < n_in_bytes) {
            pad_in.assign(batch_in_packed.begin(), batch_in_packed.end());
            pad_in.resize(n_in_bytes, static_cast<uint8_t>(0));
            in_ptr = pad_in.data();
        }

        sycl::queue& q = gf3_layers_queue_for_thread();
        sycl::buffer<uint8_t, 1> buf_in(in_ptr, sycl::range<1>(n_in_bytes));
        sycl::buffer<uint8_t, 1> buf_w(batch_w_packed.data(), sycl::range<1>(n_w_bytes));
        sycl::buffer<int32_t, 1> buf_d(delta_buf.data(), sycl::range<1>(B));

        const int8_t lr = learning_rate;

        q.submit([&](sycl::handler& h) {
            auto acc_in = buf_in.get_access<sycl::access::mode::read>(h);
            auto acc_w = buf_w.get_access<sycl::access::mode::read_write>(h);
            auto acc_delta = buf_d.get_access<sycl::access::mode::read>(h);

            h.parallel_for(sycl::range<1>(n_w_bytes), [=](sycl::id<1> oid) {
                const size_t bi = oid[0];
                int8_t t5[5];
                {
                    uint8_t x = acc_w[bi];
                    for (int k = 0; k < 5; ++k) {
                        const uint8_t u = static_cast<uint8_t>(x % 3u);
                        x = static_cast<uint8_t>(x / 3u);
                        t5[k] = static_cast<int8_t>(static_cast<int>(u) - 1);
                    }
                }
                for (unsigned lane = 0; lane < 5u; ++lane) {
                    const size_t T = bi * 5u + lane;
                    if (T >= n_w_trits) {
                        continue;
                    }
                    const size_t b = T / w_cells;
                    const size_t rem = T % w_cells;
                    const size_t i = rem / out_d;
                    const size_t j = rem % out_d;
                    const int32_t gd = acc_delta[b];
                    if (gd == 0) {
                        continue;
                    }
                    const int8_t us = static_cast<int8_t>((gd > 0) ? lr : -lr);
                    const size_t in_tri = b * in_d + i;
                    const int8_t xin = dev_read_t5_idx(acc_in, in_tri);
                    const int8_t dlt = dev_gf3_mul(us, xin);
                    t5[lane] = dev_gf3_add(t5[lane], dlt);
                }
                acc_w[bi] = dev_pack_5trits(t5);
            });
        });
        (void)wait_for_completion;
        q.wait_and_throw();
        return true;
    } catch (const sycl::exception& e) {
        std::cerr << "[GF3 SYCL] gf3_hebbian_update_batched_pack5_io_sycl: " << e.what() << std::endl;
        return false;
    } catch (const std::exception& e) {
        std::cerr << "[GF3 SYCL] gf3_hebbian_update_batched_pack5_io_sycl: " << e.what() << std::endl;
        return false;
    } catch (...) {
        std::cerr << "[GF3 SYCL] gf3_hebbian_update_batched_pack5_io_sycl: unknown exception\n";
        return false;
    }
}
