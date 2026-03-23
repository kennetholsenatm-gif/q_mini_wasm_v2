# Development Workflow

## Overview

The Q-Mini-WASM development workflow is designed to be comprehensive, secure, and efficient, incorporating modern DevSecOps practices throughout the entire development lifecycle. This workflow ensures that all code meets quality standards, passes security checks, and adheres to compliance requirements before being integrated into the main codebase.

## Development Environment Setup

### Prerequisites
- **Python Version**: 3.11+
- **Operating Systems**: Linux, macOS, Windows
- **Tools**: Git, Docker (for development), GitHub account
- **IDE**: Visual Studio Code, PyCharm, or preferred IDE

### Initial Setup
```bash
# Clone the repository
git clone https://github.com/kennetholsenatm-gif/qminiwasm-core.git
cd qminiwasm-core

# Install dependencies
pip install -r requirements.txt

# Install pre-commit hooks
pre-commit install

# Set up development environment
python -m pip install --upgrade pip
pip install -e .
```

## Development Workflow Steps

### 1. Feature Branch Creation
```bash
# Create a new feature branch
git checkout -b feature/your-feature-name
```

### 2. Development Phase
- **Code Implementation**: Write feature code following project standards
- **Documentation**: Update documentation as needed
- **Testing**: Write unit tests for new functionality
- **Security Considerations**: Implement security best practices

### 3. Pre-commit Hooks
All commits must pass the following pre-commit hooks:
- **Black**: Code formatting with 100-character line length
- **Flake8**: Python linting with custom configuration
- **MyPy**: Static type checking for type safety
- **Bandit**: Python security linter
- **Detect-secrets**: Secret detection with baseline comparison

### Training web UI (optional)

A small **Go** UI under [`training-wui/`](../training-wui) lists `configs/training/*.toml` and runs `python -m engine --config …` from the repo root. To provision an Incus guest with deps + mount + built WUI, use [`training-wui/incus/`](../training-wui/incus) (`setup-instance.sh`). See [`training-wui/README.md`](../training-wui/README.md).

### IBM Quantum (optional, training MoE)

To run the **QAOA** MoE block on **real IBM hardware** during training, configure **`qiskit_ibm`**, **`IBM_QUANTUM_API_TOKEN`**, and a backend name (see [`docs/QUANTUM_QISKIT.md`](../docs/QUANTUM_QISKIT.md)). This path has been **validated on IBM Quantum** (March 2026); quota and account limits apply.

### 4. Local Testing
```bash
# Run complete test suite
pytest tests/ -v --cov=qminiwasm --cov-report=html

# Run security scan
bandit -r qminiwasm/
safety check

# Run local DevSecOps workflow
pwsh -File scripts/devsecops-workflow.ps1
```

## Connecting to Docker and Kubernetes via Teleport

