# Contributing Guide

Thank you for your interest in contributing to q_mini_wasm_v2! This guide
will help you get started.

## Getting Started

### Prerequisites

Before you begin, ensure you have:

- **C++17 Compiler**: GCC 7+, Clang 5+, or MSVC 2017+
- **CMake**: Version 3.14 or higher
- **Git**: For version control
- **GitHub Account**: For pull requests

### Setting Up Your Development Environment

1. **Fork the Repository**
   ```bash
   # Click "Fork" on GitHub, then clone your fork
   git clone https://github.com/YOUR-USERNAME/q_mini_wasm_v2.git
   cd q_mini_wasm_v2
   ```

2. **Add Upstream Remote**
   ```bash
   git remote add upstream https://github.com/kennetholsenatm-gif/q_mini_wasm_v2.git
   ```

3. **Build the Project**
   ```bash
   cd q_mini_wasm_v2
   mkdir build && cd build
   cmake -DBUILD_TESTS=ON ..
   cmake --build .
   ```

4. **Run Tests**
   ```bash
   ctest --output-on-failure
   ```

## Development Workflow

### 1. Create a Feature Branch

Always work on a feature branch, never directly on `main`:

```bash
git checkout -b feature/your-feature-name
```

### 2. Make Your Changes

- Write clean, well-documented code
- Follow the [Code Standards](#code-standards) below
- Add tests for new functionality
- Update documentation as needed

### 3. Test Your Changes

Run the full test suite to ensure nothing is broken:

```bash
cd build
cmake --build .
ctest --output-on-failure
```

### 4. Commit Your Changes

Follow the [Commit Message Guidelines](#commit-messages) below:

```bash
git add .
git commit -m "feat: add new feature"
```

### 5. Push and Create Pull Request

```bash
git push origin feature/your-feature-name
```

Then create a pull request on GitHub.

## Code Standards

### C++ Standards

- **C++17**: Use modern C++ features
- **Headers**: Use `#pragma once` for include guards
- **Namespaces**: Use `q_mini_wasm_v2::` namespace hierarchy
- **Documentation**: All public APIs must have Doxygen-style comments

### Code Style

```cpp
// Good example
namespace q_mini_wasm_v2::core::ternary {

/**
 * @brief Convert Trit to GF(3) value
 * @param t Input Trit
 * @return GF(3) value in {0, 1, 2}
 */
constexpr int8_t to_gf3(Trit t) noexcept {
    return static_cast<int8_t>(t) + 1;
}

}  // namespace q_mini_wasm_v2::core::ternary
```

### Naming Conventions

| Element | Convention | Example |
|---|---|---|
| Classes | PascalCase | `StabilizerTableau` |
| Functions | snake_case | `apply_hadamard` |
| Variables | snake_case | `num_qutrits` |
| Constants | UPPER_SNAKE_CASE | `MAX_EXPERTS` |
| Namespaces | snake_case | `core::stabilizer` |

### Header Organization

```cpp
#pragma once

// Standard library headers (alphabetical)
#include <cstdint>
#include <memory>
#include <vector>

// Third-party headers (if any)

// Project headers
#include "../ternary/trit.hpp"
  // ... (truncated)
  // See source for complete code
```

## Documentation Standards

### Code Documentation

All public APIs must have Doxygen-style comments:

```cpp
/**
 * @brief Apply Hadamard gate to target qutrit
 * @param target_qutrit Index of qutrit to apply gate to
 * @throws std::out_of_range if target_qutrit >= num_qutrits()
 */
void apply_hadamard(size_t target_qutrit);
```

### Documentation Files

Follow Cognitive Ergonomics principles:

- **Line length**: ≤ 75 characters
- **Paragraphs**: ≤ 4 lines
- **Headers**: Every ±200 words
- **Code blocks**: ≤ 15 lines
- **Navigation depth**: ≤ 3 levels

### Example Documentation

```markdown
# Feature Title

Brief description of the feature.

## Overview

Detailed explanation with clear sections.

## Usage

```cpp
// Example code
auto result = feature_function();
```

## See Also

- [Related Feature](Related)
```

## Commit Messages

### Format

```
type: short description

Longer explanation if needed.
```

### Types

| Type | Description | Example |
|---|---|---|
| `feat` | New feature | `feat: add MoE routing` |
| `fix` | Bug fix | `fix: correct tableau measurement` |
| `docs` | Documentation | `docs: update API reference` |
| `refactor` | Code refactoring | `refactor: simplify trit operations` |
| `test` | Adding tests | `test: add stabilizer tests` |
| `chore` | Maintenance | `chore: update CMake configuration` |

### Examples

```bash
# Good commit messages
git commit -m "feat: add entanglement-based routing"
git commit -m "fix: correct GF(3) multiplication edge case"
git commit -m "docs: add quick start guide"

# Bad commit messages
git commit -m "update stuff"
git commit -m "fix"
git commit -m "WIP"
```

## Testing

### Running Tests

```bash
# Run all tests
cd build
ctest --output-on-failure

# Run specific test
./q_mini_wasm_v2_tests --gtest_filter="*Trit*"

# Run with verbose output
ctest --verbose
```

### Writing Tests

Add tests for all new functionality:

```cpp
#include <cassert>
#include <iostream>

#include "../core/ternary/trit.hpp"

void test_gf3_operations() {
    using namespace q_mini_wasm_v2::core::ternary;
    
    // Test addition
    assert(gf3_add(Trit::POSITIVE, Trit::POSITIVE) == Trit::NEGATIVE);
    assert(gf3_add(Trit::POSITIVE, Trit::ZERO) == Trit::POSITIVE);
  // ... (truncated)
  // See source for complete code
```

### Test Coverage

Aim for comprehensive test coverage:

- **Unit tests**: Test individual functions
- **Integration tests**: Test component interactions
- **Edge cases**: Test boundary conditions
- **Error handling**: Test error paths

## Pull Request Process

### Before Submitting

1. **Update your branch**
   ```bash
   git fetch upstream
   git rebase upstream/main
   ```

2. **Run all tests**
   ```bash
   cd build
   cmake --build .
   ctest --output-on-failure
   ```

3. **Check code style**
   - Ensure consistent formatting
   - Verify documentation is complete

### Pull Request Template

```markdown
## Description

Brief description of changes.

## Type of Change

- [ ] Bug fix
- [ ] New feature
- [ ] Documentation update
- [ ] Refactoring

  // ... (truncated)
  // See source for complete code
```

### Review Process

1. **Automated checks**: CI will run tests
2. **Code review**: At least one maintainer review
3. **Feedback**: Address any review comments
4. **Merge**: Once approved, your PR will be merged

## Issue Reporting

### Bug Reports

When reporting bugs, include:

1. **Description**: Clear description of the issue
2. **Steps to reproduce**: Minimal steps to reproduce
3. **Expected behavior**: What should happen
4. **Actual behavior**: What actually happens
5. **Environment**: OS, compiler, CMake version

### Feature Requests

When requesting features, include:

1. **Description**: Clear description of the feature
2. **Use case**: Why this feature is needed
3. **Proposed solution**: How it could be implemented
4. **Alternatives**: Other solutions considered

## Getting Help

If you need help:

1. **Documentation**: Check the [docs](../readme)
2. **Issues**: Search existing [GitHub
Issues](https://github.com/kennetholsenatm-gif/q_mini_wasm_v2/issues)
3. **Discussions**: Use GitHub Discussions for questions
4. **Email**: Contact maintainers directly

## Code of Conduct

### Our Standards

- **Respectful**: Be respectful and inclusive
- **Constructive**: Provide constructive feedback
- **Collaborative**: Work together toward common goals
- **Professional**: Maintain professional communication

### Enforcement

Violations of the code of conduct will be handled by project maintainers.

## License

By contributing, you agree that your contributions will be licensed under
the MIT License.
