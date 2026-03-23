#!/usr/bin/env bash
# Run OpenTofu/Terraform with RUNPOD_TOKEN -> RUNPOD_API_KEY for the official provider.
# Usage: ./tofu.sh init | ./tofu.sh plan | ./tofu.sh apply | ./tofu.sh destroy
set -euo pipefail
cd "$(dirname "$0")"

if [[ -n "${RUNPOD_TOKEN:-}" && -z "${RUNPOD_API_KEY:-}" ]]; then
  export RUNPOD_API_KEY="$RUNPOD_TOKEN"
fi

if [[ -z "${RUNPOD_API_KEY:-}" ]]; then
  echo "error: set RUNPOD_TOKEN or RUNPOD_API_KEY (e.g. in repo .env)" >&2
  exit 1
fi

if command -v tofu >/dev/null 2>&1; then
  exec tofu "$@"
fi
if command -v terraform >/dev/null 2>&1; then
  exec terraform "$@"
fi
echo "error: install OpenTofu (tofu) or Terraform" >&2
exit 1
