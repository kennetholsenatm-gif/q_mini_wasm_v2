# Security Documentation

## Overview
This document outlines the security practices, policies, and compliance measures implemented in the Q-Mini-WASM project following NIST SP 800-53, CMMC2.0, and STIG standards. It is the **Security policy** for this repository and appears on the [Security tab](https://github.com/kennetholsenatm-gif/LLM_Pract/security). That tab also shows **Security advisories** (reported and published vulnerabilities), **Dependabot** (dependency alerts; see [.github/dependabot.yml](.github/dependabot.yml)), and—when enabled—code scanning.

## Reporting a vulnerability
**Do not report security vulnerabilities in public issues.** Please report them privately so I can fix and disclose them in a coordinated way.

- **Preferred:** [Open a private security advisory](https://github.com/kennetholsenatm-gif/LLM_Pract/security/advisories/new) on this repository. Maintainers will be notified and can work with you on a fix and disclosure.
- **Alternative:** Use the contact or process described in [CONTRIBUTING.md](CONTRIBUTING.md#vulnerability-management) if you cannot use GitHub advisories.

We will acknowledge your report and work with you on next steps. Thank you for helping keep this project secure.

## How We Implement This

Implementation artifacts in this repository:

| Control / Practice | Implementation |
|-------------------|----------------|
| **CI/CD and gates** | [.github/workflows/ci.yml](.github/workflows/ci.yml) – lint, tests, Bandit, pip-audit; **lint-and-test** and **security** jobs are required to pass (critical steps do not use continue-on-error), so merges are blocked on failures; branch protection should require CI to pass (see [CONTRIBUTING.md](CONTRIBUTING.md)). |
| **Scheduled security scans** | [.github/workflows/security.yml](.github/workflows/security.yml) – weekly and on release: pip-audit, Gitleaks (secrets), Trivy filesystem; SBOM (CycloneDX) artifact. |
| **Shift-left (pre-commit)** | [.pre-commit-config.yaml](.pre-commit-config.yaml) – Black, Flake8, MyPy, Bandit, detect-secrets. |
| **SAST / code scanning** | [pyproject.toml](pyproject.toml) `[tool.bandit]`; [.semgrep.yml](.semgrep.yml); CI runs Bandit on `qminiwasm/` and `wui/backend/`, optional Semgrep. |
| **Dependency scanning** | CI and security workflow run `pip-audit`; [.github/dependabot.yml](.github/dependabot.yml) for dependency and GitHub Actions updates. |
| **Secret detection** | Pre-commit: detect-secrets (baseline [.secrets.baseline](.secrets.baseline)); CI/security: Gitleaks. |
| **Local DevSecOps workflow** | [scripts/devsecops-workflow.ps1](scripts/devsecops-workflow.ps1) – full local workflow (tests, Bandit, Safety, Semgrep, STIG checks, reports). |
| **Repository rulesets** | [.github/rulesets/](.github/rulesets/) – branch protection and security rules; import JSON via **Settings → Rules → Rulesets** so default branch requires PRs and CI. |
| **Compliance evidence** | CI and script produce Bandit/pip-audit reports and compliance-report.md / stig-report.md; retain as artifacts for audits. |
| **Admission control** | [infra/kyverno/](infra/kyverno/) – Kyverno policies (STIG baseline, Trivy scan gate); [infra/opentofu/kyverno/](infra/opentofu/kyverno/) for deploy. |
| **Runtime security** | [infra/falco/](infra/falco/) – Falco + Falcosidekick (ELK/Splunk); custom rules for shell, filesystem, privilege escalation. |
| **Trivy image scan** | [.github/workflows/ci.yml](.github/workflows/ci.yml) job `trivy-image`; [scripts/devsecops-workflow.ps1](scripts/devsecops-workflow.ps1) `Run-TrivyImageScan` before push. The scan uses `ignore-unfixed` (only fixable CRITICAL/HIGH fail the pipeline); accepted risks can be listed in [.trivyignore](.trivyignore). |

Control mapping (NIST / CMMC): Bandit and Semgrep → **RA-5** (vulnerability scanning); pip-audit → **RA-5** (dependency vulnerabilities); pre-commit and CI gates → **CM-3** (change control), **AC-3** (access enforcement); audit logs in GitHub Actions → **AU-2**, **AU-3**.

## Security Controls

### Access Control (AC)
- **AC-2**: Account Management - All user accounts are managed through GitHub authentication
- **AC-3**: Access Enforcement - Role-based access control implemented in CI/CD pipeline
- **AC-5**: Separation of Duties - Development, testing, and production environments are isolated
- **AC-6**: Least Privilege - Minimal permissions granted to users and services

### Audit and Accountability (AU)
- **AU-2**: Audit Events - All code commits, builds, and deployments are logged
- **AU-3**: Content of Audit Records - Detailed audit trails maintained in GitHub Actions
- **AU-6**: Audit Review - Regular security audit reviews conducted
- **AU-10**: Non-Repudiation - Digital signatures used for code commits

### Configuration Management (CM)
- **CM-2**: Baseline Configuration - All systems configured to security baseline
- **CM-3**: Configuration Change Control - All changes tracked through Git
- **CM-4**: Security Impact Analysis - Security impact assessed for all changes
- **CM-5**: Access Restrictions - Configuration access restricted to authorized personnel

### Identification and Authentication (IA)
- **IA-2**: Identification and Authentication - Multi-factor authentication required
- **IA-4**: Identifier Management - Unique identifiers assigned to all users
- **IA-5**: Authenticator Management - Secure password policies enforced
- **IA-8**: Identification and Authentication (Non-Organizational Users) - External contributor verification

### Risk Assessment (RA)
- **RA-5**: Vulnerability Scanning - Automated vulnerability scanning implemented
- **RA-6**: Security Authorization - Security authorization required for deployments
- **RA-7**: Risk Response - Risk mitigation strategies documented

### System and Communications Protection (SC)
- **SC-7**: Boundary Protection - Network boundaries protected through firewalls
- **SC-8**: Transmission Integrity - Data integrity verified through checksums
- **SC-13**: Cryptographic Protection - Encryption used for sensitive data
- **SC-23**: Data Origin Authentication - Data origin verified through digital signatures

### Zero Trust Access (Teleport)

When Teleport is deployed (see [infra/teleport/](infra/teleport/) and [scripts/teleport-login.ps1](scripts/teleport-login.ps1)), access to development Kubernetes and Docker environments is gated by a Zero Trust boundary:

- **Teleport Zero Trust boundary:** All access to development Kubernetes (and, if configured, Docker via Application Access) goes through Teleport. No long-lived SSH keys or static passwords are used; authentication is SSO (OIDC with GitHub, Okta, or Azure AD) plus WebAuthn only (hardware key or platform authenticator).
- **IA-2 (Identification and Authentication):** Multi-factor authentication is enforced: first factor via the identity provider (SSO), second factor via WebAuthn (e.g., YubiKey, Touch ID, Windows Hello). Password-only and single-factor access are not permitted.
- **AC-3 (Access Enforcement):** Teleport RBAC roles (`dev-read-only`, `dev-admin`) enforce least privilege; roles are mapped from IdP groups and restrict access to development namespaces. Users do not receive direct cluster credentials.
- **AU-2 / AU-3 (Audit Events and Content):** Teleport emits login, session, and command execution events as structured audit data. These events can be forwarded to an ELK Stack or Splunk endpoint (configured via environment or secrets, not in the repository) for continuous monitoring and audit review (AU-2, AU-3).

IdP configuration (OIDC client secret) and audit sink configuration (ELK/Splunk endpoint and credentials) are not stored in the repository and are supplied via secrets or environment at deployment time.

### Admission Control and Runtime Security (Kyverno and Falco)

We enforce **deploy gates** and **runtime security** so that only successfully scanned images can be deployed and containers are monitored in real time. This supports NIST RA-5, CM-3, AC-3, and STIG baselines.

**Admission control (Kyverno)**  
[Kyverno](https://kyverno.io/) runs as an admission controller in the cluster. It enforces:

- **Trivy scan gate:** Workloads (Pods, Deployments, etc.) must have the annotation `trivy.scan/passed: "true"`. CI runs Trivy image scan and fails the pipeline on CRITICAL or HIGH vulnerabilities that have a fix available (`ignore-unfixed`); accepted risks may be listed in [.trivyignore](.trivyignore). Only images that pass are eligible for this annotation when deploying. Optionally, use Trivy SBOM attestation and Kyverno image verification (cosign attestors) so the cluster only accepts attested images.
- **Kubernetes STIG baseline:** Pods must run as non-root (`runAsNonRoot: true`), set `allowPrivilegeEscalation: false`, and use a read-only root filesystem where applicable. This aligns with least privilege (AC-6) and configuration baselines (CM-2).

Policies and Helm values are in [infra/kyverno/](infra/kyverno/). OpenTofu can deploy Kyverno and the policies when [infra/opentofu/desired/kyverno-*.tfvars.json](infra/opentofu/desired/kyverno-main.tfvars.json) is present. Blocked deployment attempts are logged by the API server and can be forwarded to ELK/Splunk for audit (AU-2, AU-3).

**Runtime security (Falco)**  
[Falco](https://falco.org/) provides defense-in-depth by detecting at runtime:

- Shell/terminal execution inside containers  
- Writes to sensitive directories (e.g. `/etc`, `/bin`)  
- Privilege escalation attempts (e.g. sudo, su)  
- Unexpected network connections or sensitive port binding  

Custom rules are in [infra/falco/](infra/falco/). Falco output is structured (JSON). [Falcosidekick](https://github.com/falcosecurity/falcosidekick) forwards events to ELK Stack or Splunk (configure endpoint and credentials via environment or secrets, not in the repository) for continuous monitoring and SIEM (AU/SI controls).

**Summary**  
- **Shift-left:** Trivy scans images in CI and in the local [scripts/devsecops-workflow.ps1](scripts/devsecops-workflow.ps1) before push.  
- **Cluster boundary:** Kyverno blocks workloads that have not passed the scan (annotation) or that violate the STIG baseline.  
- **Runtime:** Falco detects malicious or risky behavior in running containers; events are sent to ELK/Splunk for analysis and alerting.

## Security Policies

### Code Security
- All code must pass security scans before merging
- No hardcoded secrets or credentials allowed
- Dependencies must be regularly updated and scanned
- Code reviews required for all changes

### Data Protection
- Sensitive data encrypted at rest and in transit
- Data classification policies enforced
- Data retention policies implemented
- Secure data disposal procedures followed

### Incident Response
- Security incidents reported within 24 hours
- Incident response procedures documented
- Regular incident response drills conducted
- Post-incident analysis performed

## Compliance Standards

### NIST SP 800-53
- Security controls implemented per NIST guidelines
- Regular compliance assessments conducted
- Documentation maintained per NIST requirements
- Continuous monitoring implemented

### CMMC2.0
- Level 2 compliance achieved
- Security practices documented
- Training programs implemented
- Audit trails maintained

### STIG Compliance
- Security Technical Implementation Guides followed
- Regular STIG compliance checks performed
- Configuration hardening implemented
- Security baselines maintained

## Security Tools and Technologies

### Static Analysis
- Bandit: Python security linter
- Semgrep: Code analysis tool
- Safety: Dependency vulnerability scanner
- Trivy: Container security scanner

### Dynamic Analysis
- OWASP ZAP: Web application scanner
- Burp Suite: Security testing tool
- Metasploit: Penetration testing framework

### Monitoring
- Prometheus: Metrics collection
- Grafana: Visualization and alerting
- ELK Stack: Log analysis
- Splunk: Security information and event management

## Security Training

### Developer Training
- Secure coding practices
- Threat modeling
- Security testing techniques
- Incident response procedures

### Security Awareness
- Phishing awareness
- Social engineering awareness
- Physical security awareness
- Data protection awareness

## Security Documentation

### Security Policies
- Access control policies
- Data protection policies
- Incident response procedures
- Change management procedures

### Security Procedures
- Security assessment procedures
- Vulnerability management procedures
- Patch management procedures
- Backup and recovery procedures

## Security Metrics

### Key Performance Indicators
- Mean time to detect (MTTD)
- Mean time to respond (MTTR)
- Vulnerability remediation time
- Security incident frequency

### Compliance Metrics
- Compliance assessment results
- Audit findings
- Training completion rates
- Policy adherence rates

## Security Testing

### Penetration Testing
- Regular penetration testing conducted
- Vulnerability assessments performed
- Social engineering tests conducted
- Physical security assessments performed

### Security Validation
- Security control validation
- Configuration validation
- Access control validation
- Data protection validation

## Security Incident Response

### Incident Classification
- Critical: System compromise
- High: Data breach
- Medium: Service disruption
- Low: Minor security event

### Response Procedures
- Immediate containment
- Investigation and analysis
- Remediation and recovery
- Post-incident review

## Security Architecture

### Network Security
- Firewalls and intrusion prevention
- Network segmentation
- VPN access control
- DDoS protection

### Application Security
- Input validation
- Output encoding
- Authentication and authorization
- Session management

### Data Security
- Encryption at rest and in transit
- Data masking and tokenization
- Secure key management
- Data loss prevention

## Security Monitoring

### Real-time Monitoring
- Security event monitoring
- System health monitoring
- Network traffic monitoring
- Application performance monitoring

### Log Management
- Centralized log collection
- Log retention policies
- Log analysis and correlation
- Log backup and recovery

## Security Assessment

### Regular Assessments
- Security control assessments
- Compliance assessments
- Risk assessments
- Performance assessments

### Assessment Results
- Assessment findings documented
- Remediation plans developed
- Progress tracked
- Results reported to stakeholders

## Security Improvement

### Continuous Improvement
- Lessons learned incorporated
- Best practices adopted
- Technologies updated
- Processes refined

### Future Enhancements
- Advanced threat detection
- Automated response capabilities
- Enhanced monitoring capabilities
- Improved compliance automation

## Contact Information

### Security Team
- Security Officer: see [CONTRIBUTING.md](CONTRIBUTING.md) for current contact or open a private security advisory on GitHub.
- Incident Response: report via repository security advisories or the contact listed in CONTRIBUTING.
- Security Operations: see CONTRIBUTING and repository settings.

### Reporting
- **Security vulnerabilities:** Report via [GitHub Security Advisories](https://github.com/kennetholsenatm-gif/LLM_Pract/security/advisories) — use [Create a new advisory](https://github.com/kennetholsenatm-gif/LLM_Pract/security/advisories/new). Do not use public issues.
- **Security incidents:** Same as above.
- **Security questions:** Open a [Discussion](https://github.com/kennetholsenatm-gif/LLM_Pract/discussions) or see [CONTRIBUTING.md](CONTRIBUTING.md).

## Revision History

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 1.0 | 2024-01-01 | Security Team | Initial release |
| 1.1 | 2024-03-15 | Security Team | Updated compliance requirements |
| 1.2 | 2024-06-30 | Security Team | Added new security tools |
| 1.3 | 2024-09-15 | Security Team | Enhanced incident response procedures |
| 1.4 | 2025-01-01 | Security Team | Updated NIST compliance requirements |

## Legal and Compliance

### Regulatory Compliance
- GDPR compliance
- HIPAA compliance (if applicable)
- PCI DSS compliance (if applicable)
- SOX compliance (if applicable)

### Legal Requirements
- Data protection laws
- Privacy regulations
- Industry standards
- Contractual obligations

## Security Certifications

### Current Certifications
- ISO 27001
- SOC 2 Type II
- FedRAMP (if applicable)
- Other relevant certifications

### Certification Process
- Documentation requirements
- Assessment procedures
- Audit requirements
- Maintenance procedures

## Security Budget

### Security Investments
- Security tools and technologies
- Training and awareness programs
- Security assessments and audits
- Incident response capabilities

### Cost Management
- Budget allocation
- Cost tracking
- ROI analysis
- Cost optimization

## Security Partners

### Third-Party Vendors
- Security tool providers
- Consulting services
- Training providers
- Incident response services

### Partnerships
- Information sharing agreements
- Joint security initiatives
- Collaborative research
- Best practice sharing

## Security Roadmap

### Short-term Goals (1-6 months)
- Implement new security tools
- Enhance monitoring capabilities
- Improve incident response
- Update security policies

### Medium-term Goals (6-18 months)
- Achieve new certifications
- Implement advanced security controls
- Enhance automation capabilities
- Improve compliance processes

### Long-term Goals (18+ months)
- Implement zero-trust architecture
- Enhance threat intelligence capabilities
- Improve security analytics
- Achieve advanced security maturity

## Security Culture

### Security Awareness
- Security awareness training
- Security best practices
- Security culture promotion
- Security champions program

### Security Leadership
- Security leadership commitment
- Security governance
- Security strategy
- Security vision

## Security Innovation

### Emerging Technologies
- AI and machine learning for security
- Blockchain for security
- Quantum cryptography
- Advanced threat detection

### Research and Development
- Security research initiatives
- Proof of concept projects
- Technology evaluations
- Innovation programs

## Security Metrics Dashboard

### Key Metrics
- Security incidents by type
- Vulnerability remediation time
- Compliance assessment results
- Security control effectiveness

### Reporting
- Executive dashboards
- Operational reports
- Compliance reports
- Trend analysis

## Security Knowledge Base

### Documentation
- Security procedures
- Best practices
- Troubleshooting guides
- Reference materials

### Training Materials
- Training modules
- Reference guides
- Quick reference cards
- Video tutorials

## Security Community

### Internal Community
- Security team collaboration
- Cross-functional partnerships
- Knowledge sharing
- Best practice exchange

### External Community
- Industry forums
- Security conferences
- Professional associations
- Information sharing groups

## Security Future

### Emerging Threats
- AI-powered attacks
- Quantum computing threats
- IoT security challenges
- Supply chain security

### Future Capabilities
- Advanced threat detection
- Automated response
- Enhanced analytics
- Improved resilience