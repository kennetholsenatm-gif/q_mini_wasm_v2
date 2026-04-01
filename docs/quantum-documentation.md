# Quantum Computing Documentation

This MCP provides comprehensive quantum computing tools and documentation for the QMINIWASM project, focusing on quantum circuit simulation, optimization, and execution.

## Tools

### quantum_simulate
Simulates quantum circuits with configurable shot counts.

**Usage**:
```bash
# Simulate basic circuit
mcp_quantum_computing.py --tool=quantum_simulate --args='{"circuit": "OPENQASM 2.0; include \"qelib1.inc\"; qreg q[2]; creg c[2]; h q[0]; cx q[0],q[1]; measure q[0] -> c[0]; measure q[1] -> c[1];", "shots": 1024}'

# Simulate with default shots
mcp_quantum_computing.py --tool=quantum_simulate --args='{"circuit": "OPENQASM 2.0; include \"qelib1.inc\"; qreg q[1]; creg c[1]; h q[0]; measure q[0] -> c[0];"}'
```

### quantum_optimize
Optimizes quantum circuits using various methods.

**Usage**:
```bash
# Optimize with gate simplification
mcp_quantum_computing.py --tool=quantum_optimize --args='{"circuit": "OPENQASM 2.0; include \"qelib1.inc\"; qreg q[2]; creg c[2]; h q[0]; cx q[0],q[1]; measure q[0] -> c[0]; measure q[1] -> c[1];", "method": "gate_simplification"}'

# Optimize with other methods
mcp_quantum_computing.py --tool=quantum_optimize --args='{"circuit": "OPENQASM 2.0; include \"qelib1.inc\"; qreg q[3]; creg c[3]; h q[0]; cx q[0],q[1]; cx q[1],q[2]; measure q[0] -> c[0]; measure q[1] -> c[1]; measure q[2] -> c[2];", "method": "depth_optimization"}'
```

### quantum_analyze
Analyzes quantum circuits for various properties.

**Usage**:
```bash
# Analyze circuit depth
mcp_quantum_computing.py --tool=quantum_analyze --args='{"circuit": "OPENQASM 2.0; include \"qelib1.inc\"; qreg q[2]; creg c[2]; h q[0]; cx q[0],q[1]; measure q[0] -> c[0]; measure q[1] -> c[1];"}'

# Analyze circuit complexity
mcp_quantum_computing.py --tool=quantum_analyze --args='{"circuit": "OPENQASM 2.0; include \"qelib1.inc\"; qreg q[3]; creg c[3]; h q[0]; cx q[0],q[1]; cx q[1],q[2]; measure q[0] -> c[0]; measure q[1] -> c[1]; measure q[2] -> c[2];"}'
```

### quantum_compile
Compiles quantum circuits for specific hardware targets.

**Usage**:
```bash
# Compile for Qiskit
mcp_quantum_computing.py --tool=quantum_compile --args='{"circuit": "OPENQASM 2.0; include \"qelib1.inc\"; qreg q[2]; creg c[2]; h q[0]; cx q[0],q[1]; measure q[0] -> c[0]; measure q[1] -> c[1];", "target": "qiskit"}'

# Compile for other targets
mcp_quantum_computing.py --tool=quantum_compile --args='{"circuit": "OPENQASM 2.0; include \"qelib1.inc\"; qreg q[3]; creg c[3]; h q[0]; cx q[0],q[1]; cx q[1],q[2]; measure q[0] -> c[0]; measure q[1] -> c[1]; measure q[2] -> c[2];", "target": "aer"}'
```

### quantum_run
Runs quantum circuits on actual quantum hardware.

**Usage**:
```bash
# Run on IBMQ simulator
mcp_quantum_computing.py --tool=quantum_run --args='{"circuit": "OPENQASM 2.0; include \"qelib1.inc\"; qreg q[2]; creg c[2]; h q[0]; cx q[0],q[1]; measure q[0] -> c[0]; measure q[1] -> c[1];", "backend": "ibmq_qasm_simulator"}'

# Run on real quantum hardware
mcp_quantum_computing.py --tool=quantum_run --args='{"circuit": "OPENQASM 2.0; include \"qelib1.inc\"; qreg q[1]; creg c[1]; h q[0]; measure q[0] -> c[0];", "backend": "ibmq_manhattan"}'
```

### quantum_visualize
Visualizes quantum circuits in various formats.

**Usage**:
```bash
# Visualize in LaTeX format
mcp_quantum_computing.py --tool=quantum_visualize --args='{"circuit": "OPENQASM 2.0; include \"qelib1.inc\"; qreg q[2]; creg c[2]; h q[0]; cx q[0],q[1]; measure q[0] -> c[0]; measure q[1] -> c[1];", "format": "latex"}'

# Visualize in other formats
mcp_quantum_computing.py --tool=quantum_visualize --args='{"circuit": "OPENQASM 2.0; include \"qelib1.inc\"; qreg q[3]; creg c[3]; h q[0]; cx q[0],q[1]; cx q[1],q[2]; measure q[0] -> c[0]; measure q[1] -> c[1]; measure q[2] -> c[2];", "format": "mpl"}'
```

## Quantum Algorithms

### Basic Quantum Circuits
#### Bell State Preparation
```qasm
OPENQASM 2.0;
include "qelib1.inc";
qreg q[2];
creg c[2];
h q[0];
cx q[0],q[1];
measure q[0] -> c[0];
measure q[1] -> c[1];
```

