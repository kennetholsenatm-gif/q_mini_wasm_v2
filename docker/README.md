# Container definitions

- **`Dockerfile.backend`** — Minimal AlmaLinux image used by `.github/workflows/ci.yml` for **Trivy** image scans and SPDX SBOM generation. Must stay **tracked in git** (see `.gitignore` exceptions for `docker/Dockerfile.*`).
