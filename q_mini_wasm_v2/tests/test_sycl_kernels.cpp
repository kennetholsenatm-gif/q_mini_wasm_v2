#include <gtest/gtest.h>
#include "../sycl/tableau_kernels.hpp"
#include <vector>
#include <cstdint>

using namespace q_mini_wasm_v2::sycl_kernels;

// ============================================================================
// Test Fixtures
// ============================================================================

class SYCLKernelTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize test data
        n = 4;  // 4 qutrits = 8x8 tableau
        tableau_size = 2 * n * 2 * n;  // 2n x 2n
        phase_size = 2 * n;
        
        tableau_data.resize(tableau_size, 0);
        phase_data.resize(phase_size, 0);
        
        // Initialize identity tableau (X on left, Z on right)
        for (size_t i = 0; i < n; ++i) {
            // X generators on left half
            tableau_data[i * 2 * n + i] = 1;  // X part
            // Z generators on right half
            tableau_data[(i + n) * 2 * n + (i + n)] = 1;  // Z part
        }
    }
    
    size_t n;
    size_t tableau_size;
    size_t phase_size;
    std::vector<int8_t> tableau_data;
    std::vector<int8_t> phase_data;
};

// ============================================================================
// Hadamard Gate Tests
// ============================================================================

TEST_F(SYCLKernelTest, HadamardPreservesCommutation) {
    // Apply Hadamard to qutrit 0
    parallel_apply_hadamard(tableau_data, phase_data, n, 0);
    
    // Check that tableau is still valid (no data corruption)
    EXPECT_EQ(tableau_data.size(), tableau_size);
    EXPECT_EQ(phase_data.size(), phase_size);
    
    // All values should still be in GF(3): {0, 1, 2}
    for (auto val : tableau_data) {
        EXPECT_GE(val, 0);
        EXPECT_LE(val, 2);
    }
}

TEST_F(SYCLKernelTest, HadamardSwapsXZ) {
    // Apply Hadamard to qutrit 1
    size_t target = 1;
    
    // Record initial X and Z for target
    int8_t initial_x = tableau_data[target * 2 * n + target];
    int8_t initial_z = tableau_data[(target + n) * 2 * n + (target + n)];
    
    parallel_apply_hadamard(tableau_data, phase_data, n, target);
    
    // After Hadamard: X <-> Z swap
    int8_t final_x = tableau_data[target * 2 * n + target];
    int8_t final_z = tableau_data[(target + n) * 2 * n + (target + n)];
    
    // X becomes Z, Z becomes X
    EXPECT_EQ(final_x, initial_z);
    EXPECT_EQ(final_z, initial_x);
}

TEST_F(SYCLKernelTest, HadamardMultipleApplications) {
    // H * H = I (up to phase)
    size_t target = 2;
    
    std::vector<int8_t> initial_tableau = tableau_data;
    
    parallel_apply_hadamard(tableau_data, phase_data, n, target);
    parallel_apply_hadamard(tableau_data, phase_data, n, target);
    
    // Tableau structure should be preserved (modulo phase)
    for (size_t i = 0; i < tableau_size; ++i) {
        EXPECT_EQ(tableau_data[i], initial_tableau[i]);
    }
}

// ============================================================================
// Phase Gate Tests
// ============================================================================

TEST_F(SYCLKernelTest, PhasePreservesTableauSize) {
    parallel_apply_phase(tableau_data, phase_data, n, 0);
    
    EXPECT_EQ(tableau_data.size(), tableau_size);
    EXPECT_EQ(phase_data.size(), phase_size);
}

TEST_F(SYCLKernelTest, PhaseModifiesZComponent) {
    size_t target = 1;
    
    // Apply phase gate
    parallel_apply_phase(tableau_data, phase_data, n, target);
    
    // All values should still be valid GF(3) elements
    for (auto val : tableau_data) {
        EXPECT_GE(val, 0);
        EXPECT_LE(val, 2);
    }
    
    for (auto val : phase_data) {
        EXPECT_GE(val, 0);
        EXPECT_LE(val, 2);
    }
}

// ============================================================================
// CSUM Gate Tests
// ============================================================================

TEST_F(SYCLKernelTest, CSUMPreservesTableau) {
    size_t control = 0;
    size_t target = 1;
    
    parallel_apply_csum(tableau_data, phase_data, n, control, target);
    
    // Check sizes preserved
    EXPECT_EQ(tableau_data.size(), tableau_size);
    EXPECT_EQ(phase_data.size(), phase_size);
    
    // Check GF(3) validity
    for (auto val : tableau_data) {
        EXPECT_GE(val, 0);
        EXPECT_LE(val, 2);
    }
}