When the project uses [Teleport](https://goteleport.com/) for Zero Trust access, development access to Kubernetes (and optionally Docker) is gated by Teleport. You use short-lived certificates instead of long-lived kubeconfig or SSH keys.

### Prerequisites

- **tsh** (Teleport client) installed. See [Teleport installation](https://goteleport.com/docs/installation/).
- **TELEPORT_PROXY** set to your Teleport proxy address (e.g. `teleport.example.com`).
- **TELEPORT_KUBE_CLUSTER** (optional) set to the Kubernetes cluster name configured in Teleport (e.g. `qminiwasm-dev`).

### Steps

1. Set environment variables (replace with your cluster values):

   ```bash
   export TELEPORT_PROXY=teleport.example.com
   export TELEPORT_KUBE_CLUSTER=qminiwasm-dev
   ```

   On Windows (PowerShell):

   ```powershell
   $env:TELEPORT_PROXY = "teleport.example.com"
   $env:TELEPORT_KUBE_CLUSTER = "qminiwasm-dev"
   ```

2. Run the login script. This performs `tsh login --auth=sso` (browser/WebAuthn) and updates your kubeconfig with short-lived certs:

   - **Windows:** `pwsh -File scripts/teleport-login.ps1`
   - **Linux/macOS:** `./scripts/teleport-login.sh`

3. Use `kubectl` and (if configured) Docker as usual. Access is enforced by Teleport; certificates expire after the session TTL (e.g. 8 hours). Re-run the script when needed.

For how this satisfies IA-2, AC-3, and AU-2/AU-3, see [SECURITY.md](../SECURITY.md#zero-trust-access-teleport).

## Code Quality Standards

### Python Code Standards
- **Formatting**: Black code formatting with 100-character line length
- **Linting**: Flake8 with custom configuration (E203, W503 ignored)
- **Type Checking**: MyPy static type analysis
- **Documentation**: Docstrings following Google style guide

### Security Standards
- **No Hardcoded Secrets**: Strict prohibition of hardcoded credentials
- **Dependency Management**: Regular dependency updates and scanning
- **Security Review**: Security impact assessment for all changes
- **Vulnerability Management**: Systematic vulnerability identification and remediation

## Testing Strategy

### Unit Testing
- **Coverage**: Minimum 80% code coverage required
- **Test Structure**: Tests follow pytest conventions
- **Mock Testing**: Use of mocks for external dependencies
- **Integration Testing**: Integration test coverage for major components

### Security Testing
- **SAST Scanning**: Bandit for static application security testing
- **Dependency Scanning**: pip-audit for dependency vulnerability scanning
- **Secret Detection**: Detect-secrets for hardcoded secret identification
- **Compliance Testing**: STIG and CMMC2.0 compliance validation

### Performance Testing
- **Benchmarking**: Performance benchmarks for critical operations
- **Load Testing**: Load testing for scalability assessment
- **Memory Testing**: Memory usage and leak detection
- **Optimization**: Performance optimization and tuning

## ML engine training and inference

**Wiki overview (goals and design rationale):** [AI Training Pipeline](AI-Training-Pipeline.md).

Training runs from the **qminiwasm-core** repository:

```bash
pip install -e ".[training]"
python -m engine
```

**Canonical reference:** [docs/TRAINING_DATA.md](https://github.com/kennetholsenatm-gif/qminiwasm-core/blob/main/docs/TRAINING_DATA.md) (data sources, env table, checkpoints, metrics).

**Highlights (recent pipeline behavior):**

- **`hf_tabular`** — When `TARGET_MEAN_MSE` is unset, the engine defaults it to **1e-4**; optional defaults also include **grad clip 1.0**, **cascade policy LR = 0.5 × main LR**, and a slightly higher default **learning rate (1.5e-4)** when `LEARNING_RATE` is unset and the constructor LR is the stock **1e-4**. Override any of these with explicit env vars.
- **`HF_MESH_BLEND_FRACTION`** — Appends mesh-generated samples (fraction × HF row count) to Hugging Face tabular data, then shuffles when `SEED` is set, so training mixes WASM curriculum with Hub rows.
- **Cascade RL** — GRPO toy routing runs before each epoch’s supervised MSE phase by default. **`CASCADE_COUPLE_FORWARD`** (default on) blends mean **input hidden** and mean **`hybrid_inference` output** for the cascade digest (tighter coupling to the live model). Disable with `CASCADE_COUPLE_FORWARD=0`.
- **Checkpoints** — `cascade_policy` weights load into **`model.cascade_router`** when present and the model was constructed with **`USE_CASCADE_ROUTER=1`**.
- **Serving** — `uvicorn engine.serve:app` with **`QMINIWASM_CHECKPOINT`**, optional **`HYBRID_ADAPTER`**, **`USE_CASCADE_ROUTER`**, and matching **`CASCADE_*`** dims. **`POST /infer`** returns **`cascade_logits`** per row when the router is attached.

## Pull Request Process

### PR Requirements
- **Status Checks**: All CI checks must pass
- **Code Review**: Mandatory peer review by at least one developer
- **Security Review**: Security impact assessment for all changes
- **Documentation**: Updated documentation for new features

### PR Template
```markdown
## Description
[Brief description of the changes]

## Type of Change
- [ ] Bug fix
- [ ] New feature
- [ ] Breaking change
- [ ] Documentation update
- [ ] Security improvement

## Testing
- [ ] Unit tests added/updated
- [ ] Security scans passed
- [ ] Performance benchmarks included
- [ ] Documentation updated

## Checklist
- [ ] Code follows project standards
- [ ] Security review completed
- [ ] Tests pass locally
- [ ] Documentation updated
```

## Continuous Integration

### GitHub Actions Workflow
- **Trigger Events**: Push to main/master, pull requests, manual dispatch
- **Jobs**: Linting, testing, security scanning, coverage reporting
- **Artifacts**: Test reports, coverage reports, security scan results
- **Notifications**: Status notifications for build results

### CI Pipeline Steps
1. **Checkout**: Repository checkout and setup
2. **Setup**: Python environment setup and dependency installation
3. **Linting**: Code formatting and linting checks
4. **Testing**: Unit test execution with coverage
5. **Security**: Static security scanning and dependency analysis
6. **Reporting**: Test and security report generation

## Security Integration

### Security Gates
- **Pre-commit**: Security checks before code commits
- **CI/CD**: Security scanning in automated pipeline
- **Manual Review**: Security review for high-risk changes
- **Compliance Validation**: STIG and CMMC2.0 compliance checks

### Security Tools Integration
- **Bandit**: Python security linter
- **Semgrep**: Advanced code analysis
- **Safety**: Dependency vulnerability scanner
- **Gitleaks**: Git history secret scanner
- **Trivy**: Container and filesystem security scanner

## Documentation Standards

### Code Documentation
- **Docstrings**: Google style docstrings for all functions
- **Type Hints**: Comprehensive type hinting
- **Comments**: Clear, concise comments for complex logic
- **Examples**: Usage examples for public APIs

### Project Documentation
- **README**: Comprehensive project documentation
- **API Documentation**: Auto-generated API documentation
- **Tutorials**: Step-by-step guides and tutorials
- **Architecture**: System architecture and design documentation

## Release Management

### Versioning Strategy
- **Semantic Versioning**: MAJOR.MINOR.PATCH versioning
- **Release Notes**: Comprehensive release notes for each version
- **Changelog**: Detailed changelog with breaking changes
- **Deprecation Policy**: Clear deprecation and removal policies

### Release Process
1. **Feature Freeze**: Feature freeze for release preparation
2. **Testing**: Comprehensive testing and validation
3. **Security Scan**: Final security scan and vulnerability assessment
4. **Documentation**: Update release documentation
5. **Tagging**: Git tagging and version release
6. **Distribution**: Package distribution and deployment

## Development Tools and Configuration

### Development Tools
- **IDE Configuration**: Project-specific IDE configuration
- **EditorConfig**: Consistent editor configuration
- **Pre-commit Hooks**: Automated code quality checks
- **Testing Framework**: pytest with comprehensive configuration

### Configuration Files
- **pyproject.toml**: Project configuration and tool settings
- **pytest.ini**: Test configuration and settings
- **.pre-commit-config.yaml**: Pre-commit hook configuration
- **.github/workflows/**: CI/CD pipeline configuration

## Best Practices

### Code Quality
- **Readability**: Write clear, readable code
- **Maintainability**: Write maintainable, extensible code
- **Performance**: Consider performance implications
- **Security**: Implement security best practices

### Collaboration
- **Code Reviews**: Participate in code reviews
- **Documentation**: Keep documentation up to date
- **Communication**: Clear communication with team members
- **Knowledge Sharing**: Share knowledge and expertise

### Problem Solving
- **Root Cause Analysis**: Identify root causes of issues
- **Testing**: Comprehensive testing of solutions
- **Documentation**: Document solutions and workarounds
- **Monitoring**: Monitor for regression and performance issues

## Getting Started

Ready to start developing with Q-Mini-WASM? Follow the [Development Workflow](Development) guide to set up your development environment and begin contributing to this cutting-edge quantum computing framework.

---

**Last Updated**: 2026-03-22
**Version**: 1.4.1