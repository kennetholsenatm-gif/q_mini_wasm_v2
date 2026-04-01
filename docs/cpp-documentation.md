# C++ Programming Documentation

This MCP provides comprehensive C++ development tools and documentation for the QMINIWASM project, focusing on architecture patterns and best practices.

## Tools

### cpp_compile
Compiles C++ code using CMake with configurable build types.

**Usage**:
```bash
mcp_cpp_programming.py --tool=cpp_compile --args='{"source": "cpp/", "build_type": "Release"}'
```

**Output**:
```
Scanning dependencies of target qminiwasm
[ 50%] Building CXX object CMakeFiles/qminiwasm.dir/src/main.cpp.o
[100%] Linking CXX executable qminiwasm
[100%] Built target qminiwasm
```

### cpp_build
Builds specific C++ components or targets.

**Usage**:
```bash
# Build all targets
mcp_cpp_programming.py --tool=cpp_build --args='{"target": "all"}'

# Build specific target
mcp_cpp_programming.py --tool=cpp_build --args='{"target": "qminiwasm"}'
```

### cpp_test
Runs C++ tests with optional filtering.

**Usage**:
```bash
# Run all tests
mcp_cpp_programming.py --tool=cpp_test

# Run specific test
mcp_cpp_programming.py --tool=cpp_test --args='{"test_name": "test_ternary"}'
```

### cpp_analyze
Analyzes C++ code for specific patterns.

**Usage**:
```bash
# Analyze ternary patterns
mcp_cpp_programming.py --tool=cpp_analyze --args='{"pattern": "ternary"}'

# Analyze other patterns
mcp_cpp_programming.py --tool=cpp_analyze --args='{"pattern": "SYCL"}'
```

### cpp_refactor
Refactors C++ code with specified changes.

**Usage**:
```bash
# Simple refactoring
mcp_cpp_programming.py --tool=cpp_refactor --args='{"file": "cpp/src/main.cpp", "changes": "old_function -> new_function"}'
```

### cpp_document
Generates C++ documentation in various formats.

**Usage**:
```bash
# Generate Doxygen documentation
mcp_cpp_programming.py --tool=cpp_document --args='{"format": "Doxygen"}'

# Generate Sphinx documentation
mcp_cpp_programming.py --tool=cpp_document --args='{"format": "Sphinx"}'
```

## Architecture Patterns

### Ternary Computing
The QMINIWASM project implements ternary computing patterns for efficient AI inference.

**Key Concepts**:
- Balanced ternary representation
- Ternary logic operations
- Efficient memory usage
- Reduced computational complexity

**Implementation**:
```cpp
// Ternary addition example
int ternary_add(int a, int b) {
    int carry = 0;
    int result = 0;
    int multiplier = 1;
    
    while (a != 0 || b != 0 || carry != 0) {
        int digit_a = a % 3;
        int digit_b = b % 3;
        int sum = digit_a + digit_b + carry;
        
        result += (sum % 3) * multiplier;
        carry = sum / 3;
        
        a /= 3;
        b /= 3;
        multiplier *= 10;
    }
    
    return result;
}
```

### SYCL Integration
SYCL (SYCL for OpenCL) integration for heterogeneous computing.

**Key Features**:
- Cross-platform parallel programming
- Device-agnostic code
- Efficient memory management
- Scalable performance

**Example**:
```cpp
// SYCL kernel example
queue q;
buffer<int, 1> buf(data, range<1>(N));

q.submit([&](handler& cgh) {
    auto acc = buf.get_access<access::mode::write>(cgh);
    cgh.parallel_for<class kernel>(range<1>(N), [=](id<1> idx) {
        acc[idx] = idx[0] * 2;
    });
});
```

### WASM Integration
WebAssembly integration for edge deployment.

**Key Components**:
- WASM module compilation
- Memory management
- Function exports/imports
- Performance optimization

