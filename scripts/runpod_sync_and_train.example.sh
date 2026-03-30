#!/usr/bin/env bash
# Example: sync repo to a Runpod pod and run native training (C++ gRPC + qmw-grpc-train).
# Full flow: docs/RUNPOD_QUICKSTART.md
# Prerequisites: tofu/OpenTofu applied in infra/runpod, SSH access, rsync, built C++ server + Go on the pod.
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

echo "Running native training on pod (adjust server binary path if needed)..."
ssh "${SSH_USER}@${IP}" bash -s <<EOF
set -euo pipefail
cd ${REMOTE_DIR}
export ACCELERATOR=cuda
SRV="./build/cpp/training/qminiwasm_training_engine_server"
if [[ ! -x "\$SRV" ]]; then
  echo "Missing executable: \$SRV — build cpp/training first (see cpp/training/README.md)" >&2
  exit 1
fi
"\$SRV" 127.0.0.1:50061 &
sleep 2
cd training-wui
go run ./cmd/qmw-grpc-train -root .. -config ${CONFIG_REL} -grpc 127.0.0.1:50061
EOF
