#!/usr/bin/env bash
# Destroy RunPod pod managed by infra/runpod (stops billing). Run from repo root.
# Usage: ./scripts/runpod_destroy.sh
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"

cd "$REPO_ROOT"
if [[ -f .env ]]; then
  set -a
  # shellcheck disable=SC1090
  source <(sed 's/\r$//' .env)
  set +a
fi

if [[ -z "${RUNPOD_API_KEY:-}" && -z "${RUNPOD_TOKEN:-}" ]]; then
  echo "error: set RUNPOD_TOKEN or RUNPOD_API_KEY in .env" >&2
  exit 1
fi

cd "$REPO_ROOT/infra/runpod"
./tofu.sh destroy
