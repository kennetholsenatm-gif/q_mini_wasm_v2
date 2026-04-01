#include "qutrit_tableau.hpp"

#include <algorithm>
#include <format>
#include <stdexcept>

namespace qminiwasm::quantum {

QutritTableau::QutritTableau(int n_qutrits) : n_(n_qutrits) {
  if (n_qutrits < 1) {
    throw std::invalid_argument("n_qutrits must be >= 1");
  }
  // Initialize x_ and z_ as n x n matrices (row-major)
  x_.resize(n_ * n_, 0);
  z_.resize(n_ * n_, 0);
  r_.resize(n_, 0);
  // Initialize to |0⟩^n: stabilizers are Z_i for each qutrit i
  for (int i = 0; i < n_; ++i) {
    z_[i * n_ + i] = 1;
  }
}

QutritTableau QutritTableau::copy() const {
  QutritTableau result(n_);
  result.x_ = x_;
  result.z_ = z_;
  result.r_ = r_;
  return result;
}

void QutritTableau::apply_h3(int q) {
  check_qutrit(q);
  // H₃: X → 2Z, Z → 2X (mod 3)
  // Phase update: r += 2*x*z (mod 3)
  for (int i = 0; i < n_; ++i) {
    int x_val = x_[i * n_ + q];
    int z_val = z_[i * n_ + q];
    r_[i] = (r_[i] + 2 * x_val * z_val) % 3;
    x_[i * n_ + q] = (2 * z_val) % 3;
    z_[i * n_ + q] = (2 * x_val) % 3;
  }
}

void QutritTableau::apply_s3(int q) {
  check_qutrit(q);
  // S₃: X → X + Z (mod 3), Z unchanged
  // Phase update: r += x*z (mod 3)
  for (int i = 0; i < n_; ++i) {
    int x_val = x_[i * n_ + q];
    int z_val = z_[i * n_ + q];
    r_[i] = (r_[i] + x_val * z_val) % 3;
    x_[i * n_ + q] = (x_val + z_val) % 3;
  }
}

void QutritTableau::apply_cz3(int control, int target) {
  if (control == target) {
    throw std::invalid_argument("control and target must differ");
  }
  check_qutrit(control);
  check_qutrit(target);

  // CZ₃: X_c → X_c + Z_t, X_t → X_t + Z_c
  // Phase update: r += 2 * x_c * z_t (mod 3)
  for (int i = 0; i < n_; ++i) {
    int xc = x_[i * n_ + control];
    int zt = z_[i * n_ + target];
    int zc = z_[i * n_ + control];
    int xt = x_[i * n_ + target];

    r_[i] = (r_[i] + 2 * xc * zt) % 3;
    x_[i * n_ + control] = (xc + zt) % 3;
    x_[i * n_ + target] = (xt + zc) % 3;
  }
}

void QutritTableau::apply_x3(int q, int power) {
  check_qutrit(q);
  power = power % 3;
  if (power == 0) return;

  // X^power: adds power * Z to phase
  for (int i = 0; i < n_; ++i) {
    int z_val = z_[i * n_ + q];
    r_[i] = (r_[i] + power * z_val) % 3;
  }
}

void QutritTableau::apply_z3(int q, int power) {
  check_qutrit(q);
  power = power % 3;
  if (power == 0) return;

  // Z^power: adds power * X to phase
  for (int i = 0; i < n_; ++i) {
    int x_val = x_[i * n_ + q];
    r_[i] = (r_[i] + power * x_val) % 3;
  }
}

int QutritTableau::measure(int q) {
  check_qutrit(q);
  // Check if any stabilizer has X component on qutrit q
  bool has_x = false;
  for (int i = 0; i < n_; ++i) {
    if (x_[i * n_ + q] != 0) {
      has_x = true;
      break;
    }
  }
  if (!has_x) {
    return -1;  // Random measurement
  }
  // Find first stabilizer with non-zero X on this qutrit
  for (int i = 0; i < n_; ++i) {
    if (x_[i * n_ + q] != 0) {
      return r_[i] % 3;
    }
  }
  return -1;
}

std::string QutritTableau::stabilizer_string(int row) const {
  if (row < 0 || row >= n_) {
    throw std::out_of_range("row out of range");
  }

  std::string result;
  for (int j = 0; j < n_; ++j) {
    int xb = x_[row * n_ + j] % 3;
    int zb = z_[row * n_ + j] % 3;

    if (xb == 0 && zb == 0) {
      result += "I";
    } else if (xb == 1 && zb == 0) {
      result += "X";
    } else if (xb == 2 && zb == 0) {
      result += "X²";
    } else if (xb == 0 && zb == 1) {
      result += "Z";
    } else if (xb == 0 && zb == 2) {
      result += "Z²";
    } else if (xb == 1 && zb == 1) {
      result += "Y";
    } else if (xb == 1 && zb == 2) {
      result += "XZ²";
    } else if (xb == 2 && zb == 1) {
      result += "X²Z";
    } else {
      result += "X²Z²";
    }

    if (j < n_ - 1) {
      result += " ";
    }
  }

  int r_val = r_[row] % 3;
  if (r_val == 1) {
    return "ω·" + result;
  } else if (r_val == 2) {
    return "ω²·" + result;
  }
  return result;
}

void QutritTableau::check_qutrit(int q) const {
  if (q < 0 || q >= n_) {
    throw std::out_of_range("qutrit index out of range");
  }
}

}  // namespace qminiwasm::quantum