# Project Overview

## Introduction to Q-Mini-WASM

Q-Mini-WASM is a quantum computing framework that combines WebAssembly (WASM) with quantum machine learning capabilities. It provides a secure, scalable platform for quantum applications with mature DevSecOps, Zero Trust security, and event-driven data infrastructure suitable for tactical edge and air-gapped deployments.

## Platform Architecture (Current State)

The repository is organized into four main pillars:

### 1. Host Appliance (Packer + QEMU)
- **Location:** `infra/image-builder/`
- **Purpose:** Build a hardened AlmaLinux 9 QCOW2 golden image for tactical edge.
- **Contents:** Packer HCL (QEMU builder), Kickstart (ks.cfg), and a provision script that installs K3s, applies STIG-like hardening (SSH, chrony, firewalld), and creates an `admin` user.
- **Output:** `output-almalinux9/almalinux9-golden.qcow2`. Aligns with [DockerOS Platform Standard](https://github.com/kennetholsenatm-gif/LLM_Pract/blob/main/docs/DockerOS-Platform-Standard.md) (AlmaLinux 9 as host OS).

### 2. Data Stack (Event-Driven Architecture)
- **Location:** `containers/data-stack/`
- **Purpose:** Vertically scalable, air-gap-friendly data ingestion and storage.
- **Components:** PostgreSQL (pgvector for LLM/quantum data), RabbitMQ (event broker), Apache NiFi (data flow engine). All use `deploy.resources` for vertical scaling on a single host.
- **Flow:** WUI backend publishes events to RabbitMQ; NiFi consumes, transforms, and writes final state to PostgreSQL. Optional mTLS for Postgres via Vault-issued client certs (see `postgres-mtls.conf` and security-stack).

### 3. Security Stack (Zero Trust / PQC-Ready)
- **Location:** `containers/security-stack/`
- **Purpose:** Passwordless human and machine identity; TLS 1.3 with post-quantum (ML-KEM) readiness.
- **Components:** Keycloak (FIDO2/Passkeys, OIDC for Teleport), HashiCorp Vault (PKI secrets engine for short-lived mTLS certs), Envoy (reverse proxy with TLS 1.3 and PQC curve options). Dedicated PostgreSQL for Keycloak.
- **Integration:** Teleport uses Keycloak as OIDC IdP; services request client certs from Vault for passwordless database access.

### 4. Application (Core Engine + WUI)
- **Location:** `qminiwasm/` (Python package), `wui/` (FastAPI backend + React frontend), `charts/qminiwasm-wui/` (Helm chart).
- **Purpose:** Quantum circuit simulation, WASM execution, Intel Quantum/ARC integration, and the Web UI for configuration and control.
- **Deployment:** Docker (e.g. `wui/backend/Dockerfile`) or Kubernetes via Helm; chart is STIG/Kyverno-aware (non-root, securityContext, optional Trivy scan annotation).

Optional Kubernetes/OpenTofu: `infra/opentofu/` (Teleport, Kyverno, Falco), `infra/teleport/`, `infra/kyverno/`, `infra/falco/`. See the [Greenfield Deployment Guide](https://github.com/kennetholsenatm-gif/LLM_Pract/blob/main/docs/Greenfield-Deployment.md) in the repository for full deployment order.

## Core Application Architecture

### Quantum Computing Layer
- **PennyLane Integration**: Cross-platform quantum machine learning (primary).
- **Intel Quantum**: Integration points for Intel Quantum SDK, IQS, and Tunnel Falls (see [Intel Quantum and ARC](Intel-Quantum-and-ARC)).
- **Quantum Circuit Simulation**: High-fidelity simulation and hardware abstraction.

### WebAssembly Layer
- **WASM Compilation and Execution**: High-performance WASM runtime (e.g. wasmtime, pywasm).
- **Memory Management and Parallel Execution**: Optimized for quantum and classical workloads.

### Application Layer
- **API Interface**: FastAPI backend (REST) for the WUI and service integration.
- **Data Pipeline**: Event-driven flow via RabbitMQ and NiFi; canonical state in PostgreSQL (pgvector for embeddings).
- **Web UI**: React frontend for configuration, quantum backend selection, and monitoring.

## Technical Specifications

### System Requirements
- **Python Version**: 3.11+
- **Operating Systems**: Linux, macOS, Windows
- **Memory**: Minimum 8GB RAM, recommended 16GB+
- **Storage**: 2GB free space for installation
- **Network**: Internet connection for package downloads

### Dependencies
- **Core Libraries**: Qiskit, PennyLane, NumPy, SciPy
- **Development Tools**: Black, Flake8, MyPy, Bandit
- **Security Tools**: Safety, Semgrep, Gitleaks, Trivy
- **Build Tools**: Setuptools, Wheel, Poetry (optional)

## Key Features

### Quantum Computing Capabilities
- **Circuit Creation**: Intuitive quantum circuit design interface
- **Algorithm Library**: Pre-built quantum algorithms and protocols
- **Optimization Tools**: Quantum circuit optimization and error mitigation
- **Simulation Engine**: High-performance quantum state simulation

### WebAssembly Integration
- **Performance**: Near-native execution speed for quantum algorithms
- **Portability**: Cross-platform compatibility with WASM support
- **Security**: Sandboxed execution environment with memory protection
- **Scalability**: Horizontal scaling for large quantum computations

### DevSecOps Features
- **Automated Security**: Continuous security scanning and vulnerability detection
- **Compliance**: STIG, CMMC2.0, and NIST SP 800-53 compliance
- **CI/CD Pipeline**: Automated testing, building, and deployment
- **Monitoring**: Real-time performance and security monitoring

## Use Cases

### Research and Development
- **Quantum Algorithm Development**: Prototyping and testing new quantum algorithms
- **Quantum Machine Learning**: Developing quantum-enhanced ML models
- **Quantum Simulation**: Simulating complex quantum systems and phenomena
- **Academic Research**: Educational tools and research platforms

### Enterprise Applications
- **Financial Modeling**: Quantum optimization for portfolio management
- **Drug Discovery**: Molecular simulation and protein folding analysis
- **Supply Chain Optimization**: Quantum logistics and routing optimization
- **Cybersecurity**: Quantum-resistant encryption and security protocols

### Cloud Services
- **Quantum-as-a-Service**: Scalable quantum computing in the cloud
- **API Integration**: RESTful quantum services for application integration
- **Multi-tenant Support**: Secure isolation between quantum computing tenants
- **Cost Optimization**: Pay-per-use quantum computing resources

## Architecture Diagram

```mermaid
flowchart TB
    subgraph clients [Clients]
        User[Users]
    end
    subgraph security [Security Stack]
        Keycloak[Keycloak]
        Vault[Vault]
        Envoy[Envoy]
    end
    subgraph app [Application]
        WUI[WUI Backend]
        Engine[Q-Mini-WASM Engine]
    end
    subgraph data [Data Stack]
        RabbitMQ[RabbitMQ]
        NiFi[NiFi]
        PG[(PostgreSQL)]
    end
    subgraph host [Host]
        Packer[AlmaLinux 9 Golden Image]
    end
    User --> Keycloak
    Keycloak --> WUI
    Envoy --> Keycloak
    Envoy --> Vault
    WUI --> RabbitMQ
    RabbitMQ --> NiFi
    NiFi --> PG
    WUI --> Engine
    Vault -->|mTLS certs| PG
    Packer --> host
```

## Development Philosophy

### Security-First Approach
- **Zero Trust Architecture**: No implicit trust in any component
- **Defense in Depth**: Multiple layers of security controls
- **Secure by Default**: Security features enabled by default
- **Continuous Monitoring**: Real-time security event detection

### Performance Optimization
- **Algorithm Efficiency**: Optimized quantum algorithms for performance
- **Resource Management**: Efficient memory and CPU utilization
- **Parallel Processing**: Multi-threaded execution for speed
- **Caching Strategies**: Intelligent caching for frequently used operations

### Scalability Principles
- **Horizontal Scaling**: Support for distributed quantum computations
- **Load Balancing**: Intelligent distribution of quantum workloads
- **Resource Pooling**: Shared quantum computing resources
- **Auto-scaling**: Dynamic resource allocation based on demand

## Integration Capabilities

### Third-Party Integrations
- **Cloud Platforms**: AWS, Azure, Google Cloud quantum services
- **Container Orchestration**: Kubernetes, Docker Swarm support
- **CI/CD Systems**: Jenkins, GitLab CI, GitHub Actions integration
- **Monitoring Tools**: Prometheus, Grafana, ELK stack integration

### API Specifications
- **RESTful APIs**: Standard HTTP-based quantum service APIs
- **GraphQL Support**: Flexible query language for quantum data
- **WebSocket APIs**: Real-time quantum computation streaming
- **SDKs**: Python, JavaScript, and other language bindings

## Future Roadmap

### Near-term Goals (1-3 months)
- Enhanced security features and compliance updates
- Performance improvements and optimization
- Additional quantum algorithms and protocols
- Improved documentation and tutorials

### Medium-term Goals (3-6 months)
- Advanced monitoring and observability features
- Scalability improvements and distributed computing support
- Integration enhancements with cloud platforms
- Community features and collaboration tools

### Long-term Goals (6+ months)
- Advanced security capabilities and threat detection
- Performance optimizations and quantum advantage demonstrations
- New quantum computing features and protocols
- Enterprise-grade features and support

## Getting Started

Ready to begin your quantum computing journey with Q-Mini-WASM? Follow our [Development Workflow](Development) guide to set up your development environment and start building quantum applications today.

---

**Last Updated**: 2026-03-12
**Version**: 1.4.0