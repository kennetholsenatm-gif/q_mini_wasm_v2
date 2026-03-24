#!/usr/bin/env bash
# Verify RunPod + OpenTofu prerequisites from repo root (bash / WSL / Git Bash).
# Usage: ./scripts/runpod_prereqs.sh
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

load_env() {
  if [[ -f .env ]]; then
    set -a
    # shellcheck disable=SC1090
    source <(sed 's/\r$//' .env)
    set +a
  fi
}
load_env

ERR=0

if [[ -z "${RUNPOD_API_KEY:-}" && -z "${RUNPOD_TOKEN:-}" ]]; then
  echo "MISSING: RUNPOD_TOKEN or RUNPOD_API_KEY (add to repo .env — see .env.example and docs/RUNPOD_QUICKSTART.md)"
  echo "  API key: https://www.runpod.io/console/user/settings"
  ERR=1
else
  echo "OK: RunPod credential present (RUNPOD_API_KEY or RUNPOD_TOKEN)."
fi

if command -v tofu >/dev/null 2>&1; then
  echo "OK: OpenTofu: $(command -v tofu)"
elif command -v terraform >/dev/null 2>&1; then
  echo "OK: Terraform: $(command -v terraform)"
else
  echo "MISSING: Install OpenTofu (tofu) or Terraform — https://opentofu.org/docs/intro/install/"
  ERR=1
fi

TFVARS="$REPO_ROOT/infra/runpod/terraform.tfvars"
if [[ -f "$TFVARS" ]]; then
  echo "OK: infra/runpod/terraform.tfvars exists."
else
  echo "WARN: infra/runpod/terraform.tfvars missing — run ./scripts/runpod_bootstrap.sh or cp infra/runpod/terraform.tfvars.example infra/runpod/terraform.tfvars"
  ERR=1
fi

if [[ ! -d "$REPO_ROOT/infra/runpod/.terraform" ]]; then
  echo "HINT: OpenTofu not initialized in infra/runpod — run ./scripts/runpod_bootstrap.sh or: cd infra/runpod && ./tofu.sh init"
fi

exit "$ERR"
