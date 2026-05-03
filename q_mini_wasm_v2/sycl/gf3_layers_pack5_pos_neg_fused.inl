// Fused batched pos+neg tropical (max-plus) linear forward: two queue submits (pos, neg), one wait.

namespace {

/**
 * Per-thread grow-only USM scratch for fused pos/neg forwards.
 * Avoids malloc_device/sycl::free on every layer invocation (important when training.sycl_prereserve_gib pins
 * large separate allocations — extra per-call device allocs can stress the same driver budget).
 */
struct Gf3FusedPosNegPack5TlsScratch {
    size_t cap_in = 0;
    size_t cap_w = 0;
    size_t cap_bias = 0;
    size_t cap_out = 0;
    uint8_t* d_pos_in = nullptr;
    uint8_t* d_neg_in = nullptr;
    uint8_t* d_w = nullptr;
    uint8_t* d_bias = nullptr;
    uint8_t* d_pos_out = nullptr;
    uint8_t* d_neg_out = nullptr;

    static void free_one(sycl::queue& q, uint8_t*& p) {
        if (p) {
            sycl::free(p, q);
            p = nullptr;
        }
    }

    bool ensure(sycl::queue& q, size_t n_in, size_t n_w, size_t bias_b, size_t n_out) {
        if (n_in == 0 || n_w == 0 || bias_b == 0 || n_out == 0) {
            return false;
        }
        if (n_in > cap_in) {
            free_one(q, d_pos_in);
            free_one(q, d_neg_in);
            d_pos_in = sycl::malloc_device<uint8_t>(n_in, q);
            if (!d_pos_in) {
                cap_in = 0;
                return false;
            }
            d_neg_in = sycl::malloc_device<uint8_t>(n_in, q);
            if (!d_neg_in) {
                free_one(q, d_pos_in);
                cap_in = 0;
                return false;
            }
            cap_in = n_in;
        }
        if (n_w > cap_w) {
            free_one(q, d_w);
            d_w = sycl::malloc_device<uint8_t>(n_w, q);
            if (!d_w) {
                cap_w = 0;
                return false;
            }
            cap_w = n_w;
        }
        if (bias_b > cap_bias) {
            free_one(q, d_bias);
            d_bias = sycl::malloc_device<uint8_t>(bias_b, q);
            if (!d_bias) {
                cap_bias = 0;
                return false;
            }
            cap_bias = bias_b;
        }
        if (n_out > cap_out) {
            free_one(q, d_pos_out);
            free_one(q, d_neg_out);
            d_pos_out = sycl::malloc_device<uint8_t>(n_out, q);
            if (!d_pos_out) {
                cap_out = 0;
                return false;
            }
            d_neg_out = sycl::malloc_device<uint8_t>(n_out, q);
            if (!d_neg_out) {
                free_one(q, d_pos_out);
                cap_out = 0;
                return false;
            }
            cap_out = n_out;
        }
        return d_pos_in && d_neg_in && d_w && d_bias && d_pos_out && d_neg_out;
    }
};

thread_local Gf3FusedPosNegPack5TlsScratch g_gf3_fused_pos_neg_tls_scratch;

} // namespace

