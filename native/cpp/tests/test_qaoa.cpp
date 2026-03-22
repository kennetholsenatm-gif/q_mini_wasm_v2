#include "../qubo/qaoa_small_n.hpp"

#include <cmath>
#include <vector>

bool test_qaoa() {
  const int n = 3;
  std::vector<double> q(static_cast<std::size_t>(n * (n - 1) / 2), 0.0);
  q[0] = -1.0;
  std::vector<int> bits = {1, -1, 1};
  const double e = qminiwasm::qubo::qubo_energy(q, bits);
  // Edge (0,1): q01=-1, z0=1, z1=-1 => (-1)*(1)*(-1) = +1
  if (std::fabs(e - 1.0) > 1e-9) {
    return false;
  }
  double g = 0;
  double b = 0;
  double en = 0;
  if (!qminiwasm::qubo::optimize_qaoa_p1_cobyla(q, n, g, b, en)) {
    return false;
  }
  return true;
}
