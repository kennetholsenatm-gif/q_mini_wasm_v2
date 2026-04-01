# Native Documentation

This MCP provides comprehensive native execution and optimization tools for the QMINIWASM project, focusing on compilation, optimization, profiling, and deployment.

## Tools

### native_compile
Compiles native code from source.

**Usage**:
```bash
# Compile C++ source to native binary
mcp_native.py --tool=native_compile --args='{"source": "cpp/", "target": "x86_64"}'

# Compile specific source file
mcp_native.py --tool=native_compile --args='{"source": "cpp/main.cpp", "target": "x86_64"}'
```

### native_optimize
Optimizes native binaries.

**Usage**:
```bash
# Optimize native binary
mcp_native.py --tool=native_optimize --args='{"binary": "output", "level": "3"}'

# Optimize with different levels
mcp_native.py --tool=native_optimize --args='{"binary": "output", "level": "2"}'
```

### native_profile
Profiles native code execution.

**Usage**:
```bash
# Profile native binary
mcp_native.py --tool=native_profile --args='{"binary": "output", "metrics": "performance"}'

# Profile with different metrics
mcp_native.py --tool=native_profile --args='{"binary": "output", "metrics": "memory"}'
```

### native_debug
Debugs native code with breakpoints.

**Usage**:
```bash
# Debug native binary
mcp_native.py --tool=native_debug --args='{"binary": "output", "breakpoint": "main"}'

# Debug with specific breakpoint
mcp_native.py --tool=native_debug --args='{"binary": "output", "breakpoint": "function_name"}'
```

### native_deploy
Deploys native binaries.

**Usage**:
```bash
# Deploy native binary
mcp_native.py --tool=native_deploy --args='{"binary": "output", "target": "local"}'

# Deploy to different targets
mcp_native.py --tool=native_deploy --args='{"binary": "output", "target": "remote"}'
```

### native_monitor
Monitors native execution.

**Usage**:
```bash
# Monitor native execution
mcp_native.py --tool=native_monitor --args='{"binary": "output", "metrics": "performance"}'

# Monitor with different metrics
mcp_native.py --tool=native_monitor --args='{"binary": "output", "metrics": "resource"}'
```

## Native Workflow

### Compilation
1. **Source Preparation**: Prepare C++ source code
2. **Compilation**: Compile to native binary using CMake
3. **Validation**: Validate the generated binary
4. **Optimization**: Optimize the native binary

### Optimization
1. **Binary Preparation**: Prepare native binary for optimization
2. **Optimization**: Apply optimization levels
3. **Validation**: Validate optimized binary
4. **Testing**: Test optimized performance

### Profiling
1. **Setup**: Set up profiling environment
2. **Execution**: Execute and profile binary
3. **Analysis**: Analyze profiling results
4. **Optimization**: Optimize based on profiling data

### Debugging
1. **Setup**: Set up debugging environment
2. **Breakpoint**: Set breakpoints in native code
3. **Execution**: Execute and debug binary
4. **Analysis**: Analyze debugging results

### Deployment
1. **Preparation**: Prepare binary for deployment
2. **Deployment**: Deploy to target environment
3. **Validation**: Validate deployment success
4. **Monitoring**: Monitor deployed binary

### Monitoring
1. **Setup**: Set up monitoring for binary
2. **Data Collection**: Collect monitoring data
3. **Analysis**: Analyze monitoring data
4. **Reporting**: Generate monitoring reports

## Best Practices

### Compilation
- Use appropriate compiler flags
- Enable optimization levels
- Validate generated binaries
- Test compiled binaries

### Optimization
- Use appropriate optimization levels
- Consider target platform
- Validate optimized binaries
- Test optimized performance

### Profiling
- Use appropriate profiling tools
- Set meaningful metrics
- Analyze profiling results
- Optimize based on profiling data

### Debugging
- Use appropriate debugging tools
- Set meaningful breakpoints
- Analyze debugging output
- Test debugging results

### Deployment
- Test deployment process
- Verify deployment success
- Monitor deployed binaries
- Handle deployment errors

### Monitoring
- Monitor key metrics
- Set appropriate thresholds
- Implement alerting
- Regular monitoring reviews

## Integration with QMINIWASM

### Native Runtime
The QMINIWASM native runtime provides optimized execution for native binaries.

```python
from qminiwasm.native import NativeRuntime

# Initialize native runtime
runtime = NativeRuntime()

# Execute native binary
result = runtime.execute_native_binary(binary)
```

### Native Optimization
Native-specific optimizations for performance and size.

```python
from qminiwasm.native import NativeOptimizer

# Initialize native optimizer
optimizer = NativeOptimizer()

# Optimize native binary
optimized_binary = optimizer.optimize_binary(binary)
```

### Native Security
Native security features for binary protection.

```python
from qminiwasm.native import NativeSecurity

# Initialize native security
security = NativeSecurity()

# Apply security policies
security.apply_policies(binary)
```

## Performance Considerations

### Compilation Optimization
- Use appropriate compiler flags
- Enable optimization levels
- Consider target platform
- Validate compilation results

### Runtime Optimization
- Use efficient memory management
- Optimize function calls
- Minimize binary size
- Consider caching strategies

### Memory Management
- Monitor memory usage
- Use efficient data structures
- Implement proper cleanup
- Handle memory leaks

### Profiling
- Use appropriate profiling tools
- Set meaningful metrics
- Analyze profiling results
- Optimize based on profiling data

## Troubleshooting

### Common Issues
1. **Compilation Errors**: Check source code and compiler flags
2. **Optimization Issues**: Check optimization levels and settings
3. **Profiling Problems**: Check profiling tools and configuration
4. **Debugging Issues**: Check debugging setup and breakpoints
5. **Deployment Failures**: Check target environment and configuration

### Useful Commands
```bash
# Check CMake version
cmake --version

# Build with CMake
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Profile with perf
perf record ./binary
perf report

# Debug with gdb
gdb binary --batch --ex 'break main' --ex 'run'
```

## Dependencies

### Required
- CMake
- C++ compiler
- Performance profiling tools
- Debugging tools

### Optional
- Binary optimization tools
- Deployment automation tools
- Performance monitoring tools
- Security scanning tools

## Configuration

The MCP configuration is stored in `.cursor/mcp-native.json` and can be modified to add new tools or change existing ones.

## Security Considerations

### Binary Security
- Validate all binaries
- Check for vulnerabilities
- Use secure compilation
- Implement binary integrity checks

### Deployment Security
- Secure deployment process
- Verify deployment integrity
- Monitor deployed binaries
- Handle security incidents

### Runtime Security
- Implement runtime security
- Monitor runtime behavior
- Handle security events
- Regular security updates