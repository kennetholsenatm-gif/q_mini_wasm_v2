# DevSecOps Implementation

## Overview

Q-Mini-WASM implements a comprehensive DevSecOps framework that integrates security practices throughout the entire development lifecycle, from initial code commit to production deployment. The platform also includes infrastructure-as-code (Packer, OpenTofu), container-based data and security stacks, and Kubernetes admission/runtime controls.

## CI/CD Pipeline Architecture

### GitHub Actions Workflows

The project uses GitHub Actions for automated CI/CD with these workflows:

#### CI Workflow (`.github/workflows/ci.yml`)
- **Linting**: Black, Flake8
- **Type Checking**: MyPy
- **Testing**: Unit tests with coverage (pytest, coverage.xml)
- **Security**: Bandit SAST, pip-audit dependency scanning
- **Trivy image**: Builds `wui/backend/Dockerfile`, runs Trivy with CRITICAL/HIGH severity and `ignore-unfixed` (only fixable vulns fail the job); uploads SARIF and SBOM (CycloneDX). Accepted risks can be listed in [.trivyignore](.trivyignore). Image scan can be gated in-cluster via Kyverno.

#### Security Scans Workflow (`.github/workflows/security-scans.yml`)
- **Trigger**: Push and pull requests to `main`/`master`
- **Steps**: Install security deps (`requirements/security.txt` or bandit, pip-audit, click, loguru), run Bandit, pip-audit, Trivy filesystem scan, and **OpenSCAP** via `scripts/security/run_openscap_scan.py`
- **Artifacts**: OpenSCAP HTML report, Bandit and pip-audit reports (uploaded when present)

#### Security Workflow (`.github/workflows/security.yml`)
- **Scheduled / release**: Dependency audit (pip-audit), Gitleaks (secrets), Trivy, SBOM (CycloneDX)

## Pre-commit Hooks

### Configuration (`.pre-commit-config.yaml`)

The project implements shift-left security through pre-commit hooks that run automatically before code commits:

#### Quality Hooks
- **Black**: Code formatting with 100-character line length
- **Flake8**: Python linting with custom configuration
- **MyPy**: Static type checking for type safety
- **Debug Statements**: Detection of debug statements and print statements

#### Security Hooks
- **Bandit**: Python security linter with custom configuration
- **Detect-secrets**: Secret detection with baseline comparison
- **Private Key Detection**: Identification of private key files
- **YAML/JSON Validation**: Configuration file validation

## Security Scanning Implementation

### Static Application Security Testing (SAST)
- **Bandit**: Python-specific security linter
- **Semgrep**: Advanced pattern-based code analysis
- **MyPy**: Type-based vulnerability detection
- **Custom Rules**: Project-specific security rules and patterns

### Dependency Security
- **pip-audit**: Python package vulnerability scanning (primary)
- **Safety**: Listed in `requirements/security.txt`; optional
- **Dependabot**: Automated dependency updates (`.github/dependabot.yml`)
- **SBOM Generation**: Trivy CycloneDX in CI; usable for attestation and Kyverno verifyImages

### Secret Detection
- **Gitleaks**: Git history secret scanning
- **Detect-secrets**: Pre-commit secret detection
- **Baseline Management**: Secret baseline for known safe patterns
- **Custom Patterns**: Project-specific secret detection rules

## Compliance Implementation

### STIG Compliance
- **Security Technical Implementation Guides**: DoD security standards
- **Automated Checks**: STIG compliance validation in CI/CD
- **Configuration Hardening**: Security baseline configuration
- **Documentation**: STIG compliance reports and evidence

### CMMC2.0 Compliance
- **Maturity Model Certification**: Cybersecurity Maturity Model Certification
- **Practice Implementation**: CMMC2.0 security practices integration
- **Assessment Readiness**: Documentation and evidence for audits
- **Continuous Monitoring**: Ongoing compliance validation

### NIST SP 800-53
- **Control Implementation**: NIST security control implementation
- **Control Mapping**: Security controls to specific implementations
- **Assessment Framework**: NIST-based security assessment
- **Documentation**: Control implementation evidence and reports

## Local DevSecOps Workflow

### Development Environment Setup
```bash
# Install pre-commit hooks
pre-commit install

# Run complete DevSecOps workflow
pwsh -File scripts/devsecops-workflow.ps1
```

### Workflow Components
- **Code Quality**: Black formatting, Flake8 linting, MyPy type checking
- **Security Scanning**: Bandit, Safety, Semgrep, Gitleaks
- **Compliance Checks**: STIG validation, CMMC2.0 assessment
- **Testing**: Unit tests, integration tests, security tests
- **Reporting**: Comprehensive security and compliance reports

## Branch Protection and Governance

### GitHub Branch Protection
- **Required Status Checks**: CI must pass before merging
- **Code Review Requirements**: Mandatory peer review
- **Security Checks**: Security scans required for all changes
- **Admin Restrictions**: Limited administrative access

### Repository Rulesets
- **Branch Protection**: Default branch requires PRs and CI
- **Security Rules**: Security-related repository configurations
- **Access Control**: Role-based access management
- **Audit Logging**: Comprehensive audit trail for all changes

## Security Policies and Procedures

### Code Security Policies
- **No Hardcoded Secrets**: Strict prohibition of hardcoded credentials
- **Security Review Required**: Security impact assessment for all changes
- **Dependency Management**: Regular dependency updates and scanning
- **Code Review Process**: Security-focused code review procedures

### Incident Response
- **Security Incident Reporting**: 24-hour reporting requirement
- **Response Procedures**: Documented incident response processes
- **Post-Incident Analysis**: Lessons learned and improvement planning
- **Communication Protocols**: Stakeholder notification procedures