bool gf3_linear_forward_batched_pos_neg_fused_pack5_io_sycl(
    const std::vector<uint8_t>& batch_pos_in_packed,
    const std::vector<uint8_t>& batch_neg_in_packed,
    const std::vector<uint8_t>& batch_w_packed,
    const std::vector<uint8_t>& batch_bias_packed,
    bool use_bias,
    size_t batch_size,
    size_t input_dim,
    size_t output_dim,
    std::vector<uint8_t>& batch_pos_out_packed,
    std::vector<uint8_t>& batch_neg_out_packed) {
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

    gf3_sycl_submit_log::log_batched_grid_throttled(
        "gf3_linear_forward_pos_neg_fused", B, in_d, out_d, n_in_bytes, n_w_bytes, n_out_bytes, 2u * n_out_bytes,
        "2×submit tropical pos+neg");

    batch_pos_out_packed.assign(n_out_bytes, static_cast<uint8_t>(0));
    batch_neg_out_packed.assign(n_out_bytes, static_cast<uint8_t>(0));

    try {
        std::vector<uint8_t> pad_pos_in;
        std::vector<uint8_t> pad_neg_in;
        std::vector<uint8_t> pad_w;
        std::vector<uint8_t> pad_bias;
        const uint8_t* pos_in_ptr = batch_pos_in_packed.data();
        const uint8_t* neg_in_ptr = batch_neg_in_packed.data();
        const uint8_t* w_ptr = batch_w_packed.data();
        const uint8_t* bias_ptr = batch_bias_packed.data();
        if (batch_pos_in_packed.size() < n_in_bytes) {
            pad_pos_in.assign(batch_pos_in_packed.begin(), batch_pos_in_packed.end());
            pad_pos_in.resize(n_in_bytes, static_cast<uint8_t>(0));
            pos_in_ptr = pad_pos_in.data();
        }
        if (batch_neg_in_packed.size() < n_in_bytes) {
            pad_neg_in.assign(batch_neg_in_packed.begin(), batch_neg_in_packed.end());
            pad_neg_in.resize(n_in_bytes, static_cast<uint8_t>(0));
            neg_in_ptr = pad_neg_in.data();
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

        const size_t bias_dev_bytes = use_bias ? n_bias_bytes : 1u;
        if (!g_gf3_fused_pos_neg_tls_scratch.ensure(q, n_in_bytes, n_w_bytes, bias_dev_bytes, n_out_bytes)) {
            return false;
        }
        uint8_t* d_pos_in = g_gf3_fused_pos_neg_tls_scratch.d_pos_in;
        uint8_t* d_neg_in = g_gf3_fused_pos_neg_tls_scratch.d_neg_in;
        uint8_t* d_w = g_gf3_fused_pos_neg_tls_scratch.d_w;
        uint8_t* d_bias = g_gf3_fused_pos_neg_tls_scratch.d_bias;
        uint8_t* d_pos_out = g_gf3_fused_pos_neg_tls_scratch.d_pos_out;
        uint8_t* d_neg_out = g_gf3_fused_pos_neg_tls_scratch.d_neg_out;

        q.memcpy(d_pos_in, pos_in_ptr, n_in_bytes);
        q.memcpy(d_neg_in, neg_in_ptr, n_in_bytes);
        q.memcpy(d_w, w_ptr, n_w_bytes);
        if (use_bias) {
            q.memcpy(d_bias, bias_ptr, n_bias_bytes);
        } else {
            uint8_t z = 0;
            q.memcpy(d_bias, &z, 1u);
        }
        q.wait_and_throw();

        const uint8_t ub = use_bias ? 1u : 0u;
        const uint8_t* d_w_ro = d_w;
        const uint8_t* d_bias_ro = d_bias;

        q.submit([&](sycl::handler& h) {
            const uint8_t* acc_in = d_pos_in;
            const uint8_t* acc_w = d_w_ro;
            const uint8_t* acc_bias = d_bias_ro;
            uint8_t* acc_out = d_pos_out;
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
                        const int8_t xi = dev_read_t5_idx_bounded_ptr(acc_in, in_tri, n_in_trits);
                        const size_t w_tri = b * w_cells + i * out_d + j;
                        const int8_t wij = dev_read_t5_idx_bounded_ptr(acc_w, w_tri, n_w_trits);
                        const int8_t val = static_cast<int8_t>(xi + wij);
                        max_val = (val > max_val) ? val : max_val;
                    }
                    if (ub) {
                        const size_t bias_tri = b * out_d + j;
                        const int8_t bj = dev_read_t5_idx_bounded_ptr(acc_bias, bias_tri, n_bias_trits);
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
        q.submit([&](sycl::handler& h) {
            const uint8_t* acc_in = d_neg_in;
            const uint8_t* acc_w = d_w_ro;
            const uint8_t* acc_bias = d_bias_ro;
            uint8_t* acc_out = d_neg_out;
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
                        const int8_t xi = dev_read_t5_idx_bounded_ptr(acc_in, in_tri, n_in_trits);
                        const size_t w_tri = b * w_cells + i * out_d + j;
                        const int8_t wij = dev_read_t5_idx_bounded_ptr(acc_w, w_tri, n_w_trits);
                        const int8_t val = static_cast<int8_t>(xi + wij);
                        max_val = (val > max_val) ? val : max_val;
                    }
                    if (ub) {
                        const size_t bias_tri = b * out_d + j;
                        const int8_t bj = dev_read_t5_idx_bounded_ptr(acc_bias, bias_tri, n_bias_trits);
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
        q.wait_and_throw();

        q.memcpy(batch_pos_out_packed.data(), d_pos_out, n_out_bytes);
        q.memcpy(batch_neg_out_packed.data(), d_neg_out, n_out_bytes);
        q.wait_and_throw();
        return true;
    } catch (const sycl::exception& e) {
        std::cerr << "[GF3 SYCL] gf3_linear_forward_batched_pos_neg_fused_pack5_io_sycl: " << e.what() << std::endl;
        return false;
    } catch (const std::exception& e) {
        std::cerr << "[GF3 SYCL] gf3_linear_forward_batched_pos_neg_fused_pack5_io_sycl: " << e.what() << std::endl;
        return false;
    } catch (...) {
        std::cerr << "[GF3 SYCL] gf3_linear_forward_batched_pos_neg_fused_pack5_io_sycl: unknown exception\n";
        return false;
    }
}
