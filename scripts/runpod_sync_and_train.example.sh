#!/usr/bin/env bash
# Example: sync repo to a Runpod pod and run the engine with ACCELERATOR=cuda.
# Prerequisites: tofu/OpenTofu applied in infra/runpod, SSH access to the pod, rsync.
# Usage:
#   chmod +x scripts/runpod_sync_and_train.example.sh
#   REPO_ROOT=/path/to/qminiwasm-core ./scripts/runpod_sync_and_train.example.sh configs/training/mesh_cpu.toml
set -euo pipefail

REPO_ROOT="${REPO_ROOT:-$(cd "$(dirname "$0")/.." && pwd)}"
CONFIG_REL="${1:-configs/training/mesh_cuda.toml}"
REMOTE_DIR="${RUNPOD_REMOTE_DIR:-/workspace/qminiwasm-core}"
SSH_USER="${RUNPOD_SSH_USER:-root}"

cd "$REPO_ROOT/infra/runpod"
IP="$(tofu output -raw public_ip 2>/dev/null || true)"
if [[ -z "${IP}" ]]; then
  echo "No public_ip in tofu output. Run: cd infra/runpod && tofu apply" >&2
  exit 1
fi

echo "Syncing $REPO_ROOT -> ${SSH_USER}@${IP}:${REMOTE_DIR}"
rsync -avz --exclude .git --exclude .venv --exclude __pycache__ --exclude artifacts \
  "$REPO_ROOT/" "${SSH_USER}@${IP}:${REMOTE_DIR}/"

echo "Running engine on pod (ACCELERATOR=cuda)..."
ssh "${SSH_USER}@${IP}" bash -s <<EOF
set -euo pipefail
cd ${REMOTE_DIR}
if [[ ! -d .venv ]]; then python -m venv .venv; fi
source .venv/bin/activate
pip install -q -U pip
pip install -q -e ".[training]"
export ACCELERATOR=cuda
python -m engine --config ${CONFIG_REL}
EOF