#### Quantum Fourier Transform
```qasm
OPENQASM 2.0;
include "qelib1.inc";
qreg q[4];
creg c[4];
h q[0];
cu1(pi/2) q[1],q[0];
h q[1];
cu1(pi/4) q[2],q[0];
cu1(pi/2) q[2],q[1];
h q[2];
cu1(pi/8) q[3],q[0];
cu1(pi/4) q[3],q[1];
cu1(pi/2) q[3],q[2];
h q[3];
swap q[0],q[3];
swap q[1],q[2];
measure q[0] -> c[0];
measure q[1] -> c[1];
measure q[2] -> c[2];
measure q[3] -> c[3];
```

### Advanced Quantum Algorithms

#### Grover's Algorithm
```qasm
OPENQASM 2.0;
include "qelib1.inc";
qreg q[3];
creg c[3];
h q[0];
h q[1];
h q[2];
ccx q[0],q[1],q[2];
h q[2];
h q[0];
h q[1];
x q[0];
x q[1];
h q[1];
ccx q[0],q[1],q[2];
h q[1];
x q[0];
x q[1];
h q[0];
h q[1];
h q[2];
measure q[0] -> c[0];
measure q[1] -> c[1];
measure q[2] -> c[2];
```

#### Shor's Algorithm
```qasm
OPENQASM 2.0;
include "qelib1.inc";
qreg q[8];
creg c[8];
h q[0];
h q[1];
h q[2];
h q[3];
h q[4];
h q[5];
h q[6];
h q[7];
// Modular exponentiation
// QFT
// Measurement
measure q[0] -> c[0];
measure q[1] -> c[1];
measure q[2] -> c[2];
measure q[3] -> c[3];
measure q[4] -> c[4];
measure q[5] -> c[5];
measure q[6] -> c[6];
measure q[7] -> c[7];
```

## Development Workflow

### Setup
1. Install Qiskit:
   ```bash
   pip install qiskit
   pip install qiskit-aer
   pip install qiskit-ibmq-provider
   ```

2. Configure IBMQ account:
   ```bash
   python -c "from qiskit import IBMQ; IBMQ.save_account('YOUR_API_TOKEN')"
   ```

### Development
1. Create quantum circuits in QASM format
2. Test with simulation:
   ```bash
   mcp_quantum_computing.py --tool=quantum_simulate
   ```

3. Optimize circuits:
   ```bash
   mcp_quantum_computing.py --tool=quantum_optimize
   ```

4. Analyze performance:
   ```bash
   mcp_quantum_computing.py --tool=quantum_analyze
   ```

5. Compile for target hardware:
   ```bash
   mcp_quantum_computing.py --tool=quantum_compile
   ```

6. Run on quantum hardware:
   ```bash
   mcp_quantum_computing.py --tool=quantum_run
   ```

### Documentation
1. Generate circuit documentation:
   ```bash
   mcp_quantum_computing.py --tool=quantum_visualize
   ```

2. Review generated visualizations

## Best Practices

### Circuit Design
- Use minimal gate count
- Optimize for target hardware
- Consider error correction
- Implement proper measurement

### Error Handling
- Check circuit validity
- Handle measurement errors
- Implement retry logic
- Monitor hardware status

### Performance Optimization
- Use efficient gate sequences
- Optimize qubit mapping
- Minimize circuit depth
- Use appropriate shot counts

## Integration with QMINIWASM

### Quantum-Native Bridge
The quantum components integrate with Python through the native bridge:

```python
from qminiwasm.quantum import QuantumBridge

# Initialize quantum bridge
bridge = QuantumBridge()

# Execute quantum circuit
result = bridge.execute_circuit(circuit)
```

### Runtime Integration
Support for quantum runtime modes:

```python
from qminiwasm.runtime_modes import RuntimeMode

# Set quantum runtime mode
RuntimeMode.set_mode('quantum')

# Execute quantum operations
quantum_result = execute_quantum_operation(operation)
```

## Troubleshooting

### Common Issues
1. **Circuit Validation**: Ensure QASM syntax is correct
2. **Hardware Access**: Check IBMQ account configuration
3. **Simulation Errors**: Verify circuit complexity
4. **Compilation Issues**: Check target compatibility

### Useful Commands
```bash
# Check Qiskit version
python -c "import qiskit; print(qiskit.__version__)"

# List available backends
python -c "from qiskit import IBMQ; IBMQ.load_account(); provider = IBMQ.get_provider(); print(provider.backends())"

# Validate circuit
python -c "from qiskit import QuantumCircuit; qc = QuantumCircuit.from_qasm_file('circuit.qasm'); print(qc.validate())"
```

## Dependencies

### Required
- Qiskit 0.30+
- Qiskit Aer
- Qiskit IBMQ Provider
- Python 3.8+

### Optional
- Qiskit Nature (for quantum chemistry)
- Qiskit Optimization (for optimization problems)
- Qiskit Machine Learning (for quantum ML)

## Configuration

The MCP configuration is stored in `.cursor/mcp-quantum-computing.json` and can be modified to add new tools or change existing ones.

## Performance Considerations

### Simulation Optimization
- Use appropriate simulator backend
- Optimize shot count
- Parallelize simulations
- Cache results when possible

### Hardware Execution
- Choose appropriate backend
- Optimize qubit mapping
- Minimize circuit depth
- Handle hardware errors gracefully

### Memory Management
- Monitor memory usage
- Use efficient data structures
- Implement proper cleanup
- Use streaming for large results