#!/usr/bin/env bash
# Run on the Incus host (WSL/Linux). No arguments: finds qminiwasm-core from this file's location
# (training-wui/incus -> repo root is ../..).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

if [[ ! -d "$REPO_ROOT/engine" || ! -d "$REPO_ROOT/qminiwasm" ]]; then
  echo "error: expected qminiwasm-core at: $REPO_ROOT" >&2
  echo "  (resolved from: $SCRIPT_DIR — run this script from the repo clone)" >&2
  exit 1
fi

echo "Repo root: $REPO_ROOT"
exec "$SCRIPT_DIR/setup-instance.sh" "$REPO_ROOT"
