# Q-Mini-WASM

[![CI](https://github.com/kennetholsenatm-gif/LLM_Pract/actions/workflows/ci.yml/badge.svg)](https://github.com/kennetholsenatm-gif/LLM_Pract/actions/workflows/ci.yml)

Q-Mini-WASM is a quantum computing framework that combines WebAssembly (WASM) with quantum machine learning capabilities. This project demonstrates DevSecOps principles with comprehensive security scanning, compliance checks, and automated testing.

**New to deployment?** See **[Greenfield Deployment Guide](docs/Greenfield-Deployment.md)** for a step-by-step, zero-to-hero deployment on a clean, air-gapped ruggedized node (Packer golden image → Security Stack → Data Stack → Application).

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
- Docker (for development). On Windows, ensure Docker is on your PATH or set `DOCKER_PATH` in `.env` (e.g. `C:\Program Files\Docker\Docker\resources\bin`). Use the repo root (e.g. `C:\GitHub\LLM_Pract`) as the build context for `docker build`.
- GitHub account

### Installation
```bash
# Clone the repository
git clone https://github.com/kennetholsenatm-gif/LLM_Pract.git
cd LLM_Pract

# Install dependencies
pip install -r requirements.txt

# Install pre-commit hooks
pre-commit install
pre-commit install --hook-type commit-msg   # Validate conventional commit messages

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

## Platform Architecture

The repository includes four main pillars:

| Layer | Location | Purpose |
|-------|----------|---------|
| **Host appliance** | [infra/image-builder/](infra/image-builder/) | Packer + QEMU: build hardened AlmaLinux 9 QCOW2 golden image with K3s and STIG-like hardening (tactical edge). |
| **Data stack** | [containers/data-stack/](containers/data-stack/) | Event-driven pipeline: PostgreSQL (pgvector), RabbitMQ, Apache NiFi; vertically scalable, air-gap-friendly. |
| **Security stack** | [containers/security-stack/](containers/security-stack/) | Zero Trust / PQC-ready: Keycloak (FIDO2/Passkeys), Vault (PKI/mTLS), Envoy (TLS 1.3 + ML-KEM). |
| **Application** | [qminiwasm/](qminiwasm/), [wui/](wui/), [charts/](charts/) | Core engine, FastAPI/React WUI, Helm chart for Kubernetes. |

Optional Kubernetes/OpenTofu: [infra/opentofu/](infra/opentofu/) (Teleport, Kyverno, Falco), [infra/teleport/](infra/teleport/), [infra/kyverno/](infra/kyverno/), [infra/falco/](infra/falco/). See [docs/Greenfield-Deployment.md](docs/Greenfield-Deployment.md) for full deployment order.

## Project Structure

```
LLM_Pract/
├── qminiwasm/           # Core Python package (quantum, WASM, hardware)
├── wui/                 # Web UI: FastAPI backend, React frontend
├── charts/              # Helm chart (qminiwasm-wui)
├── containers/          # Docker Compose stacks
│   ├── data-stack/      # PostgreSQL, RabbitMQ, NiFi (EDA)
│   └── security-stack/  # Keycloak, Vault, Envoy (Zero Trust / PQC)
├── infra/               # Infrastructure as Code
│   ├── image-builder/   # Packer + QEMU (AlmaLinux 9 golden image)
│   ├── opentofu/        # OpenTofu: Kubernetes, Teleport, Kyverno, Falco
│   ├── teleport/        # Teleport Helm values and roles
│   ├── kyverno/         # Kyverno policies
│   └── falco/           # Falco runtime security
├── scripts/             # Automation (devsecops-workflow.ps1, security/run_openscap_scan.py)
├── tests/               # Test suites
├── wiki/                # Wiki source (synced to GitHub Wiki)
├── docs/                # Documentation (DockerOS standard, Greenfield guide)
├── requirements/        # Split dependency files
├── .github/workflows/   # CI/CD pipelines
├── SECURITY.md          # Security documentation
├── CONTRIBUTING.md      # Contribution guidelines
└── README.md            # This file
```

## Security Features

### DevSecOps Implementation
- **CI**: [.github/workflows/ci.yml](.github/workflows/ci.yml) – lint (Black, Flake8), MyPy, tests, Bandit, pip-audit; optional Semgrep.
- **Security workflow**: [.github/workflows/security.yml](.github/workflows/security.yml) – weekly/release: dependency audit, Gitleaks (secrets), Trivy, SBOM.
- **Security Scans**: [.github/workflows/security-scans.yml](.github/workflows/security-scans.yml) – on PR/push to main: Bandit, Trivy, OpenSCAP; uploads OpenSCAP HTML report.
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

### Security & Compliance (OpenSCAP)

OpenSCAP is integrated to audit **low-level hardware configuration** and **container/host security posture** required by the Q-Mini-WASM execution engine (Intel ARC kernel drivers, WASM sandboxing, and Kubernetes deployments). It enforces baselines such as CIS benchmarks and NIST frameworks.

- **Inference image:** [docker/Dockerfile.inference](docker/Dockerfile.inference) includes the OpenSCAP scanner (`libopenscap8`, `openscap-utils`) so compliance scans can run inside the container or against a host that uses this stack.
- **Automation:** [scripts/security/run_openscap_scan.py](scripts/security/run_openscap_scan.py) provides a CLI to run `oscap xccdf eval`, produce an HTML report and ARF (Asset Reporting Format) XML, and exit non-zero when critical or kernel/memory-related violations are found (important for Intel driver paging).
- **CI:** The [Security Scans](.github/workflows/security-scans.yml) workflow (on PRs and pushes to `main`) installs security deps (`pip install -e .[security]`), runs Bandit and Trivy, then runs the OpenSCAP script and uploads `openscap_report.html` as an artifact.

**Run the OpenSCAP scanner locally:**

1. Install the scanner and content. This project is AlmaLinux/RHEL focused; use the appropriate commands for your OS:
   - **AlmaLinux / RHEL / Rocky:** `sudo dnf install -y openscap openscap-utils`
   - **Debian/Ubuntu:** `sudo apt-get install -y libopenscap8 openscap-utils`
   For SCAP Security Guide content, download a [ComplianceAsCode/content](https://github.com/ComplianceAsCode/content) release and set `OSCAP_CONTENT_PATH` to the unpacked directory.

2. Install the project with the security extra and run the script:
   ```bash
   pip install -e ".[security]"
   python scripts/security/run_openscap_scan.py --report-html openscap_report.html --results-arf openscap_results.arf.xml
   ```
   Use `--content-path /path/to/scap-security-guide` if content is not under `OSCAP_CONTENT_PATH`. Use `--no-fail-on-critical` to generate reports without failing on critical findings.

3. Open `openscap_report.html` to review compliance regressions (kernel parameters, memory settings, and other CIS/NIST checks).

### Security Policies
- No hardcoded secrets allowed
- All code must pass security scans
- Dependencies regularly updated
- Security impact assessed for all changes

## Development Workflow

### Pre-commit Hooks
Install both hook types so commit messages are validated:
- `pre-commit install` – code quality (Black, Flake8, etc.)
- `pre-commit install --hook-type commit-msg` – conventional commit format

Commit messages must follow: **type(scope): description** (e.g. `feat(wui): add local hardware`). Template: [.gitmessage](.gitmessage). Types: feat, fix, docs, style, refactor, test, chore, security.

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

### Platform standard (DockerOS)
We standardize on **AlmaLinux 9** (and AlmaLinux 10 when available), **Foreman** (Satellite FOSS equivalent), and **Foreman Smart Proxy** (Capsule equivalent) for hosts running Docker/Kubernetes and for container base images. **CI is AlmaLinux/RHEL focused:** lint, tests, and SAST run inside AlmaLinux 9; container images are AlmaLinux-based. See [docs/DockerOS-Platform-Standard.md](docs/DockerOS-Platform-Standard.md).

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

### GitFlow Branching Model

This repository uses a GitFlow-inspired branching model with the following branch types:

#### Main Branches
- **master**: Production-ready code (protected)
- **develop**: Integration branch for new features (protected)

#### Feature Branches
- **feature/**: New features and functionality
  - Example: `feature/user-authentication`
  - Created from: develop
  - Merged into: develop

#### Bug Fix Branches
- **bugfix/**: Bug fixes and patches
  - Example: `bugfix/login-bug`
  - Created from: develop
  - Merged into: develop

#### Release Branches
- **release/**: Release preparation and stabilization
  - Example: `release/v1.0.0`
  - Created from: develop
  - Merged into: master and develop

#### Security Branches
- **security/**: Security-related changes and fixes
  - Example: `security/critical-fix`
  - Created from: develop
  - Merged into: develop

#### Development Branches
- **dev/**: Experimental development and testing
  - Example: `dev/new-experiment`
  - Created from: develop
  - Merged into: develop

#### Workflow
1. Create feature/bugfix branches from develop
2. Submit pull requests for all changes
3. Code review required for all pull requests
4. Merge approved changes back to develop
5. Create release branches for production deployments

#### Rules
- Direct pushes to master and develop are prohibited
- All changes must go through pull requests
- Minimum 1 reviewer required for all pull requests
- Branch names must follow the specified conventions

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