## Security Tools and Technologies

### Static Analysis Tools
- **Bandit**: Python security linter
- **Semgrep**: Advanced code analysis
- **MyPy**: Type-based vulnerability detection
- **Safety**: Dependency vulnerability scanner

### Dynamic Analysis Tools
- **Trivy**: Container and filesystem security scanner
- **Gitleaks**: Git history secret scanner
- **OWASP ZAP**: Web application security scanner
- **Burp Suite**: Advanced security testing

### Monitoring and Logging
- **Prometheus**: Metrics collection and alerting
- **Grafana**: Visualization and dashboard creation
- **ELK Stack**: Log aggregation and analysis
- **Splunk**: Security information and event management

## Security Training and Awareness

### Developer Training
- **Secure Coding Practices**: Training on secure development techniques
- **Threat Modeling**: Understanding and mitigating security threats
- **Security Testing**: Security testing methodologies and tools
- **Incident Response**: Security incident handling procedures

### Security Awareness Programs
- **Phishing Awareness**: Training on social engineering attacks
- **Physical Security**: Physical security best practices
- **Data Protection**: Data classification and handling procedures
- **Compliance Training**: Regulatory compliance awareness

## Security Metrics and Reporting

### Key Performance Indicators
- **Mean Time to Detect (MTTD)**: Security incident detection time
- **Mean Time to Respond (MTTR)**: Security incident response time
- **Vulnerability Remediation Time**: Time to fix security vulnerabilities
- **Security Incident Frequency**: Number of security incidents over time

### Compliance Metrics
- **Compliance Assessment Results**: Regular compliance assessment outcomes
- **Audit Findings**: Security audit results and findings
- **Training Completion Rates**: Security training participation rates
- **Policy Adherence Rates**: Compliance with security policies

## Infrastructure and Stacks (Code-Aligned)

### Host Appliance (Packer / QEMU)
- **Location:** `infra/image-builder/`
- **Purpose:** Golden AlmaLinux 9 QCOW2 with K3s and STIG-like hardening (SSH, chrony, firewalld). Used for tactical edge host baseline.
- **Docs:** [infra/image-builder/README.md](https://github.com/kennetholsenatm-gif/LLM_Pract/blob/main/infra/image-builder/README.md)

### Data Stack (Event-Driven)
- **Location:** `containers/data-stack/`
- **Components:** PostgreSQL (pgvector), RabbitMQ, Apache NiFi; all with `deploy.resources`. Optional mTLS for Postgres via Vault PKI (`postgres-mtls.conf`).
- **Docs:** [containers/data-stack/README.md](https://github.com/kennetholsenatm-gif/LLM_Pract/blob/main/containers/data-stack/README.md)

### Security Stack (Zero Trust / PQC)
- **Location:** `containers/security-stack/`
- **Components:** Keycloak (FIDO2/Passkeys, OIDC for Teleport), Vault (PKI, short-lived mTLS certs), Envoy (TLS 1.3, PQC-ready curves).
- **Docs:** [containers/security-stack/README.md](https://github.com/kennetholsenatm-gif/LLM_Pract/blob/main/containers/security-stack/README.md)

### Kubernetes / OpenTofu
- **Location:** `infra/opentofu/` (Kubernetes provider, Helm releases for Teleport, Kyverno, Falco), `infra/teleport/`, `infra/kyverno/`, `infra/falco/`
- **Admission:** Kyverno policies (e.g. pod-security-stig, image-scan-gate). Runtime: Falco.
- **Deployment:** See `.github/workflows/opentofu-infra.yml` and `infra/opentofu/desired/*.tfvars.json`.

### OpenSCAP Compliance
- **Script:** `scripts/security/run_openscap_scan.py` — runs `oscap xccdf eval`, produces HTML report and ARF; can fail on critical/kernel-memory findings.
- **CI:** Run in Security Scans workflow; report uploaded as artifact. Optional inference image: `docker/Dockerfile.inference` includes OpenSCAP scanner.

## Security Architecture

### Network Security
- **Firewalls**: firewalld on Packer-built image; container stacks use internal bridge networks and minimal exposed ports.
- **Zero Trust**: Teleport for infrastructure access (ephemeral certs); Keycloak OIDC + WebAuthn for human identity.
- **TLS / PQC:** Envoy with TLS 1.3 and ML-KEM/Kyber-ready curves for security stack front-end.

### Application Security
- **Authentication:** Keycloak (Passkeys); Teleport for SSH/Kubernetes.
- **Machine Identity:** Vault PKI for short-lived client certs (mTLS to Postgres).
- **Containers:** Non-root, securityContext (allowPrivilegeEscalation false, drop ALL) in Helm chart and Dockerfiles.

### Data Security
- **Encryption in Transit:** TLS 1.3 at proxy; optional mTLS for database (Vault-issued certs).
- **Secrets:** No hardcoded secrets; `.env` and Vault for credentials; `.env` in `.gitignore`.

## Continuous Improvement

### Security Enhancement Process
- **Lessons Learned**: Post-incident analysis and improvement planning
- **Best Practices Adoption**: Implementation of industry best practices
- **Technology Updates**: Regular security tool and technology updates
- **Process Refinement**: Continuous improvement of security processes

### Future Security Initiatives
- **Advanced Threat Detection**: AI-powered threat detection capabilities
- **Automated Response**: Automated security incident response
- **Enhanced Monitoring**: Advanced security monitoring and analytics
- **Improved Compliance**: Enhanced compliance automation and reporting

## Getting Started with DevSecOps

Ready to implement DevSecOps in your quantum computing projects? Follow the [Development Workflow](Development) guide to set up your development environment with comprehensive security practices.

---

**Last Updated**: 2026-03-12
**Version**: 1.4.0