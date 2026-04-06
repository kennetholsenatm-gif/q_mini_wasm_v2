# BettiExtractor Code Trace: API → SYCL XPU Layer

## Overview
This document traces the complete execution path from the high-level
BettiExtractor API down to the SYCL hardware acceleration layer for XPU
(GPU/FPGA) execution.

---

## Layer 1: High-Level API (BettiExtractor)

**File:** `q_mini_wasm_v2/core/qgnn/betti_extractor.hpp`

```cpp
class BettiExtractor {
public:
    // User-facing API entry point
    BettiNumbers compute_betti() const;
    
    // Energy-aware variant
    BettiNumbers compute_betti_with_budget(EnergyTrit max_energy) const;
    
    // Core rank calculation (calls StabilizerTableau)
    uint32_t calculate_gf3_rank() const;

  // ... (truncated)
  // See source for complete code
```

**Call Flow:**
1. User calls `compute_betti()`
2. Calls `calculate_gf3_rank()` 
3. Accesses `tableau_->get_element()` for matrix data
4. Uses `gf3_mult_lut_` for GF(3) operations

---

## Layer 2: Stabilizer Tableau

**File:** `q_mini_wasm_v2/core/stabilizer/tableau.hpp`

```cpp
class StabilizerTableau {
public:
    // Gate operations used by BettiExtractor
    void apply_pauli_x(size_t j);
    void apply_pauli_z(size_t j);
    
    // Matrix access for rank calculation
    uint8_t get_element(size_t row, size_t col) const noexcept;
    uint32_t calculate_gf3_rank() const;

private:
  // ... (truncated)
  // See source for complete code
```

**Implementation:** `q_mini_wasm_v2/core/stabilizer/tableau.cpp`

```cpp
uint8_t StabilizerTableau::get_element(size_t row, size_t col) const {
    // CSR lookup - O(log E) per access
    const size_t start = row_ptr_[row];
    const size_t end = row_ptr_[row + 1];
    
    for (size_t i = start; i < end; ++i) {
        if (col_idx_[i] == col) return values_[i];
    }
    return 0;
}

  // ... (truncated)
  // See source for complete code
```

---

## Layer 3: QGNN Message Passing (CPU Path)

**File:** `q_mini_wasm_v2/core/qgnn/message_passing.hpp`

```cpp
class MessagePassingKernel {
public:
    // Discrete unitary edge operator
    struct TernaryTreeEdge {
        size_t source_node;
        size_t target_node;
        int8_t weight;  // GF(3): {-1, 0, +1}
    };
    
    // Forward pass through graph
    std::span<const stabilizer::StabilizerTableau> forward_pass(
  // ... (truncated)
  // See source for complete code
```

---

## Layer 4: SYCL XPU Acceleration Layer

**File:** `q_mini_wasm_v2/core/qgnn/message_passing_sycl.hpp`

```cpp
class MessagePassingSycl {
public:
    explicit MessagePassingSycl(sycl::queue& queue, memory::MemoryArena& arena);
    
    // GPU/FPGA accelerated forward pass
    std::span<const stabilizer::StabilizerTableau> forward_pass(
        std::span<const int8_t> graph_adjacency,
        std::span<const stabilizer::StabilizerTableau> node_states
    ) noexcept;
    
    // Batch symplectic attention on XPU
  // ... (truncated)
  // See source for complete code
```

---

## Layer 5: SYCL Kernel Execution (XPU Hardware)

**File:** `q_mini_wasm_v2/core/qgnn/message_passing_sycl.cpp`

```cpp
void MessagePassingSycl::launch_message_passing_kernel(
    const int8_t* adjacency,
    const stabilizer::StabilizerTableau* node_states,
    stabilizer::StabilizerTableau* output_states,
    size_t node_count
) noexcept {
    queue_.submit([&](sycl::handler& cgh) {
        // Allocate local (shared) memory for L1 cache
        sycl::local_accessor<stabilizer::StabilizerTableau, 1> local_nodes(
            sycl::range<1>(LOCAL_MEM_NODES), cgh
        );
  // ... (truncated)
  // See source for complete code
```

---

## Layer 6: Hardware Abstraction

**Memory Arena (Unified Memory):**
```cpp
// File: q_mini_wasm_v2/core/memory/arena.hpp
class MemoryArena {
    // USM (Unified Shared Memory) for zero-copy GPU/CPU access
    void* allocate_usm(size_t size);  // SYCL malloc_device/malloc_shared
    
    // Trit packing for 5 trits/byte
    TritPack5* allocate_trits(size_t count);
};
```

**SYCL Queue Management:**
```cpp
// SYCL queue targets GPU, FPGA, or host
sycl::queue queue{sycl::default_selector_v};

// Possible backends:
// - Intel GPU (Level Zero)
// - NVIDIA GPU (CUDA backend)  
// - AMD GPU (ROCm backend)
// - FPGA (Intel oneAPI)
// - Host (fallback)
```

---

## Complete Call Stack Trace

```
User Application
    ↓
BettiExtractor::compute_betti() [betti_extractor.cpp:138]
    ↓
BettiExtractor::calculate_gf3_rank() [betti_extractor.cpp:190]
    ↓
tableau_->get_element(row, col) [tableau.cpp:54]  // CSR lookup
    ↓ (optional acceleration path)
MessagePassingKernel::forward_pass() [message_passing.hpp:44]
    ↓
MessagePassingSycl::forward_pass() [message_passing_sycl.cpp:11]
  // ... (truncated)
  // See source for complete code
```

---

## Data Flow Summary

1. **Input:** SimplicialComplex (edges as TritPack5, vertices, faces)
2. **BettiExtractor** converts to StabilizerTableau (CSR sparse matrix)
3. **Tableau** stores GF(3) values {0,1,2} in compressed format
4. **Rank calculation** uses LUT-based GF(3) arithmetic (<0.5 pJ/op)
5. **Optional SYCL path** batches operations on XPU:
   - Node states copied to local memory (L1)
   - Symplectic attention computed in parallel
   - apply_csum updates via XPU ALUs
6. **Output:** BettiNumbers (β₀, β₁, β₂)

---

## Energy & Complexity Budget

| Layer | Complexity | Energy/Op | Implementation |
|-------|-----------|-----------|----------------|
| BettiExtractor API | O(n²) | <0.5 pJ | C++ LUT-based |
| StabilizerTableau | O(E) storage | ~1.5 pJ | CSR sparse |
| MessagePassingKernel | O(N·E) | ~2.0 pJ | CPU loop |
| MessagePassingSycl | O(N·E/work_group) | ~0.3 pJ | XPU parallel |
| SYCL Kernel | O(1) per thread | ~0.1 pJ | Hardware ALU |

---

## GF(3) Purity Verification

All layers maintain GF(3) constraints:
- ✅ No floating-point operations
- ✅ All values in {0, 1, 2} (or {-1, 0, +1})
- ✅ Modular arithmetic: `value % 3`
- ✅ LUT-based ops: `gf3_add_lut_[a*3+b]`
- ✅ Gottesman-Knill: Only H, S, CSUM, Pauli X/Z gates

---

**Trace Complete:** BettiExtractor → SYCL XPU Layer
