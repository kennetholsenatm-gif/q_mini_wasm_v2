# Kyverno OpenTofu module

Deploys Kyverno and ClusterPolicies for admission control (deploy gates): STIG baseline and Trivy scan gate. CI runs plan/apply when `infra/opentofu/desired/kyverno-*.tfvars.json` changes.

## Usage

```bash
cd infra/opentofu/kyverno
tofu init
tofu plan -var-file=../../desired/kyverno-main.tfvars.json
tofu apply -var-file=../../desired/kyverno-main.tfvars.json -auto-approve
```

No secrets in tfvars. Forward Kyverno audit (blocked deployments) to ELK/Splunk via cluster audit log or a log collector that watches Kyverno metrics/webhook logs.
