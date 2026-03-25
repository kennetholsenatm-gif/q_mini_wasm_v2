# Deployment Guide: Implementation Roadmap

This guide provides step-by-step instructions for deploying the Hierarchical Edge-Quantum AI Architecture across different environments.

## Unified edge vocabulary (deployment)

Operators SHOULD size deployments by **Enclave Footprint (EF)** and **Ternary-Packed Memory Enclave (TPEM)** geometry—not informal “tensor” or raw parameter-count estimates alone:

- **Micro-Enclaves:** EF sub-250 MB WASM linear memory (≈1.2B effective at ~1.6 bits/weight).
- **Meso-Enclaves:** EF ≈2 GB (≈10B effective); fits classic 32-bit WASM bounds.
- **Macro-Enclaves:** EF ≈8 GB with **Memory64** (≈40B effective); align host **WASM_MAX_MEMORY** / tier caps in `configs/serve/default.toml`.

Runtime behavior uses **Edge Cognitive Looping (ECL)** and **Certainty Scalars** vs **`certainty_scalar_threshold` / $T_{conf}$**; **Certainty-Gated Escalation (CGE)** hands off to Tier 2/3. Context is managed via **Ephemeral State Inversion (ESI)** (not KV-cache growth). Remote path selection uses **Quantum-Assisted Hierarchical Routing (QAHR)**. Suspend/resume favors **WASM Linear Execution Snapshots (WLES)** over ad-hoc tensor-only checkpoints.

See [Concepts Explained](../Concepts-Explained.md) and [Architecture Overview](Architecture-Overview.md) for the full glossary.

## Prerequisites

### System Requirements

#### Edge Environment
- **Operating System:** Linux (AlmaLinux 9, Ubuntu 20.04+), Windows 10+, macOS 10.15+
- **Memory:** Minimum 4GB RAM, recommended 8GB+
- **Storage:** Minimum 20GB free space
- **Network:** Internet connectivity for initial setup
- **CPU:** x86_64 architecture with AES-NI support

#### Cloud Environment
- **Kubernetes:** Version 1.25+
- **Container Runtime:** Docker 20.10+ or containerd 1.6+
- **Storage:** Persistent storage for databases
- **Networking:** Load balancer and ingress controller
- **Security:** TLS certificates and secrets management

### Software Dependencies

#### Development Environment
```bash
# Python 3.11+
python --version

# Docker
docker --version

# Kubernetes tools
kubectl version --client
helm version

# Infrastructure tools
tofu version  # OpenTofu
```

#### Production Environment
```bash
# Container runtime
containerd --version

# Monitoring tools
prometheus --version
grafana-server --version

# Security tools
vault --version
keycloak --version
```

## Quick Start Deployment

### Option 1: Local Development (Kind + OpenTofu)

#### Step 1: Environment Setup
```bash
# Clone the repository
git clone https://github.com/kennetholsenatm-gif/qminiwasm-core.git
cd qminiwasm-core

# Install Python dependencies
pip install -r requirements.txt

# Install pre-commit hooks
pre-commit install
```

#### Step 2: Local Kubernetes Setup
```bash
# Create Kind cluster
make -C infra/opentofu/modules/cluster/kind kind-create

# Verify cluster
kubectl cluster-info
```

#### Step 3: Deploy Infrastructure
```bash
# Navigate to local environment
cd infra/opentofu/environments/local

# Initialize OpenTofu
tofu init

# Apply infrastructure
tofu apply

# Verify deployment
kubectl get pods -A
```

#### Step 4: Deploy Application
```bash
# Deploy WUI (optional)
cd ../../../../charts/qminiwasm-wui
helm install qminiwasm-wui .

# Verify application
kubectl get services
```

### Option 2: Production Deployment (Multi-Cloud)

#### Step 1: Cloud Provider Setup
```bash
# AWS
aws configure
aws eks update-kubeconfig --region us-west-2 --name production-cluster

# Azure
az account set --subscription <subscription-id>
az aks get-credentials --resource-group production-rg --name production-cluster

# GCP
gcloud container clusters get-credentials production-cluster --zone us-central1-a
```

#### Step 2: Infrastructure Deployment
```bash
# Navigate to production environment
cd infra/opentofu/environments/prod

# Configure variables
cp terraform.tfvars.example terraform.tfvars
# Edit terraform.tfvars with your cloud-specific settings

# Initialize and apply
tofu init
tofu apply
```