**Example**:
```cpp
// WASM module example
#include <emscripten.h>

EMSCRIPTEN_KEEPALIVE
int add(int a, int b) {
    return a + b;
}

EMSCRIPTEN_KEEPALIVE
void process_data(float* data, int length) {
    for (int i = 0; i < length; i++) {
        data[i] = data[i] * 2.0f;
    }
}
```

## Development Workflow

### Setup
1. Install dependencies:
   ```bash
   sudo apt-get install build-essential cmake
   pip install -r requirements.txt
   ```

2. Configure build:
   ```bash
   mcp_cpp_programming.py --tool=cpp_compile
   ```

### Development
1. Make changes to C++ files
2. Build and test:
   ```bash
   mcp_cpp_programming.py --tool=cpp_build
   mcp_cpp_programming.py --tool=cpp_test
   ```

3. Analyze code:
   ```bash
   mcp_cpp_programming.py --tool=cpp_analyze
   ```

### Documentation
1. Generate documentation:
   ```bash
   mcp_cpp_programming.py --tool=cpp_document
   ```

2. Review generated docs in `build/docs/`

## Best Practices

### Code Style
- Follow Google C++ Style Guide
- Use consistent naming conventions
- Add comprehensive comments
- Use modern C++ features (C++17+)

### Performance
- Optimize for ternary operations
- Use SYCL for parallel processing
- Minimize memory allocations
- Profile and benchmark regularly

### Testing
- Write unit tests for all components
- Use property-based testing
- Test edge cases thoroughly
- Maintain high code coverage

## Architecture Whitepapers

The project includes several architecture whitepapers that provide in-depth technical details:

### Key Whitepapers
1. **ARCHITECTURE_WHITEPAPERS.md** - Overall architecture overview
2. **CASCADE_AND_MOPD.md** - Cascade and MOPD algorithms
3. **EDGE_BUNDLE.md** - Edge deployment strategies
4. **TPEM_ARTIFACT_FORMAT.md** - TPEM artifact specifications

### Accessing Whitepapers
```bash
# List available whitepapers
ls docs/research/

# Read specific whitepaper
cat docs/research/ARCHITECTURE_WHITEPAPERS.md
```

## Integration with QMINIWASM

### Native Bridge
The C++ components integrate with Python through the native bridge:

```cpp
// Native bridge example
#include <pybind11/pybind11.h>

namespace py = py::module_;

PYBIND11_MODULE(qminiwasm, m) {
    m.def("add", &add);
    m.def("process_data", &process_data);
}
```

### Runtime Modes
Supports multiple runtime modes:
- **Native**: Direct C++ execution
- **WASM**: WebAssembly deployment
- **SYCL**: Heterogeneous computing

## Troubleshooting

### Common Issues
1. **CMake Configuration**: Ensure CMakeLists.txt is properly configured
2. **SYCL Support**: Verify SYCL compiler is installed
3. **WASM Compilation**: Check Emscripten SDK installation
4. **Memory Issues**: Monitor memory usage during compilation

### Useful Commands
```bash
# Clean build
rm -rf cpp/build

# Reconfigure CMake
cmake -S cpp -B cpp/build

# Run specific test
ctest -R test_name

# Analyze code
grep -r pattern cpp/
```

## Dependencies

### Required
- CMake 3.15+
- C++17 compatible compiler
- Python 3.8+
- Pybind11
- SYCL compiler (optional)

### Optional
- Emscripten SDK (for WASM)
- Doxygen (for documentation)
- Sphinx (for documentation)

## Configuration

The MCP configuration is stored in `.cursor/mcp-cpp-programming.json` and can be modified to add new tools or change existing ones.

## Performance Considerations

### Ternary Optimization
- Use bitwise operations for ternary logic
- Implement custom memory allocators
- Optimize for cache locality
- Use SIMD instructions when possible

### SYCL Optimization
- Choose appropriate work-group sizes
- Use local memory for shared data
- Implement efficient data transfers
- Profile kernel performance

### WASM Optimization
- Minimize module size
- Use efficient data structures
- Implement lazy loading
- Optimize for browser environments