#!/usr/bin/env bash
# One-time / repeat bootstrap: ensure terraform.tfvars, tofu init, plan; optional apply.
# Run from repo root. Loads .env for RUNPOD_TOKEN if present.
# Usage:
#   ./scripts/runpod_bootstrap.sh           # init + plan only
#   APPLY=1 ./scripts/runpod_bootstrap.sh    # also tofu apply (starts billing)
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
RP_DIR="$REPO_ROOT/infra/runpod"

cd "$REPO_ROOT"
if [[ -f .env ]]; then
  set -a
  # shellcheck disable=SC1090
  source <(sed 's/\r$//' .env)
  set +a
fi

if [[ -z "${RUNPOD_API_KEY:-}" && -z "${RUNPOD_TOKEN:-}" ]]; then
  echo "error: set RUNPOD_TOKEN or RUNPOD_API_KEY in .env (see .env.example)" >&2
  exit 1
fi

cd "$RP_DIR"
if [[ ! -f terraform.tfvars ]]; then
  cp terraform.tfvars.example terraform.tfvars
  echo "Created infra/runpod/terraform.tfvars from example — edit gpu_type_ids / data_center_ids if plan fails."
fi

./tofu.sh init
./tofu.sh plan

if [[ "${APPLY:-}" == "1" ]]; then
  echo "APPLY=1: running tofu apply (pod will incur cost)..."
  ./tofu.sh apply
else
  echo "Next: review the plan, then run APPLY=1 ./scripts/runpod_bootstrap.sh or: cd infra/runpod && ./tofu.sh apply"
fi