#### Step 3: Application Deployment
```bash
# Deploy core application
helm install qminiwasm-core charts/qminiwasm-core/

# Deploy monitoring stack
helm install monitoring charts/monitoring/

# Deploy security stack
helm install security charts/security/
```

## Edge Deployment

### Edge Device Requirements
- **Hardware:** ARM64 or x86_64 (Apple M-series / AMD Strix Halo unified-memory hosts for **Macro-Enclave** tier)
- **Host RAM:** At least EF + OS headroom; **Meso** ≈2 GB EF class agents commonly need 8 GB+ host RAM
- **Storage:** 10 GB+ for images; fast NVMe recommended for **WLES** snapshot I/O
- **Network:** Ethernet or Wi-Fi connectivity
- **Security:** TPM 2.0 (recommended)

### Edge Deployment Steps

#### Step 1: Edge Device Preparation
```bash
# Install container runtime
curl -fsSL https://get.docker.com -o get-docker.sh
sh get-docker.sh

# Install Kubernetes tools
curl -LO "https://dl.k8s.io/release/$(curl -L -s https://dl.k8s.io/release/stable.txt)/bin/linux/amd64/kubectl"
sudo install -o root -g root -m 0755 kubectl /usr/local/bin/kubectl
```

#### Step 2: Edge Agent Installation
```bash
# Download edge agent
wget https://example.com/edge-agent.tar.gz
tar -xzf edge-agent.tar.gz
cd edge-agent

# Configure agent
./configure --broker-url=wss://cloud-broker.example.com:8008
./configure --agent-id=edge-device-001
./configure --credentials-file=/path/to/credentials.json
```

#### Step 3: Start Edge Services
```bash
# Start edge agent
./start-agent.sh

# Verify connectivity
./status.sh

# Monitor logs
tail -f /var/log/edge-agent.log
```

## Configuration Management

### Environment Configuration

#### Development Environment
```yaml
# environments/local/terraform.tfvars
enable_wui = true
debug_mode = true
log_level = "debug"
resource_limits = {
  cpu = "500m"
  memory = "1Gi"
}
```

#### Production Environment
```yaml
# environments/prod/terraform.tfvars
enable_wui = false
debug_mode = false
log_level = "info"
resource_limits = {
  cpu = "2000m"
  memory = "8Gi"
}
replica_count = 3
auto_scaling = true
```

### Security Configuration

#### TLS Configuration
```yaml
# security/tls-config.yaml
tls:
  enabled: true
  certificate_authority: /path/to/ca.crt
  server_certificate: /path/to/server.crt
  server_key: /path/to/server.key
  client_authentication: required
```

#### Authentication Configuration
```yaml
# security/auth-config.yaml
authentication:
  type: oidc
  provider: keycloak
  client_id: qminiwasm-client
  client_secret: <secret>
  issuer_url: https://auth.example.com/auth/realms/qminiwasm
```

## Monitoring and Observability

### Monitoring Stack Deployment
```bash
# Deploy Prometheus
helm install prometheus stable/prometheus

# Deploy Grafana
helm install grafana stable/grafana

# Deploy Jaeger
helm install jaeger stable/jaeger
```

### Custom Metrics
```python
# application/metrics.py
from prometheus_client import Counter, Histogram, Gauge

# Define metrics
request_count = Counter('requests_total', 'Total requests')
request_duration = Histogram('request_duration_seconds', 'Request duration')
active_connections = Gauge('active_connections', 'Active connections')
```

### Alerting Configuration
```yaml
# monitoring/alerts.yaml
groups:
- name: qminiwasm-alerts
  rules:
  - alert: HighErrorRate
    expr: rate(http_requests_total{status=~"5.."}[5m]) > 0.1
    for: 5m
    labels:
      severity: critical
    annotations:
      summary: "High error rate detected"
```

## Backup and Recovery

### Database Backup
```bash
# PostgreSQL backup
pg_dump -h localhost -U qminiwasm -d qminiwasm_db > backup.sql

# Automated backup script
#!/bin/bash
DATE=$(date +%Y%m%d_%H%M%S)
pg_dump -h $DB_HOST -U $DB_USER -d $DB_NAME > backup_$DATE.sql
aws s3 cp backup_$DATE.sql s3://qminiwasm-backups/
```

### Application State Backup
```bash
# Backup Kubernetes resources
kubectl get all --all-namespaces -o yaml > cluster-backup.yaml

# Backup persistent volumes
kubectl get pv -o yaml > pv-backup.yaml
kubectl get pvc -o yaml > pvc-backup.yaml
```

