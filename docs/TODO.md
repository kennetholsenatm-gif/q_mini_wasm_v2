# Documentation and Automation Gaps

This file lists **gaps** between the codebase and the documentation (or desired user experience) as identified during the code-to-documentation review. Use it to prioritize follow-up work.

---

## Missing or Incomplete Automation

| Gap | Description | Suggested fix |
|-----|-------------|---------------|
| **No single greenfield orchestration script** | The [Greenfield Deployment Guide](Greenfield-Deployment.md) describes four manual steps. There is no script that automates the sequence (e.g. validate Packer → run security stack → run data stack → deploy WUI) with basic checks. | **Addressed:** `scripts/greenfield-deploy.sh` and `scripts/greenfield-deploy.ps1` run security → data stack with env checks and optional `--yes` / `--packer-validate` / `--skip-wui`. |
| **WUI backend → Data Stack connection** | When running the WUI backend container (Step 4), the guide does not document the environment variables needed to connect to the data stack (PostgreSQL, RabbitMQ) when those stacks run in Docker. | **Addressed:** Documented in [Greenfield-Deployment.md](Greenfield-Deployment.md) Step 4 and [containers/wui/README.md](../containers/wui/README.md): `POSTGRES_*`, `RABBITMQ_*`, `DATABASE_URL`, `BROKER_URL`. |
| **Air-gap image export/import** | Greenfield mentions air-gap but does not provide a script or checklist to export images (e.g. `docker save`) and load them on the target node. | **Addressed:** [scripts/airgap-export.sh](../scripts/airgap-export.sh) and [scripts/airgap-import.sh](../scripts/airgap-import.sh); Greenfield "Air-gap" section updated with script references. |

---

## Documentation / Link Fixes

| Gap | Description | Suggested fix |
|-----|-------------|---------------|
| **Wiki link to DockerOS** | [wiki/Overview.md](../wiki/Overview.md) references the DockerOS Platform Standard but uses a relative path that may not resolve correctly from the GitHub Wiki UI. | Add an explicit link to the repo file: `docs/DockerOS-Platform-Standard.md` (or full GitHub URL) in the Overview. |
| **README clone URL** | Already updated to `kennetholsenatm-gif/LLM_Pract`; ensure no other references to `your-org/q-mini-wasm` remain. | Grep and replace any remaining placeholder org/repo names. |
| **Security Scans workflow vs pip install** | The Security Scans workflow uses `pip install -r requirements/security.txt` with fallback to minimal deps; the README still says `pip install -e .[security]` for OpenSCAP. | Keep README as-is for local use; the workflow is intentionally lighter to avoid CI failures. Optionally add a note that CI uses a lighter install path. |

---

## Known Limitations (No Code Change Required)

| Item | Description |
|------|-------------|
| **OpenSCAP report not always generated** | In CI, the OpenSCAP step can fail (e.g. SCAP content fetch or scan failure). The artifact upload uses `if-no-files-found: ignore`, so the job passes but the report may be missing. | Acceptable; fix SCAP content path or run OpenSCAP locally when needed. |
| **Trivy image job (continue-on-error)** | The Trivy image scan in `ci.yml` has `continue-on-error: true` so the workflow passes even when CRITICAL/HIGH vulnerabilities are present. | When the backend image is hardened and Trivy passes consistently, remove `continue-on-error` to enforce the gate. |
| **Packer on Windows** | Default accelerator is `kvm`; on Windows, QEMU typically needs `accelerator = "tcg"` or WSL2 with KVM. | Documented in [infra/image-builder/README.md](../infra/image-builder/README.md); no code change needed. |
| **Vault dev mode** | The security stack uses Vault dev mode; production requires a real seal (Transit, cloud KMS). | Documented in [containers/security-stack/README.md](../containers/security-stack/README.md). |

---

## Optional Enhancements

| Enhancement | Description |
|-------------|-------------|
| **Single docker-compose for local dev** | A top-level or `containers/full-stack/docker-compose.yml` that composes the data stack + security stack + WUI for local development (with profiles to enable/disable stacks). | Low priority; current per-stack compose is sufficient for Greenfield. |
| **Helm chart for data stack / security stack** | Deploy PostgreSQL, RabbitMQ, NiFi, Keycloak, Vault, Envoy via Helm on Kubernetes instead of Docker Compose. | Optional for teams that standardize on Kubernetes; OpenTofu already deploys Teleport/Kyverno/Falco. |
| **Teleport login scripts** | `scripts/teleport-login.ps1` and `scripts/teleport-login.sh` exist and are referenced in Development.md. Ensure they are linked from the Greenfield guide if Teleport is used in the deployment. | Add a short "Optional: Teleport" subsection in Step 2 or Step 4 of Greenfield linking to these scripts. |

---

**Last updated:** After code-to-documentation review (README, wiki/Overview, wiki/DevSecOps, Greenfield-Deployment.md).
