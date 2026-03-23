# GitHub Actions (drift notes)

## CI (`ci.yml`)

- **Lint/test** run in an **AlmaLinux 9** Docker image: `black` / `flake8` on `qminiwasm/`, `tests/`, `engine/` (and `wui/backend/` when present). **`mypy` is run on `qminiwasm/` only**; full `mypy` on `engine/` is deferred until `engine/config.py` typing is tightened.
- **`training-wui-go`** job: `go build` in `training-wui/` (Go 1.22).
- **Security job**: `bandit` on `qminiwasm/` and `engine/`; `pip-audit` uses `requirements.txt`.
- **Trivy / SBOM** jobs build `docker/Dockerfile.backend`.

## Security overlap

- **`security.yml`** (scheduled): advisory pip-audit / gitleaks; many steps use `continue-on-error`.
- **`security-scans.yml`** (PR/push): stricter Bandit + pip-audit + Trivy fs + optional OpenSCAP.
- **Primary merge gate** is typically **`ci.yml`**; treat scheduled scans as supplemental.

## RunPod OpenTofu (`runpod-opentofu.yml`)

- Replaces the legacy **`infra/opentofu/desired`** workflow (removed). Validates **`infra/runpod/`**: `tofu fmt -check`, `init`, `validate`, and **`tofu plan`** when repo secret **`RUNPOD_API_KEY`** is set. **No automated apply** in CI.

## Failed-run → issue (`failed-run-to-issue.yml`)

- `workflow_run` lists **exact workflow names**: `CI`, `Security`, `CodeQL Advanced`, `RunPod OpenTofu`, `Security Scans`, `Semgrep`, `GitHub Alerts to Tickets`.
- Runs when `github.repository_owner == 'kennetholsenatm-gif'` (any repo under that account).

## Branch protection (`branch-protection.yml`)

- **Informational only** (echo). Real enforcement is **Repository → Settings → Rules / branch protection** on GitHub.

## Semgrep

- Triggers on **`main`** and **`master`** (aligned with CI).

## Dependabot alerts (`alerts-to-tickets.yml`)

- Uses **`dependabot.listAlertsForRepo`**; opens issues for HIGH/CRITICAL. New issues can be auto-added to Project #3 by **`add-to-project.yml`** when `ADD_TO_PROJECT_TOKEN` is configured.