### Disaster Recovery
```bash
# Restore database
psql -h localhost -U qminiwasm -d qminiwasm_db < backup.sql

# Restore Kubernetes resources
kubectl apply -f cluster-backup.yaml

# Verify restoration
kubectl get pods -A
```

## Troubleshooting

### Common Issues

#### Container Image Pull Errors
```bash
# Check image availability
docker pull qminiwasm/engine:latest

# Verify registry credentials
kubectl get secret regcred -o yaml

# Check image pull policy
kubectl describe pod <pod-name>
```

#### Network Connectivity Issues
```bash
# Test DNS resolution
nslookup cloud-broker.example.com

# Test TLS connectivity
openssl s_client -connect cloud-broker.example.com:8008

# Check firewall rules
sudo iptables -L
```

#### Resource Constraints
```bash
# Check resource usage
kubectl top nodes
kubectl top pods

# Check resource limits
kubectl describe pod <pod-name>

# Scale resources
kubectl scale deployment <deployment-name> --replicas=3
```

### Debugging Tools

#### Application Logs
```bash
# View application logs
kubectl logs -f deployment/qminiwasm-engine

# View system logs
journalctl -f -u docker
journalctl -f -u kubelet
```

#### Performance Monitoring
```bash
# Monitor CPU and memory
top
htop

# Monitor network
iftop
nload

# Monitor disk I/O
iotop
iostat
```

## Performance Optimization

### Edge Device Optimization
```bash
# Optimize container runtime
echo '{
  "log-driver": "json-file",
  "log-opts": {
    "max-size": "10m",
    "max-file": "3"
  },
  "storage-driver": "overlay2"
}' | sudo tee /etc/docker/daemon.json

# Optimize kernel parameters
echo 'vm.swappiness=10' | sudo tee -a /etc/sysctl.conf
echo 'net.core.somaxconn=65535' | sudo tee -a /etc/sysctl.conf
sudo sysctl -p
```

### Cloud Resource Optimization
```yaml
# resource-optimization.yaml
apiVersion: v1
kind: ResourceQuota
metadata:
  name: compute-resources
spec:
  hard:
    requests.cpu: "10"
    requests.memory: 20Gi
    limits.cpu: "20"
    limits.memory: 40Gi
```

## Security Hardening

### Container Security
```yaml
# security-context.yaml
apiVersion: v1
kind: Pod
spec:
  securityContext:
    runAsNonRoot: true
    runAsUser: 1000
    fsGroup: 2000
  containers:
  - name: qminiwasm-engine
    securityContext:
      allowPrivilegeEscalation: false
      capabilities:
        drop:
        - ALL
```

### Network Security
```yaml
# network-policy.yaml
apiVersion: networking.k8s.io/v1
kind: NetworkPolicy
metadata:
  name: qminiwasm-network-policy
spec:
  podSelector:
    matchLabels:
      app: qminiwasm
  policyTypes:
  - Ingress
  - Egress
  ingress:
  - from:
    - namespaceSelector:
        matchLabels:
          name: trusted
  egress:
  - to:
    - namespaceSelector:
        matchLabels:
          name: database
```

## Maintenance

### Regular Maintenance Tasks

#### Security Updates
```bash
# Update container images
docker pull qminiwasm/engine:latest
docker pull qminiwasm/wui:latest

# Update Kubernetes
kubectl apply -f https://github.com/kubernetes-sigs/metrics-server/releases/latest/download/components.yaml
```

#### Performance Monitoring
```bash
# Generate performance reports
kubectl top nodes --sort-by=cpu
kubectl top pods --sort-by=memory

# Check resource utilization
kubectl describe nodes
```

#### Log Management
```bash
# Rotate logs
sudo logrotate -f /etc/logrotate.conf

# Clean up old logs
find /var/log -name "*.log" -mtime +30 -delete
```

## Support and Resources

### Documentation
- [Architecture Overview](Architecture-Overview.md)
- [Security and Compliance](Security-and-Compliance.md)
- [API Documentation](https://docs.qminiwasm.com)

### Community Support
- GitHub Issues: https://github.com/kennetholsenatm-gif/qminiwasm-core/issues
- Documentation: https://docs.qminiwasm.com
- Community Forum: https://community.qminiwasm.com

### Professional Support
- Enterprise Support: [REDACTED]
- Training and Consulting: [REDACTED]
- Security Incidents: [REDACTED]

---

**Last Updated:** 2026-03-16
**Version:** 2.0