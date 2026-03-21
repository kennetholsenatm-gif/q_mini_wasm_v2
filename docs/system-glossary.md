# Autonomous Solace Agent Mesh - System Glossary

**Document Version:** 1.0  
**Last Updated:** March 17, 2026  
**Classification:** Internal Use  
**Purpose:** Terminology anchoring for AI retrieval stability

## Core System Components

### A

**Agent Node**
- **Definition:** Self-contained software entity that performs specific DevSecOps tasks
- **Characteristics:** Autonomous operation, message-based communication, resource isolation
- **Types:** Security scanner, deployment runner, monitoring collector, workflow orchestrator
- **Lifecycle:** Provisioned via OpenNebula, managed via Docker, monitored via Solace

**Ansible Playbook**
- **Definition:** Configuration management script for infrastructure deployment
- **Purpose:** Automated host configuration, service deployment, compliance enforcement
- **Location:** `infra/edge-gateway/deploy-local-mesh.yml`
- **Execution:** Targeted at `ai_mesh_nodes` inventory group

**Application Secret**
- **Definition:** Cryptographic material used by applications for secure operations
- **Storage:** Encrypted in HashiCorp Vault with PQC algorithms
- **Examples:** Database passwords, API keys, encryption keys
- **Access Control:** Role-based with audit logging

### C

**CMMC Level 2**
- **Definition:** Cybersecurity Maturity Model Certification compliance level
- **Requirements:** 110 security controls across 14 domains
- **Scope:** Controlled Unclassified Information (CUI) protection
- **Implementation:** Zero Trust architecture, audit logging, access controls

**Carrier-Grade NAT (CGNAT)**
- **Definition:** Network address translation using RFC 6598 address space
- **Range:** 100.64.0.0/10 (100.64.0.0 to 100.127.255.255)
- **Purpose:** Private network isolation with home network overload
- **Implementation:** Docker bridge network with iptables NAT rules

**Compliance Check**
- **Definition:** Automated validation of security and operational requirements
- **Frequency:** Daily execution via Ansible playbook
- **Scope:** CMMC Level 2 controls, PQC readiness, Zero Trust policies
- **Output:** Structured reports with remediation guidance

**Container Bridge Network**
- **Definition:** Docker-managed virtual network for container communication
- **Configuration:** Custom subnet 100.64.0.0/16 with gateway 100.64.0.1
- **Security:** Isolated from host network, Zero Trust policies enforced
- **Services:** All mesh components connected to this network

**Cryptographic Algorithm**
- **Definition:** Mathematical procedure for encryption, decryption, and authentication
- **Types:** Symmetric (AES), Asymmetric (RSA, ECDSA), Post-Quantum (ML-KEM)
- **Selection Criteria:** Security strength, performance, quantum resistance
- **Implementation:** PQC-ready with hybrid cryptographic approaches

### D

**Database Encryption**
- **Definition:** Protection of data at rest using cryptographic algorithms
- **Implementation:** PostgreSQL with PQC-ready encryption keys
- **Scope:** All application databases (Gitea, n8n, OpenNebula)
- **Key Management:** Vault-managed with rotation policies

**DevSecOps Pipeline**
- **Definition:** Integrated development, security, and operations workflow
- **Components:** Code management, security scanning, deployment automation
- **Trigger:** Gitea webhook events to n8n orchestrator
- **Execution:** Agent-based with Solace message coordination

**Docker Compose**
- **Definition:** Multi-container Docker application definition format
- **Files:** `docker-compose.opennebula.yml`, `docker-compose.devsecops.yml`
- **Purpose:** Service orchestration, network configuration, volume management
- **Deployment:** Local development environment with mesh networking

### E

**Event Topology**
- **Definition:** Structured flow of messages between system components
- **Pattern:** Publish-subscribe with topic-based routing
- **Channels:** Code changes, security events, deployment status, monitoring alerts
- **Performance:** Sub-second latency with guaranteed delivery

**Execution Environment**
- **Definition:** Runtime context for agent operations and service execution
- **Components:** Docker containers, OpenNebula VMs, host operating system
- **Isolation:** Network segmentation, resource limits, security boundaries
- **Management:** Automated provisioning and lifecycle management

### G

**Gitea Repository**
- **Definition:** Self-hosted Git service for source code management
- **Configuration:** Private repository with PQC-ready encryption
- **Integration:** Webhook events to n8n orchestrator
- **Security:** Database encryption, access controls, audit logging

**Google NotebookLM Formatting**
- **Definition:** Document structure optimized for AI retrieval and understanding
- **Principles:** Chunking, metadata, structured content organization
- **Purpose:** Enhanced RAG application performance
- **Implementation:** Deliberate content segmentation with clear boundaries

### H

**Home Network Overload**
- **Definition:** NAT configuration allowing mesh network access to home network
- **Implementation:** iptables rules with source network 100.64.0.0/16
- **Purpose:** External service access while maintaining isolation
- **Security:** Controlled access with logging and monitoring

