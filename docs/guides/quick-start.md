# Quick Start Guide

This guide will help you get started with the q_mini_wasm_v2 framework in minutes.

## Prerequisites

Before you begin, ensure you have:

- **C++17 Compiler**: GCC 7+, Clang 5+, or MSVC 2017+
- **CMake**: Version 3.14 or higher
- **Git**: For cloning the repository

## Installation

### 1. Clone the Repository

```bash
git clone https://github.com/kennetholsenatm-gif/q_mini_wasm_v2.git
cd q_mini_wasm_v2/q_mini_wasm_v2
```

### 2. Build the Framework

#### Option A: Basic Build (Recommended for Beginners)

```bash
mkdir build
cd build
cmake ..
cmake --build .
```

#### Option B: Build with Tests

```bash
mkdir build
cd build
cmake -DBUILD_TESTS=ON ..
cmake --build .
```

#### Option C: Build with SYCL Acceleration (Advanced)

```bash
mkdir build
cd build
cmake -DUSE_SYCL=ON ..
cmake --build .
```

### 3. Verify Installation

Run the test suite to ensure everything is working:

```bash
cd build
ctest --output-on-failure
```

You should see output indicating all tests passed.

## Your First Program

Let’s create a simple program that demonstrates the core concepts of q_mini_wasm_v2.

### Step 1: Create a New File

Create a file named `hello_ternary.cpp`:

```cpp
#include <iostream>
#include <vector>

#include "q_mini_wasm_v2/core/ternary/trit.hpp"
#include "q_mini_wasm_v2/core/stabilizer/tableau.hpp"
#include "q_mini_wasm_v2/core/moe/router.hpp"

using namespace q_mini_wasm_v2;

int main() {
    std::cout << "Hello, Ternary World!" << std::endl;
    
    // 1. Create trits
    core::ternary::Trit a = core::ternary::Trit::POSITIVE;
    core::ternary::Trit b = core::ternary::Trit::NEGATIVE;
    
    // 2. Perform GF(3) arithmetic
    core::ternary::Trit sum = core::ternary::gf3_add(a, b);
    core::ternary::Trit product = core::ternary::gf3_mul(a, b);
    
    std::cout << "a = " << static_cast<int>(a) << std::endl;
    std::cout << "b = " << static_cast<int>(b) << std::endl;
    std::cout << "a + b = " << static_cast<int>(sum) << std::endl;
    std::cout << "a * b = " << static_cast<int>(product) << std::endl;
    
    // 3. Create a stabilizer tableau
    auto tableau = core::stabilizer::create_tableau(2);
    tableau->apply_hadamard(0);
    tableau->apply_csum(0, 1);
    
    std::cout << "\nCreated 2-qutrit tableau with entanglement" << std::endl;
    
    // 4. Measure qutrit
    auto result = tableau->measure(0);
    std::cout << "Measurement result: " << static_cast<int>(result) << std::endl;
    
    return 0;
}
```

### Step 2: Compile the Program

```bash
# Navigate to build directory if not already there
cd build

# Compile (adjust include paths as needed)
g++ -std=c++17 -I../q_mini_wasm_v2 -o hello_ternary ../hello_ternary.cpp
```

### Step 3: Run the Program

```bash
./hello_ternary
```

Expected output:

```
Hello, Ternary World!
a = 1
b = -1
a + b = 0
a * b = -1

Created 2-qutrit tableau with entanglement
Measurement result: 0
```

## Understanding the Output

### Ternary Arithmetic

- `a = 1` (POSITIVE)
- `b = -1` (NEGATIVE)
- `a + b = 0` (1 + (-1) = 0 in GF(3))
- `a * b = -1` (1 * (-1) = -1 in GF(3))

### Stabilizer Tableau

- Created a 2-qutrit tableau
- Applied Hadamard gate to put qutrit 0 in superposition
- Applied Controlled-SUM to entangle qutrits 0 and 1
- Measured qutrit 0, collapsing the entangled state

## Next Steps

Now that you have a basic understanding, explore these topics:

### 1. Learn the Core Concepts

- **Ternary State Space**: Understand GF(3) arithmetic and trit encoding
- **Stabilizer Tableau**: Learn about Clifford gates and quantum-inspired operations
- **MoE Routing**: Explore tropical geometry and expert selection

### 2. Build More Complex Examples

```cpp
// Example: MoE Routing
core::moe::ExpertConfig config{8, 2, 4};  // 8 experts, Top-2, 4 routing qutrits
auto router = core::moe::create_moe_router(config);

std::vector<core::ternary::Trit> input = {
    core::ternary::Trit::POSITIVE,
    core::ternary::Trit::ZERO,
    core::ternary::Trit::NEGATIVE,
    core::ternary::Trit::POSITIVE
};

auto selected_experts = router->route_topk(input);
std::cout << "Selected " << selected_experts.size() << " experts" << std::endl;
```

### 3. Explore Advanced Features

- **Forward-Forward Learning**: Train networks without backpropagation
- **SYCL Acceleration**: Enable GPU acceleration for parallel operations
- **Runtime Orchestration**: Use async task execution

## Troubleshooting

### Common Issues

#### Issue: "Compiler not found"

**Solution**: Ensure you have a C++17 compiler installed:
- Windows: Install Visual Studio 2017+ or MinGW-w64
- Linux: Install GCC 7+ (`sudo apt install g++-7`)
- macOS: Install Xcode Command Line Tools

#### Issue: "CMake version too old"

**Solution**: Update CMake to version 3.14+:
```bash
# Ubuntu/Debian
sudo apt remove cmake
sudo snap install cmake --classic

# macOS
brew upgrade cmake
```

#### Issue: "Tests fail"

**Solution**: Ensure you built with tests enabled:
```bash
cmake -DBUILD_TESTS=ON ..
cmake --build .
ctest --output-on-failure
```

## Getting Help

If you encounter issues:

1. Check the [Architecture Overview](architecture/overview.md)
2. Review the [API Reference](api/core-reference.md)
3. Consult the [Build Guide](guides/building.md)
4. Open an issue on GitHub

## What’s Next?

Continue your learning journey:

- [Architecture Overview](architecture/overview.md) - Understand the system design
- [API Reference](api/core-reference.md) - Complete API documentation
- [Build Guide](guides/building.md) - Detailed build instructions
- [Contributing Guide](guides/contributing.md) - How to contribute
