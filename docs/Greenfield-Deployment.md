# Greenfield Deployment Guide

Step-by-step, zero-to-hero guide for deploying Q-Mini-WASM on a **completely clean, air-gapped ruggedized node**. Assumes no pre-existing containers, databases, or identity providers. Follow the steps in order; each step assumes the previous one is complete.

---

## Prerequisites (one-time)

- **Build host** (for Step 1): Packer 1.7+, QEMU (`qemu-system-x86_64`, `qemu-img`), and for Linux KVM acceleration. See [infra/image-builder/README.md](../infra/image-builder/README.md).
- **Target node** (or same host): Docker and Docker Compose v2+. For air-gap, pre-load images and copy `containers/` and any `.env` files.
- **Secrets**: Prepare strong passwords and (for production) TLS certs; never commit them. Use `.env` files (from `.env.example` templates) and keep them out of version control.

---

## Step 1: Build the Host Appliance (Packer / QEMU)

Build the AlmaLinux 9 golden image that will run the rest of the stack. This image includes K3s, basic STIG-like hardening, and is suitable for tactical edge.

1. **On the build host**, install Packer and QEMU (see [infra/image-builder/README.md](../infra/image-builder/README.md)).

2. **From the repo root:**
   ```bash
   cd infra/image-builder/packer/
   packer init .
   packer validate .
   packer fmt .
   packer build -var "ssh_password=YourSecurePassword" .
   ```

3. **Output:** `infra/image-builder/packer/output-almalinux9/almalinux9-golden.qcow2`.

4. **Deploy the QCOW2** to your ruggedized node using your hypervisor or cloud (e.g. copy to the node and run with KVM/libvirt). Boot the VM; it will have AlmaLinux 9, K3s (or Docker if you changed the provision script), SSH (use `admin` after disabling root login), chrony, and firewalld configured.

5. **Optional:** If the build fails while waiting for SSH, the AlmaLinux 9 minimal ISO boot menu may differ; adjust `boot_command` in `almalinux-9.pkr.hcl` (see "Boot command tuning" in the image-builder README).

---

## Step 2: Bootstrap the Security Stack (Vault, Keycloak)

On the node (or a dedicated host with Docker), run the Zero Trust security stack: Keycloak (human identity, Passkeys), Vault (machine identity, PKI/mTLS), and Envoy (TLS 1.3 + PQC-ready).

1. **From the repo root:**
   ```bash
   cd containers/security-stack/
   cp .env.example .env
   # Edit .env: set KEYCLOAK_DB_PASSWORD, KEYCLOAK_ADMIN_PASSWORD (and VAULT_DEV_ROOT_TOKEN_ID for dev).
   docker compose up -d
   ```

2. **Verify:** Keycloak at `http://<host>:8080`, Vault at `http://<host>:8200`. For production TLS, place `tls.crt` and `tls.key` in `proxy/certs/` and use `proxy/envoy-tls.yaml` as the Envoy config (see [containers/security-stack/README.md](../containers/security-stack/README.md)).

3. **Run Vault PKI setup** (once Vault is up; requires `vault` and `jq` on the host):
   ```bash
   export VAULT_ADDR=http://localhost:8200
   export VAULT_TOKEN=root   # or your VAULT_DEV_ROOT_TOKEN_ID from .env
   ./vault-init/setup-pki.sh
   ```
   Save the printed Intermediate CA cert if you will enable mTLS for PostgreSQL (Step 3 optional).

