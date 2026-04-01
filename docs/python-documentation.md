# Python Programming Documentation

This MCP provides comprehensive Python development tools and documentation for the QMINIWASM project, focusing on runtime modes and AI capabilities.

## Tools

### python_run
Runs Python scripts with optional arguments.

**Usage**:
```bash
# Run main CLI
mcp_python_programming.py --tool=python_run

# Run specific script with arguments
mcp_python_programming.py --tool=python_run --args='{"script": "qminiwasm/cli/main.py", "args": "--help"}'
```

### python_test
Runs Python tests with optional filtering.

**Usage**:
```bash
# Run all tests
mcp_python_programming.py --tool=python_test

# Run specific test
mcp_python_programming.py --tool=python_test --args='{"test_name": "test_runtime_modes"}'
```

### python_analyze
Analyzes Python code for specific patterns.

**Usage**:
```bash
# Analyze runtime patterns
mcp_python_programming.py --tool=python_analyze --args='{"pattern": "runtime"}'

# Analyze other patterns
mcp_python_programming.py --tool=python_analyze --args='{"pattern": "inference"}'
```

### python_refactor
Refactors Python code with specified changes.

**Usage**:
```bash
# Simple refactoring
mcp_python_programming.py --tool=python_refactor --args='{"file": "qminiwasm/runtime_modes.py", "changes": "old_function -> new_function"}'
```

### python_document
Generates Python documentation in various formats.

**Usage**:
```bash
# Generate Sphinx documentation
mcp_python_programming.py --tool=python_document --args='{"format": "Sphinx"}'

# Generate Markdown documentation
mcp_python_programming.py --tool=python_document --args='{"format": "Markdown"}'
```

### python_profile
Profiles Python code performance.

**Usage**:
```bash
# Profile main CLI
mcp_python_programming.py --tool=python_profile

# Profile specific script
mcp_python_programming.py --tool=python_profile --args='{"script": "qminiwasm/cli/main.py"}'
```

## Runtime Modes

The QMINIWASM project supports multiple runtime modes for flexible deployment.

### Available Runtime Modes
1. **Native**: Direct Python execution
2. **WASM**: WebAssembly deployment
3. **Edge**: Edge computing optimization
4. **Quantum**: Quantum computing integration

### Runtime Configuration
```python
# Runtime mode configuration
from qminiwasm.runtime_modes import RuntimeMode

# Set runtime mode
RuntimeMode.set_mode('native')

# Get current mode
current_mode = RuntimeMode.get_mode()

# Check if mode is supported
if RuntimeMode.is_supported('wasm'):
    # Use WASM mode
    pass
```

## Development Workflow

### Setup
1. Install dependencies:
   ```bash
   pip install -r requirements.txt
   ```

2. Configure environment:
   ```bash
   python -m venv venv
   source venv/bin/activate
   ```

### Development
1. Make changes to Python files
2. Build and test:
   ```bash
   mcp_python_programming.py --tool=python_test
   ```

3. Analyze code:
   ```bash
   mcp_python_programming.py --tool=python_analyze
   ```

4. Profile performance:
   ```bash
   mcp_python_programming.py --tool=python_profile
   ```

### Documentation
1. Generate documentation:
   ```bash
   mcp_python_programming.py --tool=python_document
   ```

2. Review generated docs in `build/docs/`

## Best Practices

### Code Style
- Follow PEP 8 style guide
- Use type hints for better IDE support
- Add comprehensive docstrings
- Use modern Python features (3.8+)

### Performance
- Optimize for runtime efficiency
- Use appropriate data structures
- Minimize memory allocations
- Profile and benchmark regularly

### Testing
- Write unit tests for all components
- Use property-based testing
- Test edge cases thoroughly
- Maintain high code coverage

## AI Capabilities

### Inference Engine
The project includes a sophisticated inference engine:

```python
from qminiwasm.inference import InferenceEngine

# Initialize inference engine
engine = InferenceEngine()

# Run inference
result = engine.infer(input_data)

# Get model information
model_info = engine.get_model_info()
```

### Cognitive Functions
Advanced cognitive capabilities:

```python
from qminiwasm.cognitive import CognitiveProcessor

# Process cognitive tasks
processor = CognitiveProcessor()

# Analyze data
analysis = processor.analyze(data)

# Generate insights
insights = processor.generate_insights(analysis)
```

## Integration with QMINIWASM

### Native Bridge
Python components integrate with C++ through the native bridge:

```python
# Native bridge example
from qminiwasm.native_bridge import NativeBridge

# Initialize bridge
bridge = NativeBridge()

# Call native functions
result = bridge.call_native_function('add', 1, 2)
```

### Hardware Acceleration
Support for hardware acceleration:

```python
from qminiwasm.hardware import HardwareAccelerator

# Initialize accelerator
accelerator = HardwareAccelerator()

# Accelerate computation
accelerated_result = accelerator.accelerate(computation)
```

## Troubleshooting

### Common Issues
1. **Dependency Conflicts**: Use virtual environments
2. **Runtime Mode Issues**: Check mode compatibility
3. **Performance Problems**: Profile and optimize
4. **Memory Issues**: Monitor memory usage

### Useful Commands
```bash
# Check Python version
python --version

# Install dependencies
pip install -r requirements.txt

# Run specific test
pytest -v test_file.py::test_function

# Profile script
python -m cProfile -s cumtime script.py
```

## Dependencies

### Required
- Python 3.8+
- Pytest
- Sphinx
- Cython
- NumPy
- PyTorch

### Optional
- TensorFlow
- ONNX Runtime
- CUDA (for GPU acceleration)

## Configuration

The MCP configuration is stored in `.cursor/mcp-python-programming.json` and can be modified to add new tools or change existing ones.

## Performance Considerations

### Runtime Optimization
- Use appropriate runtime mode
- Optimize data structures
- Minimize I/O operations
- Use caching where appropriate

### Memory Management
- Monitor memory usage
- Use generators for large datasets
- Implement proper cleanup
- Use weak references when needed

### Profiling
- Use cProfile for performance analysis
- Monitor CPU and memory usage
- Identify bottlenecks
- Optimize critical paths