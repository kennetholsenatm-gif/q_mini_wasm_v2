# Operations Runbook

This runbook is for platform, DevOps, and infrastructure operators. It consolidates deep operational workflows that are intentionally excluded from `README.md` and the 0-to-1 quickstart.

## Scope

Use this guide for:

- RunPod pod lifecycle and OpenTofu workflows
- RunPod serverless endpoint and template operations
- SSH-based sync and remote training execution
- advanced runtime flags and environment controls

## 1) RunPod Pods with OpenTofu

Canonical references:

- [../RUNPOD_QUICKSTART.md](../RUNPOD_QUICKSTART.md)
- [../../infra/runpod/README.md](../../infra/runpod/README.md)
- [../../infra/runpod/CLOUD_ACCELERATOR.md](../../infra/runpod/CLOUD_ACCELERATOR.md)

Operational notes:

- run `tofu init/plan/apply/destroy` from `infra/runpod`
- keep `infra/runpod/terraform.tfvars` as the source of deployment tuning
- destroy resources at the end of a run to stop billing

Typical lifecycle:

```bash
cd infra/runpod
./tofu.sh init
./tofu.sh plan
./tofu.sh apply
# ... run jobs ...
./tofu.sh destroy
```

## 2) RunPod Serverless Operations

Canonical reference:

- [../RUNPOD_SERVERLESS.md](../RUNPOD_SERVERLESS.md)

Use this mode for queue-based worker jobs rather than long-lived pods.

Operational concerns:

- endpoint/template management uses account-level API keys
- worker job invocation uses endpoint-specific credentials when configured
- payloads should stay small; datasets/checkpoints belong in image, mounted volume, or object storage

## 3) SSH Sync and Remote Execution

When training from a remote pod/host, use repo sync plus explicit remote execution.

Typical flow:

1. sync repository contents to the remote workspace
2. propagate `.env` and required secrets on the remote host
3. run `python -m engine` remotely
4. pull artifacts/logs back to local if needed

For WUI-assisted automation and helper scripts, see:

- [../../training-wui/README.md](../../training-wui/README.md)
- [../RUNPOD_QUICKSTART.md](../RUNPOD_QUICKSTART.md)

## 4) Advanced Runtime and CLI Controls

Use these references when standard quickstart settings are insufficient:

- training data, metrics, and checkpoint flags: [../TRAINING_DATA.md](../TRAINING_DATA.md)
- Qiskit and IBM Runtime execution modes: [../QUANTUM_QISKIT.md](../QUANTUM_QISKIT.md)
- cascade and MOPD tuning: [../CASCADE_AND_MOPD.md](../CASCADE_AND_MOPD.md)
- global env var index: [../environment-variables.md](../environment-variables.md)
- Intel XPU setup: [../INSTALL_TORCH_XPU.md](../INSTALL_TORCH_XPU.md)
- SYCL integration contract: [../SYCL-Integration.md](../SYCL-Integration.md)

## 5) Suggested Operational Cadence

- **Provision**: apply infrastructure and verify endpoint readiness
- **Execute**: run training/inference workload with explicit config and env capture
- **Observe**: monitor logs, status endpoints, and checkpoint output paths
- **Recover**: restart from checkpoints instead of ad hoc reruns
- **Deprovision**: destroy idle infrastructure promptly

## 6) Separation of Concerns

Documentation intent by audience:

- `README.md` and quickstart are onboarding-first and conceptual
- this runbook is operations-first and command-heavy
- research/theory remains under `wiki/` and selected `docs/` references
