# Q-Mini-WASM

[![CI](https://github.com/kennetholsenatm-gif/LLM_Pract/actions/workflows/ci.yml/badge.svg)](https://github.com/kennetholsenatm-gif/LLM_Pract/actions/workflows/ci.yml)

Q-Mini-WASM is a quantum computing framework that combines WebAssembly (WASM) with quantum machine learning capabilities. This project demonstrates DevSecOps principles with comprehensive security scanning, compliance checks, and automated testing.

## Overview

Q-Mini-WASM provides a secure, scalable platform for quantum computing applications with:
- **Quantum Computing**: Integration with Qiskit, PennyLane, and [Intel Quantum](https://www.intel.com/content/www/us/en/research/quantum-computing.html) (SDK, Intel QS, Tunnel Falls ecosystem)
- **WebAssembly**: High-performance execution engine
- **AI training**: Intel ARC (XPU) first; CUDA is not used
- **DevSecOps**: Automated security scanning and compliance
- **STIG Compliance**: Security Technical Implementation Guides adherence
- **CMMC2.0**: Cybersecurity Maturity Model Certification compliance

## Features

### Core Capabilities
- Quantum circuit simulation and execution
- WASM module compilation and execution
- Machine learning model integration
- Hardware abstraction layer
- Data pipeline management

### Security Features
- Automated security scanning (Bandit, Safety, Semgrep)
- STIG compliance checks
- CMMC2.0 adherence
- Secret detection
- Dependency vulnerability scanning

### Development Features
- Pre-commit hooks for code quality
- Comprehensive test suite
- CI/CD pipeline with GitHub Actions
- Documentation generation
- Performance monitoring

### Intel Quantum and Intel ARC (XPU)
- **[Intel Quantum](https://www.intel.com/content/www/us/en/research/quantum-computing.html)**: Integration points for Intel Quantum SDK, Intel Quantum Simulator (IQS), and Tunnel Falls silicon spin qubit ecosystem. Use `qminiwasm.quantum.intel_backend.get_intel_quantum_info()` for links and optional backend detection.
- **AI training on Intel ARC**: Device selection prefers **Intel XPU** (e.g. Intel ARC GPUs) over CPU; **CUDA is not used**. Use `qminiwasm.hardware.get_device()` for the default device; install PyTorch with XPU support and optional [Intel Extension for PyTorch](https://intel.github.io/intel-extension-for-pytorch/) for Intel GPUs. See [Wiki: Intel Quantum and ARC](wiki/Intel-Quantum-and-ARC.md).

## Quick Start

### Prerequisites
- Python 3.11+
- Git
- Docker (for development)
- GitHub account

### Installation
```bash
# Clone the repository
git clone https://github.com/your-org/q-mini-wasm.git
cd q-mini-wasm

# Install dependencies
pip install -r requirements.txt

# Install pre-commit hooks
pre-commit install

# Run security scan
bandit -r qminiwasm/
safety check
```

### Running the Application
```bash
# Run tests
pytest tests/ --cov=qminiwasm --cov-report=html

# Verify package
python -c "import qminiwasm; print(qminiwasm.__version__)"

# Run complete workflow (Windows PowerShell)
pwsh -File scripts/devsecops-workflow.ps1
```

## Project Structure

```
LLM_Pract/
├── qminiwasm/           # Application source (Python package)
├── tests/               # Test suites
├── scripts/             # Automation scripts (e.g. devsecops-workflow.ps1)
├── wiki/                # Wiki source (synced to GitHub Wiki)
├── docs/                # Documentation
├── requirements/        # Split dependency files
├── .github/workflows/   # CI/CD pipelines
├── .pre-commit-config.yaml  # Pre-commit hooks
├── SECURITY.md          # Security documentation
├── CONTRIBUTING.md      # Contribution guidelines
└── README.md            # This file
```

## Security Features

### DevSecOps Implementation
- **CI**: [.github/workflows/ci.yml](.github/workflows/ci.yml) – lint (Black, Flake8), MyPy, tests, Bandit, pip-audit; optional Semgrep.
- **Security workflow**: [.github/workflows/security.yml](.github/workflows/security.yml) – weekly/release: dependency audit, Gitleaks (secrets), Trivy, SBOM.
- **Pre-commit**: [.pre-commit-config.yaml](.pre-commit-config.yaml) – Black, Flake8, MyPy, Bandit, detect-secrets.
- **Configs**: [pyproject.toml](pyproject.toml) (Bandit, Black, Flake8), [.semgrep.yml](.semgrep.yml), [.secrets.baseline](.secrets.baseline).
- **Local workflow**: [scripts/devsecops-workflow.ps1](scripts/devsecops-workflow.ps1) – full local DevSecOps run (tests, scans, STIG, reports).
- **Dependabot**: [.github/dependabot.yml](.github/dependabot.yml) – weekly pip and GitHub Actions updates.
- **Security rules (Settings → Rules → Rulesets)**: Import [.github/rulesets/branch-protection-security.json](.github/rulesets/branch-protection-security.json) so the default branch requires PRs and CI; see [.github/rulesets/README.md](.github/rulesets/README.md).
- See [SECURITY.md](SECURITY.md) for policies and control mapping; [CONTRIBUTING.md](CONTRIBUTING.md) for branch protection and checks.

### Wiki
The [GitHub Wiki](https://github.com/kennetholsenatm-gif/LLM_Pract/wiki) is populated from the **`wiki/`** directory in this repo. The [sync-wiki](.github/workflows/sync-wiki.yml) workflow runs on pushes to `main`/`master` that change `wiki/**`, and on **Run workflow**, and deploys `wiki/*.md` to the live wiki. To update the wiki, edit files in **`wiki/`** and push to `main`/`master`, or run the **Sync wiki** workflow from the **Actions** tab. Ensure the repo has **Wiki** enabled in **Settings → General** (and optionally create one page manually once to initialize the wiki). If wiki sync fails, add a repo secret **`WIKI_DEPLOY_TOKEN`** (PAT with `repo` scope) in **Settings → Secrets and variables → Actions**; the workflow uses it when set, otherwise `GITHUB_TOKEN`.

### GitHub Actions and Projects
- **Actions show workflows**: Push this repo (including `.github/workflows/`) to the default branch (e.g. `main`) so the **Actions** tab lists CI and Security.
- **Trigger a run**: Push a commit to `main`/`master` or open a PR to trigger CI; or go to **Actions** → **CI** or **Security** → **Run workflow** to run manually.
- **Re-runs use the same commit**: If a run fails, "Re-run" does not use your latest code. You must **push a new commit** to trigger a new run; then open the **newest** run at the top of the [Actions](https://github.com/kennetholsenatm-gif/LLM_Pract/actions) list (not the old run). The fixed CI is in `.github/workflows/ci.yml` (v2) and `requirements/ci.txt`.
- **Projects + Actions**: [LLM Modeling practice (project #3)](https://github.com/users/kennetholsenatm-gif/projects/3) · [View 1](https://github.com/users/kennetholsenatm-gif/projects/3/views/1) is integrated with [Actions](https://github.com/kennetholsenatm-gif/LLM_Pract/actions):
  - [add-to-project](.github/workflows/add-to-project.yml): new issues and PRs (targeting `main`/`master`) are added to the project automatically.
  - [failed-run-to-issue](.github/workflows/failed-run-to-issue.yml): when **CI** or **Security** fails, an [issue](https://github.com/kennetholsenatm-gif/LLM_Pract/issues) is created and added to project #3.
  **Setup:** Add repo secret **`ADD_TO_PROJECT_TOKEN`** (PAT with **`project`** scope) in **Settings → Secrets and variables → Actions**. Without it, the add-to-project and failed-run-to-issue workflows still run but skip adding items to the project. If failed-run issues are created but do not show up in [project #3](https://github.com/users/kennetholsenatm-gif/projects/3), check that this secret is set; the workflow Summary step will note when the issue was not added.

### Automated Security Scanning
- **Bandit**: Python security linter (config in pyproject.toml)
- **pip-audit / Safety**: Dependency vulnerability scanning
- **Semgrep**: Code analysis (--config=auto in CI)
- **Trivy**: Filesystem/container scan in security workflow

### Compliance Checks
- **STIG Compliance**: Security Technical Implementation Guides
- **CMMC2.0**: Cybersecurity Maturity Model Certification
- **NIST SP 800-53**: Security control implementation
- **File Permissions**: Automated security hardening

### Security Policies
- No hardcoded secrets allowed
- All code must pass security scans
- Dependencies regularly updated
- Security impact assessed for all changes

## Development Workflow

### Pre-commit Hooks
All commits must pass:
- Code formatting (Black, isort)
- Security scanning (Bandit, Safety)
- Secret detection
- Type checking (MyPy)

### CI/CD Pipeline
Automated pipeline (see [.github/workflows/](.github/workflows/)) includes:
- Lint (Black, Flake8) and type check (MyPy)
- Unit tests with coverage
- SAST (Bandit), dependency scan (pip-audit/safety), optional Semgrep
- Optional: secret scanning, Trivy (when using containers)
- Branch protection: require CI to pass before merging (see [CONTRIBUTING.md](CONTRIBUTING.md))

### Testing
Comprehensive test suite:
- Unit tests
- Integration tests
- Security tests
- Performance tests

## Getting Started

### For Developers
1. Set up development environment
2. Run security scan
3. Implement features following guidelines
4. Submit pull request

### For Security Teams
1. Review SECURITY.md
2. Run compliance checks
3. Assess security impact
4. Monitor security metrics

### For Operations Teams
1. Review deployment procedures
2. Set up monitoring
3. Configure infrastructure
4. Implement backup procedures

## Security Documentation

### Security Policies
- Access control policies
- Data protection policies
- Incident response procedures
- Change management procedures

### Compliance Documentation
- STIG compliance reports
- CMMC2.0 documentation
- NIST compliance assessments
- Audit trails

## Performance Considerations

### Optimization
- WASM compilation optimization
- Quantum circuit optimization
- Memory management
- Parallel execution

### Monitoring
- Performance metrics collection
- Resource usage monitoring
- Error tracking
- Health checks

## Troubleshooting

### Common Issues
- Dependency conflicts
- Security scan failures
- Test failures
- Build issues

### Support
- Check documentation
- Review GitHub issues
- Contact security team
- Community support

## Contributing

We welcome contributions! Please review our [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines.

## License

This project is licensed under the MIT License.

## Security

Report security vulnerabilities via [GitHub Security Advisories](https://github.com/kennetholsenatm-gif/LLM_Pract/security/advisories/new) (private); see [SECURITY.md](SECURITY.md#reporting-a-vulnerability). Do not use public issues.

## Support

For support and questions, please open an issue in the GitHub repository.

## Changelog

### v1.0.0 (2024-01-01)
- Initial release
- Basic quantum computing capabilities
- WASM integration
- Security scanning implementation

### v1.1.0 (2024-03-15)
- Enhanced security features
- Improved performance
- Additional quantum algorithms
- Better documentation

### v1.2.0 (2024-06-30)
- STIG compliance implementation
- CMMC2.0 adherence
- Advanced monitoring
- Performance optimizations

## Roadmap

### Short-term (1-3 months)
- Enhanced security features
- Performance improvements
- Additional quantum algorithms
- Better documentation

### Medium-term (3-6 months)
- Advanced monitoring
- Scalability improvements
- Integration enhancements
- Community features

### Long-term (6+ months)
- Advanced security capabilities
- Performance optimizations
- New quantum computing features
- Enterprise features

## Community

### Getting Involved
- Join our community
- Contribute to development
- Report issues
- Provide feedback

### Resources
- Documentation
- Tutorials
- Examples
- Community forums

## Legal Information

### Compliance
- GDPR compliance
- HIPAA compliance (if applicable)
- PCI DSS compliance (if applicable)
- SOX compliance (if applicable)

### Certifications
- ISO 27001
- SOC 2 Type II
- FedRAMP (if applicable)

## Contact Information

- Project: [GitHub repository](https://github.com/kennetholsenatm-gif/LLM_Pract)
- Security: [Report a vulnerability](https://github.com/kennetholsenatm-gif/LLM_Pract/security/advisories/new) (private); see [SECURITY.md](SECURITY.md).
- Community: [GitHub Discussions](https://github.com/kennetholsenatm-gif/LLM_Pract/discussions) or open an issue.

## Acknowledgments

We thank our contributors, security researchers, and community members for their support and feedback.

## Revision History

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 1.0 | 2024-01-01 | Project Team | Initial release |
| 1.1 | 2024-03-15 | Project Team | Updated security features |
| 1.2 | 2024-06-30 | Project Team | Enhanced compliance documentation |
| 1.3 | 2024-09-15 | Project Team | Improved development workflow |
| 1.4 | 2025-01-01 | Project Team | Updated roadmap and features |