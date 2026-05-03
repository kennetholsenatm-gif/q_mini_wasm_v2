// GF(3) linear layer SYCL paths (tropical forward, Hebbian update).
#include "gf3_layers_sycl.hpp"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <exception>
#include <iostream>
#include <mutex>
#include <string>

#include "../core/ternary/packing.hpp"

#if defined(USE_SYCL) && USE_SYCL

#include <sycl/sycl.hpp>

// Global kernel name (complete type). q_training links core with /WHOLEARCHIVE so SPIR-V is not dropped from the DLL.
struct QminiGf3GenerateNegativeKernelName {};

namespace {
std::atomic<int32_t> g_gf3_sycl_runtime_gpu_device_index{-1};
std::atomic<uint64_t> g_gf3_sycl_runtime_min_weight_cells{0};
std::atomic<uint8_t> g_gf3_sycl_submit_grid_log{0};
} // namespace

namespace q_mini_wasm_v2::sycl_kernels {

void gf3_sycl_apply_runtime_host_config(
    int32_t gpu_device_index, uint64_t min_weight_cells, bool submit_grid_log_to_stderr) noexcept {
    g_gf3_sycl_runtime_gpu_device_index.store(gpu_device_index, std::memory_order_relaxed);
    /** Retained for config parity; GF3 SYCL layer forwards do not skip the GPU based on this floor. */
    g_gf3_sycl_runtime_min_weight_cells.store(min_weight_cells, std::memory_order_relaxed);
    g_gf3_sycl_submit_grid_log.store(submit_grid_log_to_stderr ? uint8_t{1} : uint8_t{0}, std::memory_order_relaxed);
}

namespace {

/** When multiple SYCL GPUs exist (common on laptops: iGPU + Arc), pick the one with more CUs — not default_selector (often Iris). */
static size_t pick_preferred_gpu_index(const std::vector<sycl::device>& gpus) {
    if (gpus.size() <= 1) {
        return 0;
    }
    size_t best_i = 0;
    uint32_t best_cu = 0;
    for (size_t i = 0; i < gpus.size(); ++i) {
        uint32_t cu = 0;
        try {
            cu = gpus[i].get_info<sycl::info::device::max_compute_units>();
        } catch (...) {
        }
        // Strict `>` so equal CU counts keep the first enumerated device. Intel stacks sometimes list the same
        // iGPU twice; the later duplicate can fail kernels while [0] works (SYCL negative probe -19).
        if (cu > best_cu) {
            best_cu = cu;
            best_i = i;
        }
    }
    return best_i;
}

static void log_sycl_gpu_choices(const std::vector<sycl::device>& gpus) {
    if (gpus.empty()) {
        std::cerr << "[GF3 SYCL] No SYCL GPU devices enumerated. Native GPU-mandatory training needs a visible SYCL GPU "
                     "(CPU SYCL is not a substitute for this stack).\n"
                  << "[GF3 SYCL] Note: NVIDIA/AMD dGPUs are not visible to Intel oneAPI SYCL unless you use a build/runtime that exposes them.\n";
        return;
    }
    std::cerr << "[GF3 SYCL] Visible GPU devices (pick with training.sycl_gpu_device_index in TOML, 0.."
              << (gpus.size() - 1) << "):\n";
    for (size_t i = 0; i < gpus.size(); ++i) {
        try {
            const std::string name = gpus[i].get_info<sycl::info::device::name>();
            uint32_t cu = 0;
            try {
                cu = gpus[i].get_info<sycl::info::device::max_compute_units>();
            } catch (...) {
            }
            std::cerr << "  [" << i << "] " << name << " (max_compute_units=" << cu << ")\n";
        } catch (...) {
            std::cerr << "  [" << i << "] <device info unavailable>\n";
        }
    }
}

/** Resolve SYCL device for autoscale probe (mirrors gf3_layers_target_device index rules; no static cache). */
static bool resolve_gpu_device_for_probe(int32_t configured_index, sycl::device& out_dev, std::string& err_out) {
    try {
        using namespace sycl;
        std::vector<device> gpus = device::get_devices(info::device_type::gpu);
        if (!gpus.empty()) {
            if (configured_index >= 0) {
                if (static_cast<size_t>(configured_index) < gpus.size()) {
                    out_dev = gpus[static_cast<size_t>(configured_index)];
                    return true;
                }
                const size_t chosen = pick_preferred_gpu_index(gpus);
                out_dev = gpus[chosen];
                return true;
            }
            const size_t chosen = pick_preferred_gpu_index(gpus);
            out_dev = gpus[chosen];
            return true;
        }
        try {
            queue probe{gpu_selector_v};
            out_dev = probe.get_device();
            return true;
        } catch (const std::exception& ex) {
            err_out = ex.what() ? ex.what() : "gpu_selector_v failed";
        } catch (...) {
            err_out = "gpu_selector_v failed (unknown)";
        }
        try {
            queue probe{default_selector_v};
            out_dev = probe.get_device();
            return true;
        } catch (const std::exception& ex) {
            err_out = ex.what() ? ex.what() : "default_selector_v failed";
            return false;
        }
    } catch (const std::exception& ex) {
        err_out = ex.what() ? ex.what() : "resolve_gpu_device_for_probe failed";
        return false;
    } catch (...) {
        err_out = "resolve_gpu_device_for_probe unknown exception";
        return false;
    }
}

const sycl::device& gf3_layers_target_device() {
    static sycl::device g_dev{};
    static std::once_flag g_once;
    std::call_once(g_once, [] {
        using namespace sycl;
        std::vector<device> gpus = device::get_devices(info::device_type::gpu);
        log_sycl_gpu_choices(gpus);

        const int32_t configured = g_gf3_sycl_runtime_gpu_device_index.load(std::memory_order_relaxed);
        size_t chosen = 0;
        if (configured >= 0) {
            if (!gpus.empty() && static_cast<size_t>(configured) < gpus.size()) {
                chosen = static_cast<size_t>(configured);
                try {
                    const std::string name = gpus[chosen].get_info<info::device::name>();
                    std::cerr << "[GF3 SYCL] training.sycl_gpu_device_index=" << configured << " -> " << name << "\n";
                } catch (...) {
                    std::cerr << "[GF3 SYCL] training.sycl_gpu_device_index=" << configured << "\n";
                }
                g_dev = gpus[chosen];
                return;
            }
            if (!gpus.empty()) {
                std::cerr << "[GF3 SYCL] WARN: training.sycl_gpu_device_index out of range; using auto GPU pick.\n";
            } else {
                std::cerr << "[GF3 SYCL] WARN: training.sycl_gpu_device_index set but no GPUs enumerated; using "
                             "gpu_selector_v / default_selector_v.\n";
            }
        }

        if (!gpus.empty()) {
            chosen = pick_preferred_gpu_index(gpus);
            try {
                const std::string name = gpus[chosen].get_info<info::device::name>();
                std::cerr << "[GF3 SYCL] Auto-selected GPU [" << chosen << "]: " << name << "\n";
            } catch (...) {
                std::cerr << "[GF3 SYCL] Auto-selected GPU index " << chosen << "\n";
            }
            g_dev = gpus[chosen];
            return;
        }

        try {
            queue probe{gpu_selector_v};
            std::cerr << "[GF3 SYCL] Falling back to gpu_selector_v: "
                      << probe.get_device().get_info<info::device::name>() << "\n";
            g_dev = probe.get_device();
            return;
        } catch (const std::exception& ex) {
            std::cerr << "[GF3 SYCL] gpu_selector_v failed (" << ex.what() << "); using default_selector_v\n";
        } catch (...) {
            std::cerr << "[GF3 SYCL] gpu_selector_v failed; using default_selector_v\n";
        }
        queue probe{default_selector_v};
        g_dev = probe.get_device();
    });
    return g_dev;
}

} // namespace

/**
 * One SYCL queue per OS thread. Training runs FF on many OpenMP threads; a single shared queue + mutex made
 * almost all threads block on the host while the GPU went idle between submits (visible as CPU/GPU % collapsing).
 * Multiple queues to the same device are the supported way to submit in parallel from multiple threads.
 * Exported for InitSession probe TU linked into q_training.dll (device code must live in the DLL image on Windows).
 */
sycl::queue& gf3_layers_queue_for_thread() {
    thread_local sycl::queue q{gf3_layers_target_device()};
    return q;
}

bool gf3_sycl_forward_enabled_for_shape(size_t input_dim, size_t output_dim) noexcept {
    return input_dim >= 1 && output_dim >= 1;
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

/** TritPack5 read (matches @c q::ternary::read_trit_t5_at) for device kernels. */
static inline int8_t dev_read_trit_t5_at(const uint8_t* p, size_t tri) {
    const size_t bi = tri / 5u;
    const unsigned lane = static_cast<unsigned>(tri % 5u);
    uint8_t x = p[bi];
    for (unsigned k = 0; k < lane; ++k) {
        x = static_cast<uint8_t>(x / 3u);
    }
    const uint8_t u = static_cast<uint8_t>(x % 3u);
    return static_cast<int8_t>(static_cast<int>(u) - 1);
}

static bool gf3_sycl_batched_shape_ok(size_t batch_size, size_t input_dim, size_t output_dim) noexcept {
    return batch_size >= 1 && gf3_sycl_forward_enabled_for_shape(input_dim, output_dim);
}

namespace gf3_sycl_submit_log {

std::atomic<uint64_t> g_gf3_batched_grid_log_seq{0};

/** When @c training.gf3_sycl_submit_grid_log is true (applied via @c gf3_sycl_apply_runtime_host_config). */
inline bool runtime_grid_log_enabled() noexcept {
    return g_gf3_sycl_submit_grid_log.load(std::memory_order_relaxed) != 0;
}

void log_batched_grid_throttled(const char* op, size_t B, size_t in_d, size_t out_d, size_t n_in_bytes,
                                size_t n_w_bytes, size_t n_out_bytes, size_t parallel_for_1d,
                                const char* grid_axis_note) {
    if (!runtime_grid_log_enabled()) {
        return;
    }
    const uint64_t i = g_gf3_batched_grid_log_seq.fetch_add(1, std::memory_order_relaxed);
    if (i >= 32u && ((i % 512u) != 0u)) {
        return;
    }
    std::cerr << "[GF3 SYCL grid] " << op << " B=" << B << " in_d=" << in_d << " out_d=" << out_d
              << " pack5_bytes(in/w/out)=" << n_in_bytes << "/" << n_w_bytes << "/" << n_out_bytes
              << " parallel_for<1>(" << parallel_for_1d << ") " << grid_axis_note << std::endl;
}

} // namespace gf3_sycl_submit_log

#include "gf3_layers_pack5_impl.inl"
#include "gf3_layers_pack5_pos_neg_fused.inl"

bool gf3_tropical_linear_forward_sycl(
    const std::vector<uint8_t>& input_packed,
    const std::vector<uint8_t>& weights_packed,
    const std::vector<uint8_t>& bias_packed,
    bool use_bias,
    size_t input_dim,
    size_t output_dim,
    std::vector<uint8_t>& out_packed) {
    static const std::vector<uint8_t> k_dummy_bias{0};
    return gf3_tropical_linear_forward_batched_pack5_io_sycl(
        input_packed,
        weights_packed,
        use_bias ? bias_packed : k_dummy_bias,
        use_bias,
        1,
        input_dim,
        output_dim,
        out_packed,
        true);
}

bool gf3_hebbian_update_sycl(
    const std::vector<uint8_t>& input_packed,
    int32_t goodness_delta,
    int8_t learning_rate,
    size_t input_dim,
    size_t output_dim,
    std::vector<uint8_t>& weights_packed) {
    if (!gf3_sycl_forward_enabled_for_shape(input_dim, output_dim)) {
        return false;
    }
    if (goodness_delta == 0 || input_packed.empty()) {
        return false;
    }
    const size_t in_need = (input_dim + 4u) / 5u;
    if (input_packed.size() < in_need) {
        return false;
    }
    if (weights_packed.size() < (input_dim * output_dim + 4u) / 5u) {
        return false;
    }
    const std::vector<int32_t> delta{goodness_delta};
    return gf3_hebbian_update_batched_pack5_io_sycl(
        input_packed, delta, learning_rate, 1, input_dim, output_dim, weights_packed, true);
}

bool gf3_generate_negative_sycl(
    const std::vector<uint8_t>& positive_packed,
    size_t trit_count,
    uint32_t corruption_seed,
    std::vector<uint8_t>& negative_packed
) {
    if (trit_count == 0) {
        return false;
    }
    const size_t n_bytes = (trit_count + 4u) / 5u;
    if (positive_packed.size() < n_bytes) {
        return false;
    }
    negative_packed.assign(positive_packed.begin(), positive_packed.begin() + static_cast<std::ptrdiff_t>(n_bytes));
    try {
        std::vector<uint8_t> pos_host(negative_packed.begin(), negative_packed.end());

        sycl::queue& q = gf3_layers_queue_for_thread();
        sycl::buffer<uint8_t, 1> buf_in(pos_host.data(), sycl::range<1>(pos_host.size()));
        sycl::buffer<uint8_t, 1> buf_out(negative_packed.data(), sycl::range<1>(negative_packed.size()));
        const size_t n_trits = trit_count;
        const uint32_t seed = corruption_seed;
        const uint32_t lane = (seed % 10u);
        q.submit([&](sycl::handler& h) {
            auto acc_in = buf_in.get_access<sycl::access::mode::read>(h);
            auto acc_out = buf_out.get_access<sycl::access::mode::write>(h);
            h.parallel_for<QminiGf3GenerateNegativeKernelName>(sycl::range<1>(n_bytes), [=](sycl::id<1> id) {
                const size_t bi = id[0];
                uint8_t x = acc_in[bi];
                int8_t t5[5];
                for (int k = 0; k < 5; ++k) {
                    const uint8_t u = static_cast<uint8_t>(x % 3u);
                    x = static_cast<uint8_t>(x / 3u);
                    t5[k] = static_cast<int8_t>(static_cast<int>(u) - 1);
                }
                for (unsigned lane_k = 0; lane_k < 5u; ++lane_k) {
                    const size_t T = bi * 5u + static_cast<size_t>(lane_k);
                    if (T >= n_trits) {
                        continue;
                    }
                    int8_t v = t5[lane_k];
                    if ((static_cast<uint32_t>(T) % 10u) == lane) {
                        if (v == 0) {
                            v = ((seed >> 1) & 1u) ? static_cast<int8_t>(1) : static_cast<int8_t>(-1);
                        } else {
                            v = static_cast<int8_t>(-v);
                        }
                    }
                    t5[lane_k] = v;
                }
                acc_out[bi] = dev_pack_5trits(t5);
            });
        });
        q.wait_and_throw();

        const size_t force_idx = static_cast<size_t>(seed % static_cast<uint32_t>(n_trits));
        const int8_t p = q::ternary::read_trit_t5_at(positive_packed.data(), force_idx);
        int8_t n = q::ternary::read_trit_t5_at(negative_packed.data(), force_idx);
        if (n == p) {
            const int8_t v = p;
            const int8_t fixed = (v == 0) ? static_cast<int8_t>(1) : static_cast<int8_t>(-v);
            const size_t bi = force_idx / 5u;
            const unsigned lane_fix = static_cast<unsigned>(force_idx % 5u);
            int8_t t5[5];
            q::ternary::unpack_5trits(negative_packed[bi], t5);
            t5[lane_fix] = fixed;
            negative_packed[bi] = q::ternary::pack_5trits(t5);
        }
        return true;
    } catch (const sycl::exception& e) {
        std::cerr << "[GF3 SYCL] gf3_generate_negative_sycl: " << e.what() << std::endl;
        negative_packed.clear();
        return false;
    } catch (const std::exception& e) {
        std::cerr << "[GF3 SYCL] gf3_generate_negative_sycl: " << e.what() << std::endl;
        negative_packed.clear();
        return false;
    } catch (...) {
        std::cerr << "[GF3 SYCL] gf3_generate_negative_sycl: unknown exception\n";
        negative_packed.clear();
        return false;
    }
}

int gf3_sycl_probe_device_resources(
    int32_t gpu_device_index,
    uint64_t* global_mem_bytes_out,
    uint32_t* is_gpu_u32_out,
    char* name_utf8_out,
    size_t name_cap,
    char* err_utf8_out,
    size_t err_cap) noexcept {
    auto write_err = [&](const char* msg) {
        if (err_utf8_out && err_cap > 0) {
            std::snprintf(err_utf8_out, err_cap, "%s", msg ? msg : "");
        }
    };
    if (!global_mem_bytes_out || !is_gpu_u32_out) {
        write_err("null required out pointer");
        return -1;
    }
    *global_mem_bytes_out = 0;
    *is_gpu_u32_out = 0;
    if (name_utf8_out && name_cap > 0) {
        name_utf8_out[0] = '\0';
    }
    try {
        sycl::device dev;
        std::string err;
        if (!resolve_gpu_device_for_probe(gpu_device_index, dev, err)) {
            write_err(err.c_str());
            return -3;
        }
        const uint64_t gmem = dev.get_info<sycl::info::device::global_mem_size>();
        *global_mem_bytes_out = gmem;
        *is_gpu_u32_out = dev.is_gpu() ? 1u : 0u;
        if (name_utf8_out && name_cap > 0) {
            try {
                const std::string name = dev.get_info<sycl::info::device::name>();
                std::snprintf(name_utf8_out, name_cap, "%s", name.c_str());
            } catch (...) {
                name_utf8_out[0] = '\0';
            }
        }
        return 0;
    } catch (const std::exception& ex) {
        write_err(ex.what() ? ex.what() : "gf3_sycl_probe_device_resources exception");
        return -3;
    } catch (...) {
        write_err("gf3_sycl_probe_device_resources unknown exception");
        return -3;
    }
}

} // namespace q_mini_wasm_v2::sycl_kernels

#else

namespace q_mini_wasm_v2::sycl_kernels {

void gf3_sycl_apply_runtime_host_config(int32_t, uint64_t, bool) noexcept {}

int gf3_sycl_probe_device_resources(
    int32_t,
    uint64_t* global_mem_bytes_out,
    uint32_t* is_gpu_u32_out,
    char*,
    size_t,
    char* err_utf8_out,
    size_t err_cap) noexcept {
    if (global_mem_bytes_out) {
        *global_mem_bytes_out = 0;
    }
    if (is_gpu_u32_out) {
        *is_gpu_u32_out = 0;
    }
    if (err_utf8_out && err_cap > 0) {
        std::snprintf(err_utf8_out, err_cap, "%s", "USE_SYCL off");
    }
    return -2;
}

bool gf3_sycl_forward_enabled_for_shape(size_t, size_t) noexcept {
    return false;
}

bool gf3_tropical_linear_forward_sycl(
    const std::vector<uint8_t>&,
    const std::vector<uint8_t>&,
    const std::vector<uint8_t>&,
    bool,
    size_t,
    size_t,
    std::vector<uint8_t>&) {
    return false;
}

bool gf3_hebbian_update_sycl(
    const std::vector<uint8_t>&, int32_t, int8_t, size_t, size_t, std::vector<uint8_t>&) {
    return false;
}

bool gf3_generate_negative_sycl(
    const std::vector<uint8_t>&, size_t, uint32_t, std::vector<uint8_t>&) {
    return false;
}

bool gf3_tropical_linear_forward_batched_pack5_io_sycl(
    const std::vector<uint8_t>&,
    const std::vector<uint8_t>&,
    const std::vector<uint8_t>&,
    bool,
    size_t,
    size_t,
    size_t,
    std::vector<uint8_t>&,
    bool) {
    return false;
}

bool gf3_hebbian_update_batched_pack5_io_sycl(
    const std::vector<uint8_t>&,
    const std::vector<int32_t>&,
    int8_t,
    size_t,
    size_t,
    size_t,
    std::vector<uint8_t>&,
    bool) {
    return false;
}

bool gf3_linear_forward_batched_pos_neg_fused_pack5_io_sycl(
    const std::vector<uint8_t>&,
    const std::vector<uint8_t>&,
    const std::vector<uint8_t>&,
    const std::vector<uint8_t>&,
    bool,
    size_t,
    size_t,
    size_t,
    std::vector<uint8_t>&,
    std::vector<uint8_t>&) {
    return false;
}

} // namespace q_mini_wasm_v2::sycl_kernels

#endif
