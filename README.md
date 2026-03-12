# LLM_Pract

A quantum-classical hybrid AI architecture project implementing the Q-Mini-WASM architecture for deterministic in-model execution. This project focuses on MoE routing optimization and coding use cases, with planned integration of Intel Arc GPU cloud resources.

## Overview

This project implements a hybrid quantum-classical architecture that enables deterministic WebAssembly execution within the model itself, eliminating the need for external tool-use loops. The architecture combines quantum MoE routing, ternary quantization, geometric attention mechanisms, and hardware-optimized execution.

## Key Features

- **Quantum MoE Routing**: Parameterized Quantum Circuits for optimal expert routing
- **Deterministic Execution**: WebAssembly interpreter compiled into model weights
- **Infinite Context Window**: Geometric hull-based KV cache for million-token traces
- **Intel Arc GPU Support**: Planned integration with Intel's GPU cloud resources
- **Coding Focus**: Specialized for algorithmic reasoning and code execution
- **Security-First**: Integrated DevSecOps pipeline with comprehensive security scanning

## Project Structure

```
LLM_Pract/
├── src/                    # Core application source code
├── tests/                 # Test suites
├── scripts/              # Automation scripts
├── docs/                # Documentation
├── monitoring/          # Monitoring configuration
├── terraform/           # Infrastructure as code
├── qml/                # Quantum ML components
├── wasm/               # WebAssembly execution components
├── requirements.txt     # Python dependencies
├── README.md           # This file
├── .gitignore         # Git ignore rules
├── .gitattributes     # Git attributes
└── LICENSE            # License file
```

## Quick Start

### Prerequisites
- Python 3.8+
- Docker & Docker Compose
- Git
- Intel oneAPI (when available)
- IBM Quantum API key (for quantum components)

### Installation
1. Clone the repository
2. Install dependencies: `pip install -r requirements.txt`
3. Set up environment: `python3 -m pip install --upgrade pip`

### Running the Application
```bash
# Run tests
python -m pytest tests/ --cov=src --cov-report=xml

# Start the application
python src/main.py

# Run complete DevSecOps workflow
./scripts/devsecops-workflow.sh
```

## Security Features

### Security Scanning
- **Bandit**: Python security linter
- **Safety**: Dependency vulnerability scanning
- **Trivy**: Container security scanning
- **Secret Scanning**: Detection of exposed credentials

### Compliance
- **STIG Compliance**: Security Technical Implementation Guides checks
- **File Permissions**: Automated security hardening
- **Configuration Management**: Secure defaults and hardening

## Technology Stack

- **Core**: Python 3.8+, PyTorch, PennyLane
- **Quantum**: PennyLane, Qiskit Aer, IBM Quantum API
- **WASM**: wasmtime, pywasm
- **Hardware**: Intel oneAPI DPC++/SYCL (when available)
- **Testing**: pytest, hypothesis, bandit, safety
- **Infrastructure**: Terraform, Docker, Prometheus/Grafana

## Development Workflow

This project uses a comprehensive DevSecOps pipeline with automated testing, security scanning, and continuous deployment workflows.

## Contributing

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Add tests for new functionality
5. Run the complete workflow: `./scripts/devsecops-workflow.sh`
6. Submit a pull request

## License

This project is licensed under the MIT License - see the LICENSE file for details.

## Security Policy

Report security vulnerabilities to security@example.com. Please do not use public GitHub issues for reporting vulnerabilities.

## Support

For support and questions, please open an issue in the GitHub repository.

---

**Note**: This project includes quantum components and requires careful handling of quantum resources. Always review and test changes before committing to ensure they meet security requirements.