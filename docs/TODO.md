# Documentation and automation gaps

Short follow-ups for maintainers. Prefer **GitHub Issues** for new work items.

## CI and security

| Item | Notes |
|------|-------|
| **Security Scans workflow** | Uses `pip install -r requirements/security.txt` with fallback to minimal deps; local docs may mention `pip install -e ".[security]"`. |
| **OpenSCAP in CI** | Report may be missing; artifact upload uses `if-no-files-found: ignore`. |
| **Trivy image job** | `continue-on-error: true` in `.github/workflows/ci.yml` until container images are hardened consistently. |

## Wiki vs repo layout

Some **wiki** pages describe optional `containers/`, legacy `wui/`, or full-stack OpenTofu layouts that are not present in every checkout. For **training**, **engine**, and **RunPod**, use the repo [README.md](../README.md), [training-wui/README.md](../training-wui/README.md), and [docs/RUNPOD_QUICKSTART.md](RUNPOD_QUICKSTART.md).

**Last updated:** After trimming obsolete docs from `docs/` (white-paper and greenfield guides removed).
