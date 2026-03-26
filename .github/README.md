# GitHub Actions (drift notes)

## CI (`ci.yml`)

- **`taxonomy-lint`**: runs [`scripts/taxonomy_linter.py`](../scripts/taxonomy_linter.py) with `--diff-base` against the PR base commit (or `HEAD^` on push). Fails if **added lines** match forbidden legacy vocabulary ([`configs/ci/taxonomy_linter.json`](../configs/ci/taxonomy_linter.json)). External orchestrators (e.g. N8N after a Gitea webhook) can invoke the same command with a suitable `--diff-base` ref. Locally, `python scripts/taxonomy_linter.py --diff-working` checks uncommitted edits vs `HEAD`.
- **Lint/test** run in an **AlmaLinux 9** Docker image: `black` / `flake8` on `qminiwasm/` (includes `qminiwasm/engine/`), `tests/` (and `wui/backend/` when present). **`mypy` is run on `qminiwasm/` only** (process layer typing lives under `qminiwasm/engine/`).
- **`training-wui-go`** job: `go build` in `training-wui/` (Go 1.22).
- **Security job**: `bandit` on `qminiwasm/`; `pip-audit` uses `requirements.txt`.
- **Container image scan / SPDX SBOM** are not run in this workflow (no canonical `docker/Dockerfile.backend` in-repo). Use `security-scans.yml` or local tooling if you need Trivy/SBOM on a built image.

## Security overlap

- **`security.yml`** (scheduled): advisory pip-audit / gitleaks; many steps use `continue-on-error`.
- **`security-scans.yml`** (PR/push): stricter Bandit + pip-audit + Trivy fs + optional OpenSCAP.
- **Primary merge gate** is typically **`ci.yml`**; treat scheduled scans as supplemental.

## RunPod OpenTofu (`runpod-opentofu.yml`)

- Replaces the legacy **`infra/opentofu/desired`** workflow (removed). Validates **`infra/runpod/`**: `tofu fmt -check`, `init`, `validate`, and **`tofu plan`** when repo secret **`RUNPOD_API_KEY`** is set. **No automated apply** in CI.

## Failed-run → issue (`failed-run-to-issue.yml`)

- **Deduped:** One **open** issue per failed workflow name (`🚨 Workflow failed: CI`, etc.). Repeat failures **comment** on that issue instead of opening another. Project #3 is updated only when an issue is **newly** created.
- `workflow_run` lists **exact workflow names**: `CI`, `Security`, `CodeQL Advanced`, `RunPod OpenTofu`, `Security Scans`, `Semgrep`, `GitHub Alerts to Tickets`.
- Runs when `github.repository_owner == 'kennetholsenatm-gif'` (any repo under that account).

## Branch protection (`branch-protection.yml`)

- **Informational only** (echo). Real enforcement is **Repository → Settings → Rules / branch protection** on GitHub.

## Default branch (`main`)

Integration work for the Stateful WASM layout lands on **`main`**. If **`origin/HEAD`** still points at an older branch (for example `white-paper-integration`), update the repository default:

1. GitHub UI: **Settings → General → Default branch** → select **`main`** (and confirm).
2. Local clones: `git remote set-head origin -a` (refreshes `refs/remotes/origin/HEAD` from the server).
3. Optional CLI (authenticated): `gh repo edit <owner>/<repo> --default-branch main`

After the default branch is **`main`**, you can retire obsolete long-lived branches via normal **delete branch** (UI or `git push origin --delete <branch>`), only when nothing critical still targets them.

## Semgrep

- Triggers on **`main`** and **`master`** (aligned with CI).

## Dependabot alerts (`alerts-to-tickets.yml`)

- Uses **`dependabot.listAlertsForRepo`**; opens issues for HIGH/CRITICAL. New issues can be auto-added to Project #3 by **`add-to-project.yml`** when `ADD_TO_PROJECT_TOKEN` is configured.
