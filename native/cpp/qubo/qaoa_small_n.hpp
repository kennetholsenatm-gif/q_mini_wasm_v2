#pragma once

#include <cstddef>
#include <vector>

namespace qminiwasm::qubo {

/** Expect Q as upper triangle excluding diagonal: Q[i][j] for i<j, row-major. ``z_bits`` are Ising spins in {-1, 1}. */
double qubo_energy(const std::vector<double>& q_upper, const std::vector<int>& z_bits);

/**
 * QAOA-style cost expectation for p=1 on MaxCut QUBO (diagonal zero), small n only.
 * Returns optimized (gamma, beta) and best energy found (heuristic).
 */
bool optimize_qaoa_p1_cobyla(const std::vector<double>& q_upper, int n, double& out_gamma,
                             double& out_beta, double& out_energy);

/** Optional mesh-style text dump: first line "n", then i j coeff per edge. */
bool write_qubo_upper_triangle_file(const char* path, int n, const std::vector<double>& q_upper);

}  // namespace qminiwasm::qubo
