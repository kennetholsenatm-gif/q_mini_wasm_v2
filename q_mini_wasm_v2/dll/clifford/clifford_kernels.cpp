// q_mini_wasm_v2/dll/clifford/clifford_kernels.cpp
#include <cstdint>
#include <cstddef>

namespace q_mini_wasm_v2::dll::clifford::kernels {

// GF(3) arithmetic helpers
static inline uint8_t gf3_add(uint8_t a, uint8_t b) {
    return ((a % 3) + (b % 3)) % 3;
}

static inline uint8_t gf3_mul(uint8_t a, uint8_t b) {
    return ((a % 3) * (b % 3)) % 3;
}

static inline uint8_t gf3_neg(uint8_t a) {
    return (3 - (a % 3)) % 3;
}

// Helper: Get tableau entry at (row, col)
static inline uint8_t tableau_get(const uint8_t* tableau, size_t n, size_t row, size_t col) {
    return tableau[row * 2 * n + col];
}

// Helper: Set tableau entry at (row, col)
static inline void tableau_set(uint8_t* tableau, size_t n, size_t row, size_t col, uint8_t val) {
    tableau[row * 2 * n + col] = val % 3;
}

/**
 * Apply qutrit Hadamard gate H (3D discrete Fourier transform)
 * Maps X ↔ Z, Z ↔ X² for the target qutrit
 * 
 * Tableau layout: 2n x 2n matrix where:
 * - Columns [0, n-1]: X stabilizers
 * - Columns [n, 2n-1]: Z stabilizers
 * - Rows [0, n-1]: Destabilizers
 * - Rows [n, 2n-1]: Stabilizers
 */
void apply_hadamard_kernel(uint8_t* tableau, uint8_t* phase, size_t n, size_t target) {
    for (size_t i = 0; i < 2 * n; i++) {
        uint8_t x = tableau_get(tableau, n, i, target);
        uint8_t z = tableau_get(tableau, n, i, n + target);
        
        // H gate: X ↔ Z, Z ↔ X² (X squared = -X mod 3)
        tableau_set(tableau, n, i, target, z);
        tableau_set(tableau, n, i, n + target, gf3_mul(2, x));
    }
    
    // Update phase for H gate: |k> -> (|0> + ω^k|1> + ω^{2k}|2>)/√3
    // where ω = e^{2πi/3} is the primitive third root of unity
    // For stabilizer formalism, phases accumulate through row operations
    (void)phase;  // Phase handled implicitly through tableau structure
}

/**
 * Apply qutrit Phase gate S
 * Maps X → XZ, Z → Z
 * 
 * S is the diagonal matrix diag(1, ω, ω²) where ω³ = 1
 */
void apply_phase_kernel(uint8_t* tableau, uint8_t* phase, size_t n, size_t target) {
    for (size_t i = 0; i < 2 * n; i++) {
        uint8_t x = tableau_get(tableau, n, i, target);
        uint8_t z = tableau_get(tableau, n, i, n + target);
        
        // S gate: X → XZ, Z → Z
        // The Z component becomes Z + X (mod 3)
        tableau_set(tableau, n, i, target, x);
        tableau_set(tableau, n, i, n + target, gf3_add(z, x));
    }
    
    // Phase updates for S gate: adds x*z to phase (commutation relation)
    for (size_t i = 0; i < 2 * n; i++) {
        uint8_t x = tableau_get(tableau, n, i, target);
        uint8_t z = tableau_get(tableau, n, i, n + target);
        phase[i] = gf3_add(phase[i], gf3_mul(x, z));
    }
}

/**
 * Apply qutrit Controlled-SUM gate (CSUM) - qutrit CNOT equivalent
 * Maps X_control → X_control X_target, Z_target → Z_control² Z_target
 * 
 * CSUM |a,b> = |a, a+b mod 3>
 * Acts as addition mod 3 on the computational basis
 */
void apply_csum_kernel(uint8_t* tableau, uint8_t* phase, size_t n, size_t control, size_t target) {
    for (size_t i = 0; i < 2 * n; i++) {
        uint8_t xc = tableau_get(tableau, n, i, control);
        uint8_t zc = tableau_get(tableau, n, i, n + control);
        uint8_t xt = tableau_get(tableau, n, i, target);
        uint8_t zt = tableau_get(tableau, n, i, n + target);
        
        // CSUM gate action on Pauli operators:
        // X_control → X_control X_target (adds X target)
        // Z_target → Z_control² Z_target = Z_control^{-1} Z_target
        tableau_set(tableau, n, i, target, gf3_add(xt, xc));           // X_t += X_c
        tableau_set(tableau, n, i, n + control, gf3_add(zc, gf3_mul(2, zt)));  // Z_c += 2*Z_t
    }
    
    // Phase update: CSUM introduces phase factors from commutation
    // For each row, phase += x_control * z_target (before update)
    for (size_t i = 0; i < 2 * n; i++) {
        uint8_t xc = tableau_get(tableau, n, i, control);
        uint8_t zt = tableau_get(tableau, n, i, n + target);
        phase[i] = gf3_add(phase[i], gf3_mul(xc, zt));
    }
}

} // namespace q_mini_wasm_v2::dll::clifford::kernels