**Hybrid Cryptographic Approach**
- **Definition:** Combination of classical and post-quantum cryptographic algorithms
- **Purpose:** Transition strategy for quantum-resistant security
- **Implementation:** Dual encryption with algorithm agility
- **Benefits:** Backward compatibility with future-proofing

### I

**Infrastructure as Code (IaC)**
- **Definition:** Management of infrastructure through machine-readable definition files
- **Tools:** OpenTofu for provisioning, Ansible for configuration
- **Benefits:** Version control, reproducibility, automated deployment
- **Scope:** Complete mesh infrastructure from network to applications

**Integration Configuration**
- **Definition:** Settings and parameters for system component interoperability
- **Components:** Webhook secrets, API endpoints, message topics
- **Security:** Encrypted storage with access controls
- **Management:** Centralized configuration with validation

### L

**Local Development Environment**
- **Definition:** Single-host deployment for development and testing
- **Architecture:** Docker-based with OpenNebula simulation
- **Network:** Isolated mesh network with home network access
- **Services:** Complete stack from Solace to Vault

**Log Level**
- **Definition:** Verbosity setting for application and system logging
- **Values:** DEBUG, INFO, WARN, ERROR
- **Configuration:** Per-component with centralized collection
- **Purpose:** Operational visibility and troubleshooting

### M

**Mesh Network**
- **Definition:** Isolated network segment for autonomous agent communication
- **Address Space:** 100.64.0.0/16 (RFC 6598 CGNAT range)
- **Gateway:** 100.64.0.1 with NAT to home network
- **Security:** Zero Trust policies with micro-segmentation

**Message VPN**
- **Definition:** Virtual private network within Solace PubSub+ for isolated messaging
- **Configuration:** mTLS authentication, Zero Trust ACL profiles
- **Topic Structure:** `a2a/v1/{agent_type}/{agent_id}/{action}`
- **Security:** Client certificate validation, topic-level access control

**Monitoring Dashboard**
- **Definition:** Real-time visualization of system metrics and status
- **Components:** Performance metrics, security events, compliance status
- **Integration:** Solace message collection with web-based display
- **Alerting:** Threshold-based notifications with escalation

### N

**n8n Workflow**
- **Definition:** Node-based workflow automation orchestrator
- **Categories:** Code pipeline, security scanning, monitoring alerts
- **Triggers:** Gitea webhooks, scheduled events, message events
- **Integration:** Solace messaging, Vault secrets, external APIs

**Network Segmentation**
- **Definition:** Division of network into isolated segments for security
- **Implementation:** Docker bridge networks with Zero Trust policies
- **Benefits:** Attack surface reduction, lateral movement prevention
- **Management:** Automated configuration with monitoring

**Node.js Application**
- **Definition:** JavaScript runtime environment for server-side applications
- **Usage:** n8n orchestrator, web interfaces, API services
- **Security:** Container isolation, dependency scanning, vulnerability management
- **Performance:** Resource limits, monitoring, optimization

### O

**OpenNebula Frontend**
- **Definition:** Cloud management platform for virtual machine orchestration
- **Components:** Sunstone web interface, XML-RPC API, flow services
- **Integration:** Docker-based deployment with mesh networking
- **Purpose:** VM lifecycle management and resource allocation

**OpenTofu Configuration**
- **Definition:** Infrastructure as Code tool for resource provisioning
- **Files:** `opennebula_infrastructure.tofu`, `local_backend.tf`
- **Providers:** Docker, local, TLS for comprehensive infrastructure management
- **State Management:** Local backend with sensitive data protection

**Operational Excellence**
- **Definition:** Continuous improvement of system reliability and efficiency
- **Practices:** Automated monitoring, incident response, performance optimization
- **Metrics:** Uptime, response time, resource utilization
- **Goals:** Zero-downtime operations with proactive maintenance

### P

**Post-Quantum Cryptography (PQC)**
- **Definition:** Cryptographic algorithms resistant to quantum computer attacks
- **Algorithms:** ML-KEM, CRYSTALS-Kyber, Dilithium
- **Implementation:** Hybrid approach with classical algorithms
- **Readiness:** Key generation, certificate management, algorithm agility

**Pre-commit Hook**
- **Definition:** Git hook that validates code changes before commit
- **Tools:** Black, Flake8, Bandit, MyPy for Python code quality
- **Security:** Secret detection, branch validation, commit message format
- **Integration:** Automated enforcement with CI/CD pipeline

**Privileged Container**
- **Definition:** Docker container with elevated system privileges
- **Usage:** Solace Message VPN, OpenNebula components
- **Security:** Limited to essential services with monitoring
- **Isolation:** Network and resource separation from standard containers

### R