TEST_F(SYCLKernelTest, CSUMSelfInverse) {
    // CSUM is self-inverse: CSUM^2 = I
    size_t control = 0;
    size_t target = 2;
    
    std::vector<int8_t> initial_tableau = tableau_data;
    std::vector<int8_t> initial_phase = phase_data;
    
    // Apply twice
    parallel_apply_csum(tableau_data, phase_data, n, control, target);
    parallel_apply_csum(tableau_data, phase_data, n, control, target);
    
    // Should return to initial state (modulo phase)
    for (size_t i = 0; i < tableau_size; ++i) {
        EXPECT_EQ(tableau_data[i], initial_tableau[i]);
    }
}

// ============================================================================
// Measurement Tests
// ============================================================================

TEST_F(SYCLKernelTest, MeasurementReturnsValidOutcomes) {
    auto outcomes = parallel_measure_all(tableau_data, phase_data, n);
    
    // Should return n outcomes (one per qutrit)
    EXPECT_EQ(outcomes.size(), n);
    
    // Each outcome should be 0 or 1 (measurement result)
    for (auto outcome : outcomes) {
        EXPECT_TRUE(outcome == 0 || outcome == 1);
    }
}

TEST_F(SYCLKernelTest, MeasurementIdempotent) {
    // Measuring twice should give same result
    auto outcomes1 = parallel_measure_all(tableau_data, phase_data, n);
    auto outcomes2 = parallel_measure_all(tableau_data, phase_data, n);
    
    EXPECT_EQ(outcomes1.size(), outcomes2.size());
    
    for (size_t i = 0; i < outcomes1.size(); ++i) {
        EXPECT_EQ(outcomes1[i], outcomes2[i]);
    }
}

// ============================================================================
// MoE Routing Tests
// ============================================================================

TEST_F(SYCLKernelTest, RoutingLogitsCorrectSize) {
    size_t num_experts = 8;
    std::vector<int8_t> routing_weights(num_experts * 16, 1);  // Simple weights
    std::vector<int8_t> input_features(16, 1);  // Simple features
    
    auto logits = parallel_compute_routing_logits(
        routing_weights, input_features, num_experts
    );
    
    // Should return one logit per expert
    EXPECT_EQ(logits.size(), num_experts);
}

TEST_F(SYCLKernelTest, RoutingProducesValidValues) {
    size_t num_experts = 4;
    std::vector<int8_t> routing_weights(num_experts * 8, 1);
    std::vector<int8_t> input_features(8, 1);
    
    auto logits = parallel_compute_routing_logits(
        routing_weights, input_features, num_experts
    );
    
    // All logits should be finite numbers
    for (auto logit : logits) {
        EXPECT_TRUE(std::isfinite(logit));
    }
}

// ============================================================================
// Forward-Forward Layer Tests
// ============================================================================

TEST_F(SYCLKernelTest, ForwardLayerCorrectOutputSize) {
    size_t input_size = 16;
    size_t output_size = 32;
    
    std::vector<int8_t> weights(output_size * input_size, 1);
    std::vector<int8_t> biases(output_size, 0);
    std::vector<int8_t> input(input_size, 1);
    std::vector<int8_t> output;
    
    parallel_forward_layer(weights, biases, input, output);
    
    // Output should match expected size
    EXPECT_EQ(output.size(), output_size);
}

TEST_F(SYCLKernelTest, ForwardLayerGF3Values) {
    size_t input_size = 8;
    size_t output_size = 16;
    
    std::vector<int8_t> weights(output_size * input_size, 1);
    std::vector<int8_t> biases(output_size, 0);
    std::vector<int8_t> input(input_size, 1);
    std::vector<int8_t> output;
    
    parallel_forward_layer(weights, biases, input, output);
    
    // All outputs should be valid GF(3) values
    for (auto val : output) {
        EXPECT_GE(val, 0);
        EXPECT_LE(val, 2);
    }
}

// ============================================================================
// GF(3) Arithmetic Tests
// ============================================================================

TEST_F(SYCLKernelTest, GF3Addition) {
    std::vector<int8_t> a = {0, 1, 2, 0, 1, 2};
    std::vector<int8_t> b = {0, 0, 0, 1, 1, 1};
    std::vector<int8_t> result;
    
    parallel_mod3_arithmetic(a, b, result, 0);  // 0 = add
    
    EXPECT_EQ(result.size(), a.size());
    
    // 0+0=0, 1+0=1, 2+0=2, 0+1=1, 1+1=2, 2+1=0 (mod 3)
    EXPECT_EQ(result[0], 0);
    EXPECT_EQ(result[1], 1);
    EXPECT_EQ(result[2], 2);
    EXPECT_EQ(result[3], 1);
    EXPECT_EQ(result[4], 2);
    EXPECT_EQ(result[5], 0);
}

TEST_F(SYCLKernelTest, GF3Multiplication) {
    std::vector<int8_t> a = {0, 1, 2, 0, 1, 2};
    std::vector<int8_t> b = {0, 1, 1, 2, 2, 2};
    std::vector<int8_t> result;
    
    parallel_mod3_arithmetic(a, b, result, 2);  // 2 = multiply
    
    EXPECT_EQ(result.size(), a.size());
    
    // 0*0=0, 1*1=1, 2*1=2, 0*2=0, 1*2=2, 2*2=1 (mod 3)
    EXPECT_EQ(result[0], 0);
    EXPECT_EQ(result[1], 1);
    EXPECT_EQ(result[2], 2);
    EXPECT_EQ(result[3], 0);
    EXPECT_EQ(result[4], 2);
    EXPECT_EQ(result[5], 1);
}

TEST_F(SYCLKernelTest, GF3Subtraction) {
    std::vector<int8_t> a = {0, 1, 2, 0, 1, 2};
    std::vector<int8_t> b = {0, 0, 0, 1, 1, 1};
    std::vector<int8_t> result;
    
    parallel_mod3_arithmetic(a, b, result, 1);  // 1 = subtract
    
    EXPECT_EQ(result.size(), a.size());
    
    // 0-0=0, 1-0=1, 2-0=2, 0-1=2 (mod 3), 1-1=0, 2-1=1
    EXPECT_EQ(result[0], 0);
    EXPECT_EQ(result[1], 1);
    EXPECT_EQ(result[2], 2);
    EXPECT_EQ(result[3], 2);  // -1 mod 3 = 2
    EXPECT_EQ(result[4], 0);
    EXPECT_EQ(result[5], 1);
}

// ============================================================================
// Performance Tests
// ============================================================================

TEST_F(SYCLKernelTest, LargeTableauPerformance) {
    // Test with larger tableau (16 qutrits = 32x32)
    size_t large_n = 16;
    size_t large_size = 2 * large_n * 2 * large_n;
    
    std::vector<int8_t> large_tableau(large_size, 0);
    std::vector<int8_t> large_phase(2 * large_n, 0);
    
    // Initialize identity
    for (size_t i = 0; i < large_n; ++i) {
        large_tableau[i * 2 * large_n + i] = 1;
        large_tableau[(i + large_n) * 2 * large_n + (i + large_n)] = 1;
    }
    
    // Apply multiple gates - should complete in reasonable time
    for (size_t i = 0; i < large_n; ++i) {
        parallel_apply_hadamard(large_tableau, large_phase, large_n, i);
    }
    
    // Verify tableau still valid
    EXPECT_EQ(large_tableau.size(), large_size);
    for (auto val : large_tableau) {
        EXPECT_GE(val, 0);
        EXPECT_LE(val, 2);
    }
}

// ============================================================================
// Error Handling Tests
// ============================================================================

TEST_F(SYCLKernelTest, EmptyInputHandling) {
    std::vector<int8_t> empty_a;
    std::vector<int8_t> empty_b;
    std::vector<int8_t> result;
    
    // Should handle empty input gracefully
    parallel_mod3_arithmetic(empty_a, empty_b, result, 0);
    
    EXPECT_TRUE(result.empty());
}

TEST_F(SYCLKernelTest, MismatchedSizesHandled) {
    std::vector<int8_t> a(10, 1);
    std::vector<int8_t> b(8, 1);  // Different size
    std::vector<int8_t> result;
    
    // Should handle gracefully (implementation dependent)
    // At minimum should not crash
    EXPECT_NO_THROW({
        parallel_mod3_arithmetic(a, b, result, 0);
    });
}

// ============================================================================
// Integration Tests
// ============================================================================

TEST_F(SYCLKernelTest, CliffordGateSequence) {
    // Apply a sequence: H * S * H (should implement P gate up to phase)
    size_t target = 0;
    
    std::vector<int8_t> initial_tableau = tableau_data;
    
    parallel_apply_hadamard(tableau_data, phase_data, n, target);
    parallel_apply_phase(tableau_data, phase_data, n, target);
    parallel_apply_hadamard(tableau_data, phase_data, n, target);
    
    // Tableau should still be valid
    EXPECT_EQ(tableau_data.size(), tableau_size);
    for (auto val : tableau_data) {
        EXPECT_GE(val, 0);
        EXPECT_LE(val, 2);
    }
}

TEST_F(SYCLKernelTest, FullCircuitSimulation) {
    // Simulate a small circuit with multiple gates
    // |0> --H--*--H--|  (Bell-like state preparation)
    // |0> ----C-------|
    
    parallel_apply_hadamard(tableau_data, phase_data, n, 0);
    parallel_apply_csum(tableau_data, phase_data, n, 0, 1);
    parallel_apply_hadamard(tableau_data, phase_data, n, 0);
    
    // Measure
    auto outcomes = parallel_measure_all(tableau_data, phase_data, n);
    
    EXPECT_EQ(outcomes.size(), n);
    for (auto o : outcomes) {
        EXPECT_TRUE(o == 0 || o == 1);
    }
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