4. **Keycloak:** The `qminiwasm` realm is auto-imported from `keycloak-init/qminiwasm-realm.json`. Configure the Teleport OIDC client (redirect URIs, client secret) and user registration; see [containers/security-stack/keycloak-init/README.md](../containers/security-stack/keycloak-init/README.md). For developer login via Teleport (SSH/Kubernetes), use [scripts/teleport-login.ps1](../scripts/teleport-login.ps1) or [scripts/teleport-login.sh](../scripts/teleport-login.sh) after Teleport is deployed (see [wiki/Development](https://github.com/kennetholsenatm-gif/LLM_Pract/wiki/Development)).

5. **Production:** Replace Vault dev mode with a proper seal (Transit, cloud KMS); do not use the dev root token in production.

---

## Step 3: Spin Up the Data Stack (NiFi, DB, Broker)

Event-driven data pipeline: PostgreSQL (pgvector), RabbitMQ, Apache NiFi. Run on the same node or a separate host with Docker.

1. **From the repo root:**
   ```bash
   cd containers/data-stack/
   cp .env.example .env
   # Edit .env: set POSTGRES_PASSWORD and RABBITMQ_DEFAULT_PASS.
   docker compose up -d
   ```

2. **Verify:** NiFi at `http://<host>:8080`, RabbitMQ Management at `http://<host>:15672`. PostgreSQL is on the internal network (`postgres:5432`); expose 5432 only if host apps need direct access.

3. **Optional mTLS for Postgres:** To have services (e.g. NiFi, WUI backend) connect to PostgreSQL with client certificates (no passwords), use [containers/data-stack/postgres-mtls.conf](../containers/data-stack/postgres-mtls.conf): configure Postgres with `ssl = on`, server cert/key, and `ssl_ca_file` (Vault intermediate CA). Add the `hostssl ... cert` line to `pg_hba.conf`. Services then request short-lived client certs from Vault (`pki_int/issue/postgres-client`) and connect with `sslmode=verify-full` (see [containers/security-stack/README.md](../containers/security-stack/README.md#service-requesting-a-certificate-from-vault-passwordless-db)).

4. **NiFi:** Configure flows to consume from RabbitMQ and write to PostgreSQL (JDBC); see [containers/data-stack/README.md](../containers/data-stack/README.md).

---

## Step 4: Deploy the Application (WUI Backend and Frontend)

Run the Q-Mini-WASM Web UI (FastAPI backend + React frontend). You can run it with Docker Compose, Docker alone, or Kubernetes (Helm).

### Option A: Docker (single backend image)

1. **Build and run the WUI backend** (from repo root):
   ```bash
   docker build -f wui/backend/Dockerfile -t qminiwasm-backend:latest .
   docker run -d -p 8000:8000 --name wui-backend qminiwasm-backend:latest
   ```
   If the WUI needs to reach the data stack or security stack, run on the same host or use the appropriate network and env vars (e.g. `POSTGRES_HOST`, `RABBITMQ_HOST`).

2. **Frontend:** Build and serve the React app (e.g. `wui/frontend/`) and point it to the backend API URL. See [containers/wui/README.md](../containers/wui/README.md) for full container build options.

### Option B: Kubernetes (Helm)

1. **Ensure** the cluster has access to the images (e.g. after Trivy scan in CI, or from your registry). The Helm chart is in [charts/qminiwasm-wui/](https://github.com/kennetholsenatm-gif/LLM_Pract/tree/main/charts/qminiwasm-wui).

2. **Install** (from repo root; chart is in-repo):
   ```bash
   helm install qminiwasm-wui ./charts/qminiwasm-wui -n <namespace> --create-namespace
   ```
   Override with `-f my-values.yaml` as needed.
   Override `image.repository` and `image.tag` as needed. If Kyverno requires a Trivy scan label, set `podAnnotations["trivy.scan/passed"]="true"` after CI passes.

3. **Configure** the WUI for your environment (env vars, ingress, TLS) per the chart values.

---

## Order Summary

| Step | What you build/run | Where |
|------|--------------------|-------|
| 1 | AlmaLinux 9 QCOW2 golden image (K3s, hardening) | Packer on build host → deploy QCOW2 to node |
| 2 | Keycloak, Vault, Envoy (security stack) | `containers/security-stack/` → `docker compose up -d` |
| 3 | PostgreSQL, RabbitMQ, NiFi (data stack) | `containers/data-stack/` → `docker compose up -d` |
| 4 | WUI backend + frontend | Docker or Kubernetes (Helm chart) |

---

## Air-gap and ruggedized notes

- **Air-gap:** Pre-download all container images and the Packer ISO on a connected machine; copy them and the repo (or tarball) to the target node. Use `docker load` and local file paths for Packer (`iso_url` = local path). No default passwords; set all secrets via `.env` or environment.
- **Ruggedized:** The Packer-built image is minimal and hardened (chrony, firewalld, non-root SSH). Run containers with resource limits (already set in the provided `docker-compose.yml` files) and restrict exposed ports to what is necessary.

---

## Links

- [infra/image-builder/README.md](../infra/image-builder/README.md) — Packer/QEMU details
- [containers/security-stack/README.md](../containers/security-stack/README.md) — Keycloak, Vault, Envoy, PKI
- [containers/data-stack/README.md](../containers/data-stack/README.md) — Data stack architecture and NiFi
- [docs/DockerOS-Platform-Standard.md](DockerOS-Platform-Standard.md) — AlmaLinux 9 host standard
- [docs/TODO.md](TODO.md) — Known gaps and missing automation