**Retrieval-Augmented Generation (RAG)**
- **Definition:** AI technique combining retrieval with generative models
- **Application:** Documentation processing and knowledge extraction
- **Optimization:** Chunking strategies, metadata tagging, structured content
- **Benefits:** Enhanced accuracy and context-aware responses

**Role-Based Access Control (RBAC)**
- **Definition:** Security model granting permissions based on user roles
- **Implementation:** Vault secrets, application access, infrastructure management
- **Principles:** Least privilege, separation of duties, audit trails
- **Enforcement:** Automated policy application with continuous monitoring

### S

**Security Scan**
- **Definition:** Automated vulnerability and compliance assessment
- **Tools:** Bandit, Trivy, Snyk for container and code analysis
- **Frequency:** Daily execution with real-time alerting
- **Integration:** n8n workflows with Solace message coordination

**Secret Management**
- **Definition:** Secure storage and distribution of cryptographic materials
- **Implementation:** HashiCorp Vault with PQC-ready encryption
- **Categories:** Application secrets, infrastructure secrets, PQC keys
- **Access:** Role-based with audit logging and rotation policies

**Solace Message VPN**
- **Definition:** Virtual private network within Solace PubSub+ broker
- **Configuration:** mTLS authentication, Zero Trust ACL profiles
- **Topic Structure:** Hierarchical with agent-specific routing
- **Security:** Client certificate validation, topic-level access control

**SSH Key Management**
- **Definition:** Secure generation, distribution, and rotation of SSH keys
- **Implementation:** OpenNebula integration with PQC-ready algorithms
- **Security:** Encrypted storage, access controls, audit logging
- **Rotation:** Automated with minimal service disruption

### T

**Topic-Based Routing**
- **Definition:** Message routing mechanism using hierarchical topic names
- **Pattern:** `a2a/v1/{agent_type}/{agent_id}/{action}`
- **Benefits:** Flexible subscription, efficient filtering, scalable architecture
- **Security:** ACL-based access control with topic-level permissions

**TLS Certificate**
- **Definition:** X.509 certificate for Transport Layer Security encryption
- **Generation:** Self-signed with PQC-ready algorithms
- **Usage:** mTLS authentication, service encryption, client validation
- **Management:** Automated renewal with key rotation

### V

**Vault Secret**
- **Definition:** Encrypted value stored in HashiCorp Vault
- **Types:** Static secrets, dynamic secrets, PQC keys
- **Access:** Role-based with time-based policies
- **Audit:** Complete logging of access and modifications

**Version Control**
- **Definition:** System for tracking changes to source code and configuration
- **Implementation:** Gitea with private repository and webhook integration
- **Security:** Encrypted storage, access controls, audit logging
- **Integration:** Automated workflows with n8n orchestrator

**Virtual Machine Template**
- **Definition:** Pre-configured VM image for rapid deployment
- **Types:** Solace Message VPN, Gitea Server, OpenNebula components
- **Configuration:** Resource allocation, network settings, security policies
- **Management:** Automated provisioning with lifecycle management

### W

**Webhook Integration**
- **Definition:** HTTP callback for event-driven system integration
- **Configuration:** Gitea to n8n with PQC-encrypted secrets
- **Events:** Push, pull request, issues, releases
- **Security:** Shared secrets, HTTPS, content validation

**Workflow Automation**
- **Definition:** Automated execution of multi-step processes
- **Orchestrator:** n8n with node-based workflow design
- **Triggers:** Events, schedules, manual execution
- **Integration:** Solace messaging, Vault secrets, external APIs

**Zero Trust Architecture**
- **Definition:** Security model requiring verification for all access requests
- **Principles:** Never trust, always verify, least privilege access
- **Implementation:** Network segmentation, mTLS, ACL profiles
- **Benefits:** Reduced attack surface, improved security posture

## Cross-Reference Index

### By Component
- **Solace Components:** Message VPN, Topic-Based Routing, mTLS
- **Security Components:** Vault, PQC, Zero Trust, CMMC Level 2
- **Infrastructure Components:** OpenNebula, Docker, Ansible, OpenTofu
- **Application Components:** Gitea, n8n, Monitoring, Webhooks

### By Security Level
- **High Security:** PQC, mTLS, Zero Trust, CMMC Level 2
- **Medium Security:** RBAC, Secret Management, Audit Logging
- **Operational Security:** Monitoring, Compliance Checks, Security Scans

### By Network Scope
- **Internal Mesh:** 100.64.0.0/16, Container Bridge, Micro-segmentation
- **External Access:** Home Network Overload, NAT, Firewall Rules
- **Service Communication:** Solace Topics, Webhooks, API Endpoints

---

**Document Metadata:**
- **Chunk ID:** SYSTEM_GLOSSARY_V1.0
- **Created:** 2026-03-17
- **Classification:** Internal Use
- **Purpose:** AI retrieval stability and terminology consistency
- **Review Cycle:** Quarterly