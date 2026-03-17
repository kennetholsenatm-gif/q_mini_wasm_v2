# Kyverno admission control

Kyverno enforces deploy gates and Kubernetes STIG-like baselines so only successfully scanned images and compliant pods are allowed.

## Install

```bash
helm repo add kyverno https://kyverno.github.io/kyverno
helm repo update
helm install kyverno kyverno/kyverno -n kyverno --create-namespace -f infra/kyverno/kyverno-values.yaml
kubectl apply -f infra/kyverno/policies/
```

Or use OpenTofu: see [infra/opentofu/kyverno/](opentofu/kyverno/).

## Policies

- **require-pod-security-stig:** Enforces `runAsNonRoot: true`, `allowPrivilegeEscalation: false`, `readOnlyRootFilesystem: true` on pods.
- **require-trivy-scan-label:** Requires annotation `trivy.scan/passed: "true"` on Pods and on Deployment/StatefulSet/DaemonSet `spec.template.metadata.annotations`. Add this annotation in Helm values or CI after Trivy image scan passes (no CRITICAL/HIGH).

## SIEM

Blocked admission attempts are logged by the Kubernetes API server (and Kyverno). Forward cluster audit logs to ELK/Splunk for AU-2/AU-3